/*
 * Header file for sample IPP server implementation.
 *
 * Copyright 2014-2017 by the IEEE-ISTO Printer Working Group
 * Copyright 2010-2017 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

/*
 * Disable private and deprecated stuff so we can verify that the public API
 * is sufficient to implement a server.
 */

#define _IPP_PRIVATE_STRUCTURES 0	/* Disable private IPP stuff */
#define _CUPS_NO_DEPRECATED 1		/* Disable deprecated stuff */


/*
 * Include necessary headers...
 */

#include <config.h>			/* CUPS configuration header */
#include <cups/cups.h>			/* Public API */
#include <cups/string-private.h>	/* CUPS string functions */
#include <cups/thread-private.h>	/* For multithreading functions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#ifdef WIN32
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  define WEXITSTATUS(s) (s)
#  include <winsock2.h>
typedef ULONG nfds_t;
#  define poll WSAPoll
#else
extern char **environ;

#  include <sys/fcntl.h>
#  include <sys/wait.h>
#  include <poll.h>
#endif /* WIN32 */

#ifdef HAVE_DNSSD
#  include <dns_sd.h>
#elif defined(HAVE_AVAHI)
#  include <avahi-client/client.h>
#  include <avahi-client/publish.h>
#  include <avahi-common/error.h>
#  include <avahi-common/thread-watch.h>
#endif /* HAVE_DNSSD */

#ifdef HAVE_SYS_MOUNT_H
#  include <sys/mount.h>
#endif /* HAVE_SYS_MOUNT_H */
#ifdef HAVE_SYS_STATFS_H
#  include <sys/statfs.h>
#endif /* HAVE_SYS_STATFS_H */
#ifdef HAVE_SYS_STATVFS_H
#  include <sys/statvfs.h>
#endif /* HAVE_SYS_STATVFS_H */
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif /* HAVE_SYS_VFS_H */

#ifdef HAVE_PTHREAD_H
#  define _cupsCondDeinit(c)	pthread_cond_destroy(c)
#  define _cupsMutexDeinit(m)	pthread_mutex_destroy(m)
#  define _cupsRWDeinit(rw)	pthread_rwlock_destroy(rw)
#else
#  define _cupsCondDeinit(c)
#  define _cupsMutexDeinit(m)
#  define _cupsRWDeinit(rw)
#endif /* HAVE_PTHREAD_H */

#ifdef _MAIN_C_
#  define VAR
#  define VALUE(...) =__VA_ARGS__
#else
#  define VAR extern
#  define VALUE(...)
#endif /* _MAIN_C_ */


/*
 * Constants...
 */

/* Maximum lease duration value from RFC 3995 - 2^26-1 seconds or ~2 years */
#  define SERVER_NOTIFY_LEASE_DURATION_MAX		67108863
/* But a value of 0 means "never expires"... */
#  define SERVER_NOTIFY_LEASE_DURATION_FOREVER		0
/* Default duration is 1 day */
#  define SERVER_NOTIFY_LEASE_DURATION_DEFAULT		86400


/* URL schemes and DNS-SD types for IPP and web resources... */
#  define SERVER_IPP_SCHEME "ipp"
#  define SERVER_IPP_TYPE "_ipp._tcp"
#  define SERVER_IPPS_SCHEME "ipps"
#  define SERVER_IPPS_TYPE "_ipps._tcp"
#  define SERVER_IPPS_3D_TYPE "_ipps-3d._tcp"
#  define SERVER_WEB_TYPE "_http._tcp"
#  define SERVER_HTTP_SCHEME "http"
#  define SERVER_HTTPS_SCHEME "https"


/*
 * LogLevel constants...
 */

typedef enum server_loglevel_e
{
  SERVER_LOGLEVEL_ERROR,
  SERVER_LOGLEVEL_INFO,
  SERVER_LOGLEVEL_DEBUG
} server_loglevel_t;

/*
 * Event mask enumeration...
 */

enum server_event_e			/* notify-events bit values */
{
  SERVER_EVENT_DOCUMENT_COMPLETED = 0x00000001,
  SERVER_EVENT_DOCUMENT_CONFIG_CHANGED = 0x00000002,
  SERVER_EVENT_DOCUMENT_CREATED = 0x00000004,
  SERVER_EVENT_DOCUMENT_FETCHABLE = 0x00000008,
  SERVER_EVENT_DOCUMENT_STATE_CHANGED = 0x00000010,
  SERVER_EVENT_DOCUMENT_STOPPED = 0x00000020,
  SERVER_EVENT_JOB_COMPLETED = 0x00000040,
  SERVER_EVENT_JOB_CONFIG_CHANGED = 0x00000080,
  SERVER_EVENT_JOB_CREATED = 0x00000100,
  SERVER_EVENT_JOB_FETCHABLE = 0x00000200,
  SERVER_EVENT_JOB_PROGRESS = 0x00000400,
  SERVER_EVENT_JOB_STATE_CHANGED = 0x00000800,
  SERVER_EVENT_JOB_STOPPED = 0x00001000,
  SERVER_EVENT_PRINTER_CONFIG_CHANGED = 0x00002000,
  SERVER_EVENT_PRINTER_FINISHINGS_CHANGED = 0x00004000,
  SERVER_EVENT_PRINTER_MEDIA_CHANGED = 0x00008000,
  SERVER_EVENT_PRINTER_QUEUE_ORDER_CHANGED = 0x00010000,
  SERVER_EVENT_PRINTER_RESTARTED = 0x00020000,
  SERVER_EVENT_PRINTER_SHUTDOWN = 0x00040000,
  SERVER_EVENT_PRINTER_STATE_CHANGED = 0x00080000,
  SERVER_EVENT_PRINTER_STOPPED = 0x00100000,

  /* "Wildcard" values... */
  SERVER_EVENT_NONE = 0x00000000,		/* Nothing */
  SERVER_EVENT_DOCUMENT_ALL = 0x0000003f,
  SERVER_EVENT_DOCUMENT_STATE_ALL = 0x00000037,
  SERVER_EVENT_JOB_ALL = 0x00001fc0,
  SERVER_EVENT_JOB_STATE_ALL = 0x00001940,
  SERVER_EVENT_PRINTER_ALL = 0x001fe000,
  SERVER_EVENT_PRINTER_CONFIG_ALL = 0x0000e000,
  SERVER_EVENT_PRINTER_STATE_ALL = 0x001e0000,
  SERVER_EVENT_ALL = 0x001fffff		/* Everything */
};
typedef unsigned int server_event_t;	/* Bitfield for notify-events */
#define SERVER_EVENT_DEFAULT SERVER_EVENT_JOB_COMPLETED
#define SERVER_EVENT_DEFAULT_STRING "job-completed"
VAR const char * const server_events[21]
VALUE({					/* Strings for bits */
  /* "none" is implied for no bits set */
  "document-completed",
  "document-config-changed",
  "document-created",
  "document-fetchable",
  "document-state-changed",
  "document-stopped",
  "job-completed",
  "job-config-changed",
  "job-created",
  "job-fetchable",
  "job-progress",
  "job-state-changed",
  "job-stopped",
  "printer-config-changed",
  "printer-finishings-changed",
  "printer-media-changed",
  "printer-queue-order-changed",
  "printer-restarted",
  "printer-shutdown",
  "printer-state-changed",
  "printer-stopped"
});

enum server_jreason_e			/* job-state-reasons bit values */
{
  SERVER_JREASON_NONE = 0x00000000,	/* none */
  SERVER_JREASON_ABORTED_BY_SYSTEM = 0x00000001,
  SERVER_JREASON_COMPRESSION_ERROR = 0x00000002,
  SERVER_JREASON_DOCUMENT_ACCESS_ERROR = 0x00000004,
  SERVER_JREASON_DOCUMENT_FORMAT_ERROR = 0x00000008,
  SERVER_JREASON_DOCUMENT_PASSWORD_ERROR = 0x00000010,
  SERVER_JREASON_DOCUMENT_PERMISSION_ERROR = 0x00000020,
  SERVER_JREASON_DOCUMENT_SECURITY_ERROR = 0x00000040,
  SERVER_JREASON_DOCUMENT_UNPRINTABLE_ERROR = 0x00000080,
  SERVER_JREASON_ERRORS_DETECTED = 0x00000100,
  SERVER_JREASON_JOB_CANCELED_AT_DEVICE = 0x00000200,
  SERVER_JREASON_JOB_CANCELED_BY_USER = 0x00000400,
  SERVER_JREASON_JOB_COMPLETED_SUCCESSFULLY = 0x00000800,
  SERVER_JREASON_JOB_COMPLETED_WITH_ERRORS = 0x00001000,
  SERVER_JREASON_JOB_COMPLETED_WITH_WARNINGS = 0x00002000,
  SERVER_JREASON_JOB_DATA_INSUFFICIENT = 0x00004000,
  SERVER_JREASON_JOB_FETCHABLE = 0x00008000,
  SERVER_JREASON_JOB_INCOMING = 0x00010000,
  SERVER_JREASON_JOB_PASSWORD_WAIT = 0x00020000,
  SERVER_JREASON_JOB_PRINTING = 0x00040000,
  SERVER_JREASON_JOB_QUEUED = 0x00080000,
  SERVER_JREASON_JOB_SPOOLING = 0x00100000,
  SERVER_JREASON_JOB_STOPPED = 0x00200000,
  SERVER_JREASON_JOB_TRANSFORMING = 0x00400000,
  SERVER_JREASON_PRINTER_STOPPED = 0x00800000,
  SERVER_JREASON_PRINTER_STOPPED_PARTLY = 0x01000000,
  SERVER_JREASON_PROCESSING_TO_STOP_POINT = 0x02000000,
  SERVER_JREASON_QUEUED_IN_DEVICE = 0x04000000,
  SERVER_JREASON_WARNINGS_DETECTED = 0x08000000
};
typedef unsigned int server_jreason_t;	/* Bitfield for job-state-reasons */
VAR const char * const server_jreasons[28]
VALUE({					/* Strings for bits */
  /* "none" is implied for no bits set */
  "aborted-by-system",
  "compression-error",
  "document-access-error",
  "document-format-error",
  "document-password-error",
  "document-permission-error",
  "document-security-error",
  "document-unprintable-error",
  "errors-detected",
  "job-canceled-at-device",
  "job-canceled-by-user",
  "job-completed-successfully",
  "job-completed-with-errors",
  "job-completed-with-warnings",
  "job-data-insufficient",
  "job-fetchable",
  "job-incoming",
  "job-password-wait",
  "job-printing",
  "job-queued",
  "job-spooling",
  "job-stopped",
  "job-transforming",
  "printer-stopped",
  "printer-stopped-partly",
  "processing-to-stop-point",
  "queued-in-device",
  "warnings-detected"
});

enum server_preason_e			/* printer-state-reasons bit values */
{
  SERVER_PREASON_NONE = 0x0000,		/* none */
  SERVER_PREASON_OTHER = 0x0001,		/* other */
  SERVER_PREASON_COVER_OPEN = 0x0002,	/* cover-open */
  SERVER_PREASON_INPUT_TRAY_MISSING = 0x0004,
					/* input-tray-missing */
  SERVER_PREASON_MARKER_SUPPLY_EMPTY = 0x0008,
					/* marker-supply-empty */
  SERVER_PREASON_MARKER_SUPPLY_LOW = 0x0010,
					/* marker-supply-low */
  SERVER_PREASON_MARKER_WASTE_ALMOST_FULL = 0x0020,
					/* marker-waste-almost-full */
  SERVER_PREASON_MARKER_WASTE_FULL = 0x0040,
					/* marker-waste-full */
  SERVER_PREASON_MEDIA_EMPTY = 0x0080,	/* media-empty */
  SERVER_PREASON_MEDIA_JAM = 0x0100,	/* media-jam */
  SERVER_PREASON_MEDIA_LOW = 0x0200,	/* media-low */
  SERVER_PREASON_MEDIA_NEEDED = 0x0400,	/* media-needed */
  SERVER_PREASON_MOVING_TO_PAUSED = 0x0800,
					/* moving-to-paused */
  SERVER_PREASON_PAUSED = 0x1000,		/* paused */
  SERVER_PREASON_SPOOL_AREA_FULL = 0x2000,/* spool-area-full */
  SERVER_PREASON_TONER_EMPTY = 0x4000,	/* toner-empty */
  SERVER_PREASON_TONER_LOW = 0x8000	/* toner-low */
};
typedef unsigned int server_preason_t;	/* Bitfield for printer-state-reasons */
VAR const char * const server_preasons[16]
VALUE({					/* Strings for bits */
  /* "none" is implied for no bits set */
  "other",
  "cover-open",
  "input-tray-missing",
  "marker-supply-empty",
  "marker-supply-low",
  "marker-waste-almost-full",
  "marker-waste-full",
  "media-empty",
  "media-jam",
  "media-low",
  "media-needed",
  "moving-to-paused",
  "paused",
  "spool-area-full",
  "toner-empty",
  "toner-low"
});

typedef enum server_transform_e		/* Transform modes for server */
{
  SERVER_TRANSFORM_COMMAND,		/* Run command for print job processing */
  SERVER_TRANSFORM_TO_CLIENT,		/* Send output to client */
  SERVER_TRANSFORM_TO_FILE		/* Send output to file */
} server_transform_t;


/*
 * Base types...
 */

#  ifdef HAVE_DNSSD
typedef DNSServiceRef server_srv_t;	/* Service reference */
typedef TXTRecordRef server_txt_t;	/* TXT record */
typedef DNSRecordRef server_loc_t;	/* LOC record */

#  elif defined(HAVE_AVAHI)
typedef AvahiEntryGroup *server_srv_t;	/* Service reference */
typedef AvahiStringList *server_txt_t;	/* TXT record */
typedef void *server_loc_t;		/* LOC record */

#  else
typedef void *server_srv_t;		/* Service reference */
typedef void *server_txt_t;		/* TXT record */
typedef void *server_loc_t;		/* LOC record */
#  endif /* HAVE_DNSSD */


/*
 * Structures...
 */

typedef struct server_filter_s		/**** Attribute filter ****/
{
  cups_array_t		*ra;		/* Requested attributes */
  ipp_tag_t		group_tag;	/* Group to copy */
} server_filter_t;

typedef struct server_job_s server_job_t;

typedef struct server_device_s		/**** Output Device data ****/
{
  _cups_rwlock_t	rwlock;		/* Printer lock */
  char			*name,		/* printer-name (mapped to output-device) */
			*uuid;		/* output-device-uuid */
  ipp_t			*attrs;		/* All printer attributes */
  ipp_pstate_t		state;		/* printer-state value */
  server_preason_t	reasons;	/* printer-state-reasons values */
} server_device_t;

typedef struct server_lang_s		/**** Localization data ****/
{
  char			*lang,		/* Language code */
			*filename;	/* Strings file */
} server_lang_t;

typedef struct server_printer_s		/**** Printer data ****/
{
  _cups_rwlock_t	rwlock;		/* Printer lock */
  server_srv_t		ipp_ref,	/* Bonjour IPP service */
#ifdef HAVE_SSL
			ipps_ref,	/* Bonjour IPPS service */
#endif /* HAVE_SSL */
			http_ref,	/* Bonjour HTTP(S) service */
			printer_ref;	/* Bonjour LPD service */
  server_loc_t		geo_ref;	/* Bonjour geo-location */
  char			*default_uri,	/* Default/first URI */
			*resource,	/* Resource path */
                        *dnssd_name,	/* printer-dnssd-name */
			*name,		/* printer-name */
                        *icon,		/* Icon file */
                        *command,	/* Command to run for job processing, if any */
                        *device_uri,	/* Output device URI, if any */
			*output_format,	/* Output format, if any */
			*proxy_user;	/* Proxy username, if any */
  cups_array_t		*strings;	/* Strings files for various languages */
  size_t		resourcelen;	/* Length of resource path */
  cups_array_t		*devices;	/* Associated devices */
  ipp_t			*attrs;		/* Static attributes */
  ipp_t			*dev_attrs;	/* Current device attributes */
  time_t		start_time;	/* Startup time */
  time_t		config_time;	/* printer-config-change-time */
  ipp_pstate_t		state,		/* printer-state value */
			dev_state;	/* Current device printer-state value */
  server_preason_t	state_reasons,	/* printer-state-reasons values */
			dev_reasons;	/* Current device printer-state-reasons values */
  time_t		state_time;	/* printer-state-change-time */
  cups_array_t		*jobs,		/* Jobs */
			*active_jobs,	/* Active jobs */
			*completed_jobs;/* Completed jobs */
  server_job_t		*processing_job;/* Current processing job */
  int			next_job_id;	/* Next job-id value */
  cups_array_t		*subscriptions;	/* Subscriptions */
  int			next_sub_id;	/* Next notify-subscription-id value */
} server_printer_t;

struct server_job_s			/**** Job data ****/
{
  int			id;		/* job-id */
  _cups_rwlock_t	rwlock;		/* Job lock */
  const char		*name,		/* job-name */
			*username,	/* job-originating-user-name */
			*format;	/* document-format */
  int			priority;	/* job-priority */
  char			*dev_uuid;	/* output-device-uuid-assigned */
  ipp_jstate_t		state,		/* job-state value */
			dev_state;	/* output-device-job-state value */
  server_jreason_t	state_reasons,	/* job-state-reasons values */
  			dev_state_reasons;
		      			/* output-device-job-state-reasons values */
  char			*dev_state_message;
					/* output-device-job-state-message value */
  time_t		created,	/* time-at-creation value */
			processing,	/* time-at-processing value */
			completed;	/* time-at-completed value */
  int			impressions,	/* job-impressions value */
			impcompleted;	/* job-impressions-completed value */
  ipp_t			*attrs;		/* Attributes */
  int			cancel;		/* Non-zero when job canceled */
  char			*filename;	/* Print file name */
  int			fd;		/* Print file descriptor */
  server_printer_t	*printer;	/* Printer */
};

typedef struct server_subscription_s	/**** Subscription data ****/
{
  int			id;		/* notify-subscription-id */
  const char		*uuid;		/* notify-subscription-uuid */
  _cups_rwlock_t	rwlock;		/* Subscription lock */
  server_event_t	mask;		/* Event mask */
  server_printer_t	*printer;	/* Printer */
  server_job_t		*job;		/* Job, if any */
  ipp_t			*attrs;		/* Attributes */
  const char		*username;	/* notify-subscriber-user-name */
  int			lease;		/* notify-lease-duration */
  int			interval;	/* notify-time-interval */
  time_t		expire;		/* Lease expiration time */
  int			first_sequence,	/* First notify-sequence-number in cache */
			last_sequence;	/* Last notify-sequence-number used */
  cups_array_t		*events;	/* Events (ipp_t *'s) */
  int			pending_delete;	/* Non-zero when the subscription is about to be deleted/canceled */
} server_subscription_t;

typedef struct server_client_s		/**** Client data ****/
{
  int			number;		/* Client number */
  http_t		*http;		/* HTTP connection */
  ipp_t			*request,	/* IPP request */
			*response;	/* IPP response */
  time_t		start;		/* Request start time */
  http_state_t		operation;	/* Request operation */
  ipp_op_t		operation_id;	/* IPP operation-id */
  char			uri[1024],	/* Request URI */
			*options;	/* URI options */
  http_addr_t		addr;		/* Client address */
  char			hostname[256],	/* Client hostname */
			username[32];	/* Client authenticated username */
  server_printer_t	*printer;	/* Printer */
  server_job_t		*job;		/* Current job, if any */
  int			fetch_compression,
					/* Compress file? */
			fetch_file;	/* File to fetch */
} server_client_t;

typedef struct server_listener_s	/**** Listener data ****/
{
  int	fd;				/* Listener socket */
  char	host[256];			/* Hostname, if any */
  int	port;				/* Port number */
} server_listener_t;


/*
 * Globals...
 */

VAR char		*ConfigDirectory VALUE(NULL);
VAR char		*DataDirectory	VALUE(NULL);
VAR int			DefaultPort	VALUE(0);
VAR char		*DefaultPrinter	VALUE(NULL);
VAR http_encryption_t	Encryption	VALUE(HTTP_ENCRYPTION_IF_REQUESTED);
VAR int			KeepFiles	VALUE(0);
#ifdef HAVE_SSL
VAR char		*KeychainPath	VALUE(NULL);
#endif /* HAVE_SSL */
VAR cups_array_t	*Listeners	VALUE(NULL);
VAR char		*LogFile	VALUE(NULL);
VAR server_loglevel_t	LogLevel	VALUE(SERVER_LOGLEVEL_ERROR);
VAR int			MaxJobs		VALUE(100),
                        MaxCompletedJobs VALUE(100);
VAR cups_array_t	*Printers	VALUE(NULL);
VAR char		*ServerName	VALUE(NULL);
VAR char		*SpoolDirectory	VALUE(NULL);

#ifdef HAVE_DNSSD
VAR DNSServiceRef	DNSSDMaster	VALUE(NULL);
#elif defined(HAVE_AVAHI)
VAR AvahiThreadedPoll	*DNSSDMaster	VALUE(NULL);
VAR AvahiClient		*DNSSDClient	VALUE(NULL);
#endif /* HAVE_DNSSD */
VAR char		*DNSSDSubType	VALUE(NULL);

//VAR _cups_mutex_t	SubscriptionMutex VALUE(_CUPS_MUTEX_INITIALIZER);
VAR _cups_cond_t	SubscriptionCondition VALUE(_CUPS_COND_INITIALIZER);


/*
 * Functions...
 */

extern void		serverAddEvent(server_printer_t *printer, server_job_t *job, server_event_t event, const char *message, ...) __attribute__((__format__(__printf__, 4, 5)));
extern void		serverCheckJobs(server_printer_t *printer);
extern void             serverCleanAllJobs(void);
extern void		serverCleanJobs(server_printer_t *printer);
extern void		serverCopyAttributes(ipp_t *to, ipp_t *from, cups_array_t *ra, ipp_tag_t group_tag, int quickcopy);
extern void		serverCopyJobStateReasons(ipp_t *ipp, ipp_tag_t group_tag, server_job_t *job);
extern void		serverCopyPrinterStateReasons(ipp_t *ipp, ipp_tag_t group_tag, server_printer_t *printer);
extern server_client_t	*serverCreateClient(int sock);
extern server_device_t	*serverCreateDevice(server_client_t *client);
extern server_job_t	*serverCreateJob(server_client_t *client);
extern void		serverCreateJobFilename(server_printer_t *printer, server_job_t *job, const char *format, char *fname, size_t fnamesize);
extern int		serverCreateListeners(const char *host, int port);
extern server_printer_t	*serverCreatePrinter(const char *resource, const char *name, const char *location, const char *make, const char *model, const char *icon, const char *docformats, int ppm, int ppm_color, int duplex, int pin, ipp_t *attrs, const char *command, const char *device_uri, const char *output_format, const char *proxy_user, cups_array_t *strings);
extern server_subscription_t *serverCreateSubcription(server_printer_t *printer, server_job_t *job, int interval, int lease, const char *username, ipp_attribute_t *notify_events, ipp_attribute_t *notify_attributes, ipp_attribute_t *notify_user_data);
extern void		serverDeleteClient(server_client_t *client);
extern void		serverDeleteDevice(server_device_t *device);
extern void		serverDeleteJob(server_job_t *job);
extern void		serverDeletePrinter(server_printer_t *printer);
extern void		serverDeleteSubscription(server_subscription_t *sub);
extern void		serverDNSSDInit(void);
extern int		serverFinalizeConfiguration(void);
extern server_device_t	*serverFindDevice(server_client_t *client);
extern server_job_t	*serverFindJob(server_client_t *client, int job_id);
extern server_printer_t	*serverFindPrinter(const char *resource);
extern server_subscription_t *serverFindSubscription(server_client_t *client, int sub_id);
extern server_jreason_t	serverGetJobStateReasonsBits(ipp_attribute_t *attr);
extern server_event_t	serverGetNotifyEventsBits(ipp_attribute_t *attr);
extern const char	*serverGetNotifySubscribedEvent(server_event_t event);
extern server_preason_t	serverGetPrinterStateReasonsBits(ipp_attribute_t *attr);
extern ipp_t		*serverLoadAttributes(const char *filename, char **authtype, char **command, char **device_uri, char **output_format, char **make, char **model, char **proxy_user, cups_array_t **strings);
extern int		serverLoadConfiguration(const char *directory);
extern void		serverLog(server_loglevel_t level, const char *format, ...) __attribute__((__format__(__printf__, 2, 3)));
extern void		serverLogAttributes(server_client_t *client, const char *title, ipp_t *ipp, int type);
extern void		serverLogClient(server_loglevel_t level, server_client_t *client, const char *format, ...) __attribute__((__format__(__printf__, 3, 4)));
extern void		serverLogJob(server_loglevel_t level, server_job_t *job, const char *format, ...) __attribute__((__format__(__printf__, 3, 4)));
extern void		serverLogPrinter(server_loglevel_t level, server_printer_t *printer, const char *format, ...) __attribute__((__format__(__printf__, 3, 4)));
extern void		*serverProcessClient(server_client_t *client);
extern int		serverProcessHTTP(server_client_t *client);
extern int		serverProcessIPP(server_client_t *client);
extern void		*serverProcessJob(server_job_t *job);
extern int		serverRespondHTTP(server_client_t *client, http_status_t code, const char *content_coding, const char *type, size_t length);
extern void		serverRespondIPP(server_client_t *client, ipp_status_t status, const char *message, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
extern void		serverRespondUnsupported(server_client_t *client, ipp_attribute_t *attr);
extern void		serverRun(void);
extern char		*serverTimeString(time_t tv, char *buffer, size_t bufsize);
extern int		serverTransformJob(server_client_t *client, server_job_t *job, const char *command, const char *format, server_transform_t mode);
extern void		serverUpdateDeviceAttributesNoLock(server_printer_t *printer);
extern void		serverUpdateDeviceStateNoLock(server_printer_t *printer);

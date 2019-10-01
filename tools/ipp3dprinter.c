/*
 * IPP 3D printer application.
 *
 * Copyright © 2010-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.º
 *
 * Note: This program began life as the "ippserver" sample code that first
 * appeared in CUPS 1.4.  The name has been changed in order to distinguish it
 * from the PWG's much more ambitious "ippserver" program, which supports
 * different kinds of IPP services and multiple services per instance - the
 * "ipp3dprinter" program exposes a single print service conforming to the
 * current IPP 3D Printing Extensions specification, thus the new name.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups-private.h>
#include <cups/debug-private.h>

#include <limits.h>
#include <sys/stat.h>

#ifdef _WIN32
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
#endif /* _WIN32 */

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

#include <server/printer3d-png.h>


/*
 * Constants...
 */

enum ipp3d_preason_e			/* printer-state-reasons bit values */
{
  IPP3D_PREASON_NONE = 0x0000,		/* none */
  IPP3D_PREASON_OTHER = 0x0001,		/* other */
  IPP3D_PREASON_MOVING_TO_PAUSED = 0x0002,
					/* moving-to-paused */
  IPP3D_PREASON_PAUSED = 0x0004,	/* paused */
  IPP3D_PREASON_SPOOL_AREA_FULL = 0x0008,
					/* spool-area-full */
  IPP3D_PREASON_CHAMBER_HEATING = 0x0010,
					/* chamber-heating */
  IPP3D_PREASON_COVER_OPEN = 0x0020,	/* cover-open */
  IPP3D_PREASON_EXTRUDER_HEATING = 0x0040,
					/* extruder-heating */
  IPP3D_PREASON_FAN_FAILURE = 0x0080,	/* fan-failure */
  IPP3D_PREASON_MATERIAL_EMPTY = 0x0100,/* material-empty */
  IPP3D_PREASON_MATERIAL_LOW = 0x0200,	/* material-low */
  IPP3D_PREASON_MATERIAL_NEEDED = 0x0400,
					/* material-needed */
  IPP3D_PREASON_MOTOR_FAILURE = 0x0800,	/* motor-failure */
  IPP3D_PREASON_PLATFORM_HEATING = 0x1000,
					/* platform-heating */
};
typedef unsigned int ipp3d_preason_t;	/* Bitfield for printer-state-reasons */
static const char * const ipp3d_preason_strings[] =
{					/* Strings for each bit */
  /* "none" is implied for no bits set */
  "other",
  "moving-to-paused",
  "paused",
  "spool-area-full",
  "chamber-heating",
  "cover-open",
  "extruder-heating",
  "fan-failure",
  "material-empty",
  "material-low",
  "material-needed",
  "motor-failure",
  "platform-heating"
};


/*
 * URL scheme for web resources...
 */

#ifdef HAVE_SSL
#  define WEB_SCHEME "https"
#else
#  define WEB_SCHEME "http"
#endif /* HAVE_SSL */


/*
 * Structures...
 */

#ifdef HAVE_DNSSD
typedef DNSServiceRef ipp3d_srv_t;	/* Service reference */
typedef TXTRecordRef ipp3d_txt_t;	/* TXT record */

#elif defined(HAVE_AVAHI)
typedef AvahiEntryGroup *ipp3d_srv_t;	/* Service reference */
typedef AvahiStringList *ipp3d_txt_t;	/* TXT record */

#else
typedef void *ipp3d_srv_t;		/* Service reference */
typedef void *ipp3d_txt_t;		/* TXT record */
#endif /* HAVE_DNSSD */

typedef struct ipp3d_filter_s		/**** Attribute filter ****/
{
  cups_array_t		*ra;		/* Requested attributes */
  ipp_tag_t		group_tag;	/* Group to copy */
} ipp3d_filter_t;

typedef struct ipp3d_job_s ipp3d_job_t;

typedef struct ipp3d_printer_s		/**** Printer data ****/
{
  /* TODO: One IPv4 and one IPv6 listener are really not sufficient */
  int			ipv4,		/* IPv4 listener */
			ipv6;		/* IPv6 listener */
  ipp3d_srv_t		ipp_ref,	/* Bonjour IPP service */
			ipps_ref,	/* Bonjour IPPS service */
			http_ref,	/* Bonjour HTTP service */
			printer_ref;	/* Bonjour LPD service */
  char			*dns_sd_name,	/* printer-dnssd-name */
			*name,		/* printer-name */
			*icon,		/* Icon filename */
			*directory,	/* Spool directory */
			*hostname,	/* Hostname */
			*uri,		/* printer-uri-supported */
			*device_uri,	/* Device URI (if any) */
			*command;	/* Command to run with job file */
  int			port;		/* Port */
  int			web_forms;	/* Enable web interface forms? */
  size_t		urilen;		/* Length of printer URI */
  ipp_t			*attrs;		/* Static attributes */
  time_t		start_time;	/* Startup time */
  time_t		config_time;	/* printer-config-change-time */
  ipp_pstate_t		state;		/* printer-state value */
  ipp3d_preason_t	state_reasons;	/* printer-state-reasons values */
  time_t		state_time;	/* printer-state-change-time */
  cups_array_t		*jobs;		/* Jobs */
  ipp3d_job_t		*active_job;	/* Current active/pending job */
  int			next_job_id;	/* Next job-id value */
  _cups_rwlock_t	rwlock;		/* Printer lock */
} ipp3d_printer_t;

struct ipp3d_job_s			/**** Job data ****/
{
  int			id;		/* Job ID */
  const char		*name,		/* job-name */
			*username,	/* job-originating-user-name */
			*format;	/* document-format */
  ipp_jstate_t		state;		/* job-state value */
  char			*message;	/* job-state-message value */
  int			msglevel;	/* job-state-message log level (0=error, 1=info) */
  time_t		created,	/* time-at-creation value */
			processing,	/* time-at-processing value */
			completed;	/* time-at-completed value */
  int			impressions,	/* job-impressions value */
			impcompleted;	/* job-impressions-completed value */
  ipp_t			*attrs;		/* Static attributes */
  int			cancel;		/* Non-zero when job canceled */
  char			*filename;	/* Print file name */
  int			fd;		/* Print file descriptor */
  ipp3d_printer_t	*printer;	/* Printer */
};

typedef struct ipp3d_client_s		/**** Client data ****/
{
  http_t		*http;		/* HTTP connection */
  ipp_t			*request,	/* IPP request */
			*response;	/* IPP response */
  time_t		start;		/* Request start time */
  http_state_t		operation;	/* Request operation */
  ipp_op_t		operation_id;	/* IPP operation-id */
  char			uri[1024],	/* Request URI */
			*options;	/* URI options */
  http_addr_t		addr;		/* Client address */
  char			hostname[256];	/* Client hostname */
  ipp3d_printer_t	*printer;	/* Printer */
  ipp3d_job_t		*job;		/* Current job, if any */
} ipp3d_client_t;


/*
 * Local functions...
 */

static void		clean_jobs(ipp3d_printer_t *printer);
static int		compare_jobs(ipp3d_job_t *a, ipp3d_job_t *b);
static void		copy_attributes(ipp_t *to, ipp_t *from, cups_array_t *ra, ipp_tag_t group_tag, int quickcopy);
static void		copy_job_attributes(ipp3d_client_t *client, ipp3d_job_t *job, cups_array_t *ra);
static ipp3d_client_t	*create_client(ipp3d_printer_t *printer, int sock);
static ipp3d_job_t	*create_job(ipp3d_client_t *client);
static int		create_job_file(ipp3d_job_t *job, char *fname, size_t fnamesize, const char *dir, const char *ext);
static int		create_listener(const char *name, int port, int family);
static ipp3d_printer_t	*create_printer(const char *servername, int serverport, const char *name, const char *location, const char *icon, cups_array_t *docformats, const char *subtypes, const char *directory, const char *command, const char *device_uri, ipp_t *attrs);
static void		debug_attributes(const char *title, ipp_t *ipp, int response);
static void		delete_client(ipp3d_client_t *client);
static void		delete_job(ipp3d_job_t *job);
static void		delete_printer(ipp3d_printer_t *printer);
#ifdef HAVE_DNSSD
static void DNSSD_API	dnssd_callback(DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode, const char *name, const char *regtype, const char *domain, ipp3d_printer_t *printer);
#elif defined(HAVE_AVAHI)
static void		dnssd_callback(AvahiEntryGroup *p, AvahiEntryGroupState state, void *context);
static void		dnssd_client_cb(AvahiClient *c, AvahiClientState state, void *userdata);
#endif /* HAVE_DNSSD */
static void		dnssd_init(void);
static int		filter_cb(ipp3d_filter_t *filter, ipp_t *dst, ipp_attribute_t *attr);
static ipp3d_job_t	*find_job(ipp3d_client_t *client);
static void		finish_document_data(ipp3d_client_t *client, ipp3d_job_t *job);
static void		finish_document_uri(ipp3d_client_t *client, ipp3d_job_t *job);
static void		html_escape(ipp3d_client_t *client, const char *s, size_t slen);
static void		html_footer(ipp3d_client_t *client);
static void		html_header(ipp3d_client_t *client, const char *title, int refresh);
static void		html_printf(ipp3d_client_t *client, const char *format, ...) _CUPS_FORMAT(2, 3);
static void		ipp_cancel_job(ipp3d_client_t *client);
static void		ipp_close_job(ipp3d_client_t *client);
static void		ipp_create_job(ipp3d_client_t *client);
static void		ipp_get_job_attributes(ipp3d_client_t *client);
static void		ipp_get_jobs(ipp3d_client_t *client);
static void		ipp_get_printer_attributes(ipp3d_client_t *client);
static void		ipp_identify_printer(ipp3d_client_t *client);
static void		ipp_send_document(ipp3d_client_t *client);
static void		ipp_send_uri(ipp3d_client_t *client);
static void		ipp_validate_job(ipp3d_client_t *client);
static ipp_t		*load_ippserver_attributes(const char *servername, int serverport, const char *filename, cups_array_t *docformats);
static int		parse_options(ipp3d_client_t *client, cups_option_t **options);
static void		process_attr_message(ipp3d_job_t *job, char *message);
static void		*process_client(ipp3d_client_t *client);
static int		process_http(ipp3d_client_t *client);
static int		process_ipp(ipp3d_client_t *client);
static void		*process_job(ipp3d_job_t *job);
static void		process_state_message(ipp3d_job_t *job, char *message);
static int		register_printer(ipp3d_printer_t *printer, const char *subtypes);
static int		respond_http(ipp3d_client_t *client, http_status_t code, const char *content_coding, const char *type, size_t length);
static void		respond_ipp(ipp3d_client_t *client, ipp_status_t status, const char *message, ...) _CUPS_FORMAT(3, 4);
static void		respond_unsupported(ipp3d_client_t *client, ipp_attribute_t *attr);
static void		run_printer(ipp3d_printer_t *printer);
static int		show_materials(ipp3d_client_t *client);
static int		show_status(ipp3d_client_t *client);
static char		*time_string(time_t tv, char *buffer, size_t bufsize);
static void		usage(int status) _CUPS_NORETURN;
static int		valid_doc_attributes(ipp3d_client_t *client);
static int		valid_job_attributes(ipp3d_client_t *client);


/*
 * Globals...
 */

#ifdef HAVE_DNSSD
static DNSServiceRef	DNSSDMaster = NULL;
#elif defined(HAVE_AVAHI)
static AvahiThreadedPoll *DNSSDMaster = NULL;
static AvahiClient	*DNSSDClient = NULL;
#endif /* HAVE_DNSSD */

static int		KeepFiles = 0,	/* Keep spooled job files? */
			MaxVersion = 20,/* Maximum IPP version (20 = 2.0, 11 = 1.1, etc.) */
			Verbosity = 0;	/* Verbosity level */


/*
 * 'main()' - Main entry to the sample server.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  const char	*opt,			/* Current option character */
		*attrfile = NULL,	/* ippserver attributes file */
		*command = NULL,	/* Command to run with job files */
		*device_uri = NULL,	/* Device URI */
		*icon = NULL,		/* Icon file */
#ifdef HAVE_SSL
		*keypath = NULL,	/* Keychain path */
#endif /* HAVE_SSL */
		*location = "",		/* Location of printer */
		*make = "Example",	/* Manufacturer */
		*model = "Printer",	/* Model */
		*name = NULL,		/* Printer name */
		*subtypes = "_print";	/* DNS-SD service subtype */
  int		web_forms = 1;		/* Enable web site forms? */
  ipp_t		*attrs = NULL;		/* Printer attributes */
  char		directory[1024] = "";	/* Spool directory */
  cups_array_t	*docformats = NULL;	/* Supported formats */
  const char	*servername = NULL;	/* Server host name */
  int		serverport = 0;		/* Server port number (0 = auto) */
  ipp3d_printer_t *printer;		/* Printer object */


 /*
  * Parse command-line arguments...
  */

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      usage(0);
    }
    else if (!strcmp(argv[i], "--no-web-forms"))
    {
      web_forms = 0;
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(CUPS_SVERSION);
      return (0);
    }
    else if (!strncmp(argv[i], "--", 2))
    {
      _cupsLangPrintf(stderr, _("%s: Unknown option \"%s\"."), argv[0], argv[i]);
      usage(1);
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
          case 'D' : /* -D device-uri */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      device_uri = argv[i];
	      break;

#ifdef HAVE_SSL
	  case 'K' : /* -K keypath */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      keypath = argv[i];
	      break;
#endif /* HAVE_SSL */

	  case 'M' : /* -M manufacturer */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      make = argv[i];
	      break;

	  case 'a' : /* -a attributes-file */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      attrfile = argv[i];
	      break;

          case 'c' : /* -c command */
              i ++;
	      if (i >= argc)
	        usage(1);

	      command = argv[i];
	      break;

	  case 'd' : /* -d spool-directory */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      strlcpy(directory, argv[i], sizeof(directory));
	      break;

	  case 'f' : /* -f type/subtype[,...] */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      docformats = _cupsArrayNewStrings(argv[i], ',');
	      break;

	  case 'i' : /* -i icon.png */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      icon = argv[i];
	      break;

	  case 'k' : /* -k (keep files) */
	      KeepFiles = 1;
	      break;

	  case 'l' : /* -l location */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      location = argv[i];
	      break;

	  case 'm' : /* -m model */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      model = argv[i];
	      break;

	  case 'n' : /* -n hostname */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      servername = argv[i];
	      break;

	  case 'p' : /* -p port */
	      i ++;
	      if (i >= argc || !isdigit(argv[i][0] & 255))
	        usage(1);

	      serverport = atoi(argv[i]);
	      break;

	  case 'r' : /* -r subtype */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      subtypes = argv[i];
	      break;

	  case 'v' : /* -v (be verbose) */
	      Verbosity ++;
	      break;

          default : /* Unknown */
	      _cupsLangPrintf(stderr, _("%s: Unknown option \"-%c\"."), argv[0], *opt);
	      usage(1);
	}
      }
    }
    else if (!name)
    {
      name = argv[i];
    }
    else
    {
      _cupsLangPrintf(stderr, _("%s: Unknown option \"%s\"."), argv[0], argv[i]);
      usage(1);
    }
  }

  if (!name)
    usage(1);

 /*
  * Apply defaults as needed...
  */

  if (!serverport)
  {
#ifdef _WIN32
   /*
    * Windows is almost always used as a single user system, so use a default
    * port number of 8631.
    */

    serverport = 8631;

#else
   /*
    * Use 8000 + UID mod 1000 for the default port number...
    */

    serverport = 8000 + ((int)getuid() % 1000);
#endif /* _WIN32 */

    _cupsLangPrintf(stderr, _("Listening on port %d."), serverport);
  }

  if (!directory[0])
  {
    const char *tmpdir;			/* Temporary directory */

#ifdef _WIN32
    if ((tmpdir = getenv("TEMP")) == NULL)
      tmpdir = "C:/TEMP";
#elif defined(__APPLE__) && TARGET_OS_OSX
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/private/tmp";
#else
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/tmp";
#endif /* _WIN32 */

    snprintf(directory, sizeof(directory), "%s/ipp3dprinter.%d", tmpdir, (int)getpid());

    if (mkdir(directory, 0755) && errno != EEXIST)
    {
      _cupsLangPrintf(stderr, _("Unable to create spool directory \"%s\": %s"), directory, strerror(errno));
      usage(1);
    }

    if (Verbosity)
      _cupsLangPrintf(stderr, _("Using spool directory \"%s\"."), directory);
  }

 /*
  * Initialize DNS-SD...
  */

  dnssd_init();

 /*
  * Create the printer...
  */

  if (!docformats)
    docformats = _cupsArrayNewStrings("application/vnd.pwg-safe-gcode", ',');

  if (attrfile)
    attrs = load_ippserver_attributes(servername, serverport, attrfile, docformats);

  if ((printer = create_printer(servername, serverport, name, location, icon, docformats, subtypes, directory, command, device_uri, attrs)) == NULL)
    return (1);

  printer->web_forms = web_forms;

#ifdef HAVE_SSL
  cupsSetServerCredentials(keypath, printer->hostname, 1);
#endif /* HAVE_SSL */

 /*
  * Run the print service...
  */

  run_printer(printer);

 /*
  * Destroy the printer and exit...
  */

  delete_printer(printer);

  return (0);
}


/*
 * 'clean_jobs()' - Clean out old (completed) jobs.
 */

static void
clean_jobs(ipp3d_printer_t *printer)	/* I - Printer */
{
  ipp3d_job_t	*job;			/* Current job */
  time_t	cleantime;		/* Clean time */


  if (cupsArrayCount(printer->jobs) == 0)
    return;

  cleantime = time(NULL) - 60;

  _cupsRWLockWrite(&(printer->rwlock));
  for (job = (ipp3d_job_t *)cupsArrayFirst(printer->jobs);
       job;
       job = (ipp3d_job_t *)cupsArrayNext(printer->jobs))
    if (job->completed && job->completed < cleantime)
    {
      cupsArrayRemove(printer->jobs, job);
      delete_job(job);
    }
    else
      break;
  _cupsRWUnlock(&(printer->rwlock));
}


/*
 * 'compare_jobs()' - Compare two jobs.
 */

static int				/* O - Result of comparison */
compare_jobs(ipp3d_job_t *a,		/* I - First job */
             ipp3d_job_t *b)		/* I - Second job */
{
  return (b->id - a->id);
}


/*
 * 'copy_attributes()' - Copy attributes from one request to another.
 */

static void
copy_attributes(ipp_t        *to,	/* I - Destination request */
	        ipp_t        *from,	/* I - Source request */
	        cups_array_t *ra,	/* I - Requested attributes */
	        ipp_tag_t    group_tag,	/* I - Group to copy */
	        int          quickcopy)	/* I - Do a quick copy? */
{
  ipp3d_filter_t	filter;			/* Filter data */


  filter.ra        = ra;
  filter.group_tag = group_tag;

  ippCopyAttributes(to, from, quickcopy, (ipp_copycb_t)filter_cb, &filter);
}


/*
 * 'copy_job_attrs()' - Copy job attributes to the response.
 */

static void
copy_job_attributes(
    ipp3d_client_t *client,		/* I - Client */
    ipp3d_job_t    *job,			/* I - Job */
    cups_array_t  *ra)			/* I - requested-attributes */
{
  copy_attributes(client->response, job->attrs, ra, IPP_TAG_JOB, 0);

  if (!ra || cupsArrayFind(ra, "date-time-at-completed"))
  {
    if (job->completed)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-completed", ippTimeToDate(job->completed));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-completed");
  }

  if (!ra || cupsArrayFind(ra, "date-time-at-processing"))
  {
    if (job->processing)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-processing", ippTimeToDate(job->processing));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-processing");
  }

  if (!ra || cupsArrayFind(ra, "job-impressions"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions", job->impressions);

  if (!ra || cupsArrayFind(ra, "job-impressions-completed"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions-completed", job->impcompleted);

  if (!ra || cupsArrayFind(ra, "job-printer-up-time"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-printer-up-time", (int)(time(NULL) - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "job-state"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", (int)job->state);

  if (!ra || cupsArrayFind(ra, "job-state-message"))
  {
    if (job->message)
    {
      ippAddString(client->response, IPP_TAG_JOB, IPP_TAG_TEXT, "job-state-message", NULL, job->message);
    }
    else
    {
      switch (job->state)
      {
	case IPP_JSTATE_PENDING :
	    ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job pending.");
	    break;

	case IPP_JSTATE_HELD :
	    if (job->fd >= 0)
	      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job incoming.");
	    else if (ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO))
	      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job held.");
	    else
	      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job created.");
	    break;

	case IPP_JSTATE_PROCESSING :
	    if (job->cancel)
	      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job canceling.");
	    else
	      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job printing.");
	    break;

	case IPP_JSTATE_STOPPED :
	    ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job stopped.");
	    break;

	case IPP_JSTATE_CANCELED :
	    ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job canceled.");
	    break;

	case IPP_JSTATE_ABORTED :
	    ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job aborted.");
	    break;

	case IPP_JSTATE_COMPLETED :
	    ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, "Job completed.");
	    break;
      }
    }
  }

  if (!ra || cupsArrayFind(ra, "job-state-reasons"))
  {
    switch (job->state)
    {
      case IPP_JSTATE_PENDING :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "none");
	  break;

      case IPP_JSTATE_HELD :
          if (job->fd >= 0)
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-incoming");
	  else if (ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO))
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-hold-until-specified");
          else
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-data-insufficient");
	  break;

      case IPP_JSTATE_PROCESSING :
	  if (job->cancel)
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "processing-to-stop-point");
	  else
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-printing");
	  break;

      case IPP_JSTATE_STOPPED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-stopped");
	  break;

      case IPP_JSTATE_CANCELED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-canceled-by-user");
	  break;

      case IPP_JSTATE_ABORTED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "aborted-by-system");
	  break;

      case IPP_JSTATE_COMPLETED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-completed-successfully");
	  break;
    }
  }

  if (!ra || cupsArrayFind(ra, "time-at-completed"))
    ippAddInteger(client->response, IPP_TAG_JOB,
                  job->completed ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE,
                  "time-at-completed", (int)(job->completed - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "time-at-processing"))
    ippAddInteger(client->response, IPP_TAG_JOB,
                  job->processing ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE,
                  "time-at-processing", (int)(job->processing - client->printer->start_time));
}


/*
 * 'create_client()' - Accept a new network connection and create a client
 *                     object.
 */

static ipp3d_client_t *			/* O - Client */
create_client(ipp3d_printer_t *printer,	/* I - Printer */
              int            sock)	/* I - Listen socket */
{
  ipp3d_client_t	*client;		/* Client */


  if ((client = calloc(1, sizeof(ipp3d_client_t))) == NULL)
  {
    perror("Unable to allocate memory for client");
    return (NULL);
  }

  client->printer = printer;

 /*
  * Accept the client and get the remote address...
  */

  if ((client->http = httpAcceptConnection(sock, 1)) == NULL)
  {
    perror("Unable to accept client connection");

    free(client);

    return (NULL);
  }

  httpGetHostname(client->http, client->hostname, sizeof(client->hostname));

  if (Verbosity)
    fprintf(stderr, "Accepted connection from %s\n", client->hostname);

  return (client);
}


/*
 * 'create_job()' - Create a new job object from a Print-Job or Create-Job
 *                  request.
 */

static ipp3d_job_t *			/* O - Job */
create_job(ipp3d_client_t *client)	/* I - Client */
{
  ipp3d_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Job attribute */
  char			uri[1024],	/* job-uri value */
			uuid[64];	/* job-uuid value */


  _cupsRWLockWrite(&(client->printer->rwlock));
  if (client->printer->active_job &&
      client->printer->active_job->state < IPP_JSTATE_CANCELED)
  {
   /*
    * Only accept a single job at a time...
    */

    _cupsRWUnlock(&(client->printer->rwlock));
    return (NULL);
  }

 /*
  * Allocate and initialize the job object...
  */

  if ((job = calloc(1, sizeof(ipp3d_job_t))) == NULL)
  {
    perror("Unable to allocate memory for job");
    return (NULL);
  }

  job->printer    = client->printer;
  job->attrs      = ippNew();
  job->state      = IPP_JSTATE_HELD;
  job->fd         = -1;

 /*
  * Copy all of the job attributes...
  */

  copy_attributes(job->attrs, client->request, NULL, IPP_TAG_JOB, 0);

 /*
  * Get the requesting-user-name, document format, and priority...
  */

  if ((attr = ippFindAttribute(client->request, "requesting-user-name", IPP_TAG_NAME)) != NULL)
    job->username = ippGetString(attr, 0, NULL);
  else
    job->username = "anonymous";

  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-user-name", NULL, job->username);

  if (ippGetOperation(client->request) != IPP_OP_CREATE_JOB)
  {
    if ((attr = ippFindAttribute(job->attrs, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL)
      job->format = ippGetString(attr, 0, NULL);
    else if ((attr = ippFindAttribute(job->attrs, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL)
      job->format = ippGetString(attr, 0, NULL);
    else
      job->format = "application/octet-stream";
  }

  if ((attr = ippFindAttribute(client->request, "job-impressions", IPP_TAG_INTEGER)) != NULL)
    job->impressions = ippGetInteger(attr, 0);

  if ((attr = ippFindAttribute(client->request, "job-name", IPP_TAG_NAME)) != NULL)
    job->name = ippGetString(attr, 0, NULL);

 /*
  * Add job description attributes and add to the jobs array...
  */

  job->id = client->printer->next_job_id ++;

  snprintf(uri, sizeof(uri), "%s/%d", client->printer->uri, job->id);
  httpAssembleUUID(client->printer->hostname, client->printer->port, client->printer->name, job->id, uuid, sizeof(uuid));

  ippAddDate(job->attrs, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(time(&job->created)));
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uuid", NULL, uuid);
  if ((attr = ippFindAttribute(client->request, "printer-uri", IPP_TAG_URI)) != NULL)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, ippGetString(attr, 0, NULL));
  else
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, client->printer->uri);
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", (int)(job->created - client->printer->start_time));

  cupsArrayAdd(client->printer->jobs, job);
  client->printer->active_job = job;

  _cupsRWUnlock(&(client->printer->rwlock));

  return (job);
}


/*
 * 'create_job_file()' - Create a file for the document in a job.
 */

static int				/* O - File descriptor or -1 on error */
create_job_file(
    ipp3d_job_t     *job,		/* I - Job */
    char             *fname,		/* I - Filename buffer */
    size_t           fnamesize,		/* I - Size of filename buffer */
    const char       *directory,	/* I - Directory to store in */
    const char       *ext)		/* I - Extension (`NULL` for default) */
{
  char			name[256],	/* "Safe" filename */
			*nameptr;	/* Pointer into filename */
  const char		*job_name;	/* job-name value */


 /*
  * Make a name from the job-name attribute...
  */

  if ((job_name = ippGetString(ippFindAttribute(job->attrs, "job-name", IPP_TAG_NAME), 0, NULL)) == NULL)
    job_name = "untitled";

  for (nameptr = name; *job_name && nameptr < (name + sizeof(name) - 1); job_name ++)
  {
    if (isalnum(*job_name & 255) || *job_name == '-')
    {
      *nameptr++ = (char)tolower(*job_name & 255);
    }
    else
    {
      *nameptr++ = '_';

      while (job_name[1] && !isalnum(job_name[1] & 255) && job_name[1] != '-')
        job_name ++;
    }
  }

  *nameptr = '\0';

 /*
  * Figure out the extension...
  */

  if (!ext)
  {
    if (!strcasecmp(job->format, "image/jpeg"))
      ext = "jpg";
    else if (!strcasecmp(job->format, "image/png"))
      ext = "png";
    else if (!strcasecmp(job->format, "image/pwg-raster"))
      ext = "pwg";
    else if (!strcasecmp(job->format, "image/urf"))
      ext = "urf";
    else if (!strcasecmp(job->format, "application/pdf"))
      ext = "pdf";
    else if (!strcasecmp(job->format, "application/postscript"))
      ext = "ps";
    else if (!strcasecmp(job->format, "application/vnd.hp-pcl"))
      ext = "pcl";
    else
      ext = "dat";
  }

 /*
  * Create a filename with the job-id, job-name, and document-format (extension)...
  */

  snprintf(fname, fnamesize, "%s/%d-%s.%s", directory, job->id, name, ext);

  return (open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666));
}


/*
 * 'create_listener()' - Create a listener socket.
 */

static int				/* O - Listener socket or -1 on error */
create_listener(const char *name,	/* I - Host name (`NULL` for any address) */
                int        port,	/* I - Port number */
                int        family)	/* I - Address family */
{
  int			sock;		/* Listener socket */
  http_addrlist_t	*addrlist;	/* Listen address */
  char			service[255];	/* Service port */


  snprintf(service, sizeof(service), "%d", port);
  if ((addrlist = httpAddrGetList(name, family, service)) == NULL)
    return (-1);

  sock = httpAddrListen(&(addrlist->addr), port);

  httpAddrFreeList(addrlist);

  return (sock);
}


/*
 * 'create_printer()' - Create, register, and listen for connections to a
 *                      printer object.
 */

static ipp3d_printer_t *		/* O - Printer */
create_printer(
    const char   *servername,		/* I - Server hostname (NULL for default) */
    int          serverport,		/* I - Server port */
    const char   *name,			/* I - printer-name */
    const char   *location,		/* I - printer-location */
    const char   *icon,			/* I - printer-icons */
    cups_array_t *docformats,		/* I - document-format-supported */
    const char   *subtypes,		/* I - Bonjour service subtype(s) */
    const char   *directory,		/* I - Spool directory */
    const char   *command,		/* I - Command to run on job files, if any */
    const char   *device_uri,		/* I - Output device, if any */
    ipp_t        *attrs)		/* I - Capability attributes */
{
  ipp3d_printer_t	*printer;	/* Printer */
  int			i;		/* Looping var */
#ifndef _WIN32
  char			path[1024];	/* Full path to command */
#endif /* !_WIN32 */
  char			uri[1024],	/* Printer URI */
#ifdef HAVE_SSL
			securi[1024],	/* Secure printer URI */
			*uris[2],	/* All URIs */
#endif /* HAVE_SSL */
			icons[1024],	/* printer-icons URI */
			adminurl[1024],	/* printer-more-info URI */
			uuid[128];	/* printer-uuid */
  int			k_supported;	/* Maximum file size supported */
  int			num_formats;	/* Number of supported document formats */
  const char		*formats[100],	/* Supported document formats */
			*format;	/* Current format */
  int			num_sup_attrs;	/* Number of supported attributes */
  const char		*sup_attrs[100];/* Supported attributes */
  char			xxx_supported[256];
					/* Name of -supported attribute */
  _cups_globals_t	*cg = _cupsGlobals();
					/* Global path values */
#ifdef HAVE_STATVFS
  struct statvfs	spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#elif defined(HAVE_STATFS)
  struct statfs		spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#endif /* HAVE_STATVFS */
  static const char * const versions[] =/* ipp-versions-supported values */
  {
    "1.1",
    "2.0"
  };
  static const char * const features[] =/* ipp-features-supported values */
  {
    "ipp-3d"
  };
  static const int	ops[] =		/* operations-supported values */
  {
    IPP_OP_VALIDATE_JOB,
    IPP_OP_CREATE_JOB,
    IPP_OP_SEND_DOCUMENT,
    IPP_OP_SEND_URI,
    IPP_OP_CANCEL_JOB,
    IPP_OP_GET_JOB_ATTRIBUTES,
    IPP_OP_GET_JOBS,
    IPP_OP_GET_PRINTER_ATTRIBUTES,
    IPP_OP_CANCEL_MY_JOBS,
    IPP_OP_CLOSE_JOB,
    IPP_OP_IDENTIFY_PRINTER
  };
  static const char * const charsets[] =/* charset-supported values */
  {
    "us-ascii",
    "utf-8"
  };
  static const char * const compressions[] =/* compression-supported values */
  {
#ifdef HAVE_LIBZ
    "deflate",
    "gzip",
#endif /* HAVE_LIBZ */
    "none"
  };
  static const char * const identify_actions[] =
  {
    "display",
    "sound"
  };
  static const char * const job_creation[] =
  {					/* job-creation-attributes-supported values */
    "copies",
    "document-access",
    "document-charset",
    "document-format",
    "document-message",
    "document-metadata",
    "document-name",
    "document-natural-language",
    "document-password",
    "finishings",
    "finishings-col",
    "ipp-attribute-fidelity",
    "job-account-id",
    "job-account-type",
    "job-accouunting-sheets",
    "job-accounting-user-id",
    "job-authorization-uri",
    "job-error-action",
    "job-error-sheet",
    "job-hold-until",
    "job-hold-until-time",
    "job-mandatory-attributes",
    "job-message-to-operator",
    "job-name",
    "job-pages-per-set",
    "job-password",
    "job-password-encryption",
    "job-phone-number",
    "job-priority",
    "job-recipient-name",
    "job-resource-ids",
    "job-sheet-message",
    "job-sheets",
    "job-sheets-col",
    "media",
    "media-col",
    "multiple-document-handling",
    "number-up",
    "orientation-requested",
    "output-bin",
    "output-device",
    "overrides",
    "page-delivery",
    "page-ranges",
    "presentation-direction-number-up",
    "print-color-mode",
    "print-content-optimize",
    "print-quality",
    "print-rendering-intent",
    "print-scaling",
    "printer-resolution",
    "proof-print",
    "separator-sheets",
    "sides",
    "x-image-position",
    "x-image-shift",
    "x-side1-image-shift",
    "x-side2-image-shift",
    "y-image-position",
    "y-image-shift",
    "y-side1-image-shift",
    "y-side2-image-shift"
  };
  static const char * const media_col_supported[] =
  {					/* media-col-supported values */
    "media-bottom-margin",
    "media-left-margin",
    "media-right-margin",
    "media-size",
    "media-size-name",
    "media-source",
    "media-top-margin",
    "media-type"
  };
  static const char * const multiple_document_handling[] =
  {					/* multiple-document-handling-supported values */
    "separate-documents-uncollated-copies",
    "separate-documents-collated-copies"
  };
  static const char * const reference_uri_schemes_supported[] =
  {					/* reference-uri-schemes-supported */
    "file",
    "ftp",
    "http"
#ifdef HAVE_SSL
    , "https"
#endif /* HAVE_SSL */
  };
#ifdef HAVE_SSL
  static const char * const uri_authentication_supported[] =
  {					/* uri-authentication-supported values */
    "none",
    "none"
  };
  static const char * const uri_security_supported[] =
  {					/* uri-security-supported values */
    "none",
    "tls"
  };
#endif /* HAVE_SSL */
  static const char * const which_jobs[] =
  {					/* which-jobs-supported values */
    "completed",
    "not-completed",
    "aborted",
    "all",
    "canceled",
    "pending",
    "pending-held",
    "processing",
    "processing-stopped"
  };


#ifndef _WIN32
 /*
  * If a command was specified, make sure it exists and is executable...
  */

  if (command)
  {
    if (*command == '/' || !strncmp(command, "./", 2))
    {
      if (access(command, X_OK))
      {
        _cupsLangPrintf(stderr, _("Unable to execute command \"%s\": %s"), command, strerror(errno));
	return (NULL);
      }
    }
    else
    {
      snprintf(path, sizeof(path), "%s/command/%s", cg->cups_serverbin, command);

      if (access(command, X_OK))
      {
        _cupsLangPrintf(stderr, _("Unable to execute command \"%s\": %s"), command, strerror(errno));
	return (NULL);
      }

      command = path;
    }
  }
#endif /* !_WIN32 */

 /*
  * Allocate memory for the printer...
  */

  if ((printer = calloc(1, sizeof(ipp3d_printer_t))) == NULL)
  {
    _cupsLangPrintError(NULL, _("Unable to allocate memory for printer"));
    return (NULL);
  }

  printer->ipv4          = -1;
  printer->ipv6          = -1;
  printer->name          = strdup(name);
  printer->dns_sd_name    = strdup(name);
  printer->command       = command ? strdup(command) : NULL;
  printer->device_uri    = device_uri ? strdup(device_uri) : NULL;
  printer->directory     = strdup(directory);
  printer->icon          = icon ? strdup(icon) : NULL;
  printer->port          = serverport;
  printer->start_time    = time(NULL);
  printer->config_time   = printer->start_time;
  printer->state         = IPP_PSTATE_IDLE;
  printer->state_reasons = IPP3D_PREASON_NONE;
  printer->state_time    = printer->start_time;
  printer->jobs          = cupsArrayNew((cups_array_func_t)compare_jobs, NULL);
  printer->next_job_id   = 1;

  if (servername)
  {
    printer->hostname = strdup(servername);
  }
  else
  {
    char	temp[1024];		/* Temporary string */

    printer->hostname = strdup(httpGetHostname(NULL, temp, sizeof(temp)));
  }

  _cupsRWInit(&(printer->rwlock));

 /*
  * Create the listener sockets...
  */

  if ((printer->ipv4 = create_listener(servername, printer->port, AF_INET)) < 0)
  {
    perror("Unable to create IPv4 listener");
    goto bad_printer;
  }

  if ((printer->ipv6 = create_listener(servername, printer->port, AF_INET6)) < 0)
  {
    perror("Unable to create IPv6 listener");
    goto bad_printer;
  }

 /*
  * Prepare URI values for the printer attributes...
  */

  httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL, printer->hostname, printer->port, "/ipp/print3d");
  printer->uri    = strdup(uri);
  printer->urilen = strlen(uri);

#ifdef HAVE_SSL
  httpAssembleURI(HTTP_URI_CODING_ALL, securi, sizeof(securi), "ipps", NULL, printer->hostname, printer->port, "/ipp/print3d");
#endif /* HAVE_SSL */

  httpAssembleURI(HTTP_URI_CODING_ALL, icons, sizeof(icons), WEB_SCHEME, NULL, printer->hostname, printer->port, "/icon.png");
  httpAssembleURI(HTTP_URI_CODING_ALL, adminurl, sizeof(adminurl), WEB_SCHEME, NULL, printer->hostname, printer->port, "/");
  httpAssembleUUID(printer->hostname, serverport, name, 0, uuid, sizeof(uuid));

  if (Verbosity)
  {
    fprintf(stderr, "printer-more-info=\"%s\"\n", adminurl);
#ifdef HAVE_SSL
    fprintf(stderr, "printer-uri=\"%s\",\"%s\"\n", uri, securi);
#else
    fprintf(stderr, "printer-uri=\"%s\"\n", uri);
#endif /* HAVE_SSL */
  }

 /*
  * Get the maximum spool size based on the size of the filesystem used for
  * the spool directory.  If the host OS doesn't support the statfs call
  * or the filesystem is larger than 2TiB, always report INT_MAX.
  */

#ifdef HAVE_STATVFS
  if (statvfs(printer->directory, &spoolinfo))
    k_supported = INT_MAX;
  else if ((spoolsize = (double)spoolinfo.f_frsize *
                        spoolinfo.f_blocks / 1024) > INT_MAX)
    k_supported = INT_MAX;
  else
    k_supported = (int)spoolsize;

#elif defined(HAVE_STATFS)
  if (statfs(printer->directory, &spoolinfo))
    k_supported = INT_MAX;
  else if ((spoolsize = (double)spoolinfo.f_bsize *
                        spoolinfo.f_blocks / 1024) > INT_MAX)
    k_supported = INT_MAX;
  else
    k_supported = (int)spoolsize;

#else
  k_supported = INT_MAX;
#endif /* HAVE_STATVFS */

 /*
  * Assemble the final list of document formats...
  */

  if (!cupsArrayFind(docformats, (void *)"application/octet-stream"))
    cupsArrayAdd(docformats, (void *)"application/octet-stream");

  for (num_formats = 0, format = (const char *)cupsArrayFirst(docformats); format && num_formats < (int)(sizeof(formats) / sizeof(formats[0])); format = (const char *)cupsArrayNext(docformats))
    formats[num_formats ++] = format;

 /*
  * Get the list of attributes that can be used when creating a job...
  */

  num_sup_attrs = 0;
  sup_attrs[num_sup_attrs ++] = "document-access";
  sup_attrs[num_sup_attrs ++] = "document-charset";
  sup_attrs[num_sup_attrs ++] = "document-format";
  sup_attrs[num_sup_attrs ++] = "document-message";
  sup_attrs[num_sup_attrs ++] = "document-metadata";
  sup_attrs[num_sup_attrs ++] = "document-name";
  sup_attrs[num_sup_attrs ++] = "document-natural-language";
  sup_attrs[num_sup_attrs ++] = "ipp-attribute-fidelity";
  sup_attrs[num_sup_attrs ++] = "job-name";
  sup_attrs[num_sup_attrs ++] = "job-priority";

  for (i = 0; i < (int)(sizeof(job_creation) / sizeof(job_creation[0])) && num_sup_attrs < (int)(sizeof(sup_attrs) / sizeof(sup_attrs[0])); i ++)
  {
    snprintf(xxx_supported, sizeof(xxx_supported), "%s-supported", job_creation[i]);
    if (ippFindAttribute(attrs, xxx_supported, IPP_TAG_ZERO))
      sup_attrs[num_sup_attrs ++] = job_creation[i];
  }

 /*
  * Fill out the rest of the printer attributes.
  */

  printer->attrs = attrs;

  /* charset-configured */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");

  /* charset-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-supported", sizeof(charsets) / sizeof(charsets[0]), NULL, charsets);

  /* compression-supported */
  if (!ippFindAttribute(printer->attrs, "compression-supported", IPP_TAG_ZERO))
    ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "compression-supported", (int)(sizeof(compressions) / sizeof(compressions[0])), NULL, compressions);

  /* document-format-default */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "document-format-default", NULL, "application/octet-stream");

  /* document-format-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", num_formats, NULL, formats);

  /* generated-natural-language-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "generated-natural-language-supported", NULL, "en");

  /* identify-actions-default */
  ippAddString (printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-default", NULL, "sound");

  /* identify-actions-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-supported", sizeof(identify_actions) / sizeof(identify_actions[0]), NULL, identify_actions);

  /* ipp-features-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(features) / sizeof(features[0]), NULL, features);

  /* ipp-versions-supported */
  if (MaxVersion == 11)
    ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", NULL, "1.1");
  else
    ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", (int)(sizeof(versions) / sizeof(versions[0])), NULL, versions);

  /* job-creation-attributes-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-creation-attributes-supported", num_sup_attrs, NULL, sup_attrs);

  /* job-ids-supported */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "job-ids-supported", 1);

  /* job-k-octets-supported */
  ippAddRange(printer->attrs, IPP_TAG_PRINTER, "job-k-octets-supported", 0, k_supported);

  /* job-priority-default */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-default", 50);

  /* job-priority-supported */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-supported", 1);

  /* job-sheets-default */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-sheets-default", NULL, "none");

  /* job-sheets-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-sheets-supported", NULL, "none");

  /* media-col-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-col-supported", (int)(sizeof(media_col_supported) / sizeof(media_col_supported[0])), NULL, media_col_supported);

  /* multiple-document-handling-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-document-handling-supported", sizeof(multiple_document_handling) / sizeof(multiple_document_handling[0]), NULL, multiple_document_handling);

  /* multiple-document-jobs-supported */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "multiple-document-jobs-supported", 0);

  /* multiple-operation-time-out */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "multiple-operation-time-out", 60);

  /* multiple-operation-time-out-action */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-operation-time-out-action", NULL, "abort-job");

  /* natural-language-configured */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "natural-language-configured", NULL, "en");

  /* operations-supported */
  ippAddIntegers(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "operations-supported", sizeof(ops) / sizeof(ops[0]), ops);

  /* pdl-override-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pdl-override-supported", NULL, "attempted");

  /* preferred-attributes-supported */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "preferred-attributes-supported", 0);

  /* printer-get-attributes-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-get-attributes-supported", NULL, "document-format");

  /* printer-geo-location */
  ippAddOutOfBand(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_UNKNOWN, "printer-geo-location");

  /* printer-icons */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-icons", NULL, icons);

  /* printer-is-accepting-jobs */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);

  /* printer-info */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, name);

  /* printer-location */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", NULL, location);

  /* printer-more-info */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-more-info", NULL, adminurl);

  /* printer-name */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, name);

  /* printer-organization */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-organization", NULL, "");

  /* printer-organizational-unit */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-organizational-unit", NULL, "");

  /* printer-uri-supported */
#ifdef HAVE_SSL
  uris[0] = uri;
  uris[1] = securi;

  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", 2, NULL, (const char **)uris);

#else
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, uri);
#endif /* HAVE_SSL */

  /* printer-uuid */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid);

  /* reference-uri-scheme-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_URISCHEME), "reference-uri-schemes-supported", (int)(sizeof(reference_uri_schemes_supported) / sizeof(reference_uri_schemes_supported[0])), NULL, reference_uri_schemes_supported);

  /* uri-authentication-supported */
#ifdef HAVE_SSL
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-authentication-supported", 2, NULL, uri_authentication_supported);
#else
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-authentication-supported", NULL, "none");
#endif /* HAVE_SSL */

  /* uri-security-supported */
#ifdef HAVE_SSL
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-security-supported", 2, NULL, uri_security_supported);
#else
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-security-supported", NULL, "none");
#endif /* HAVE_SSL */

  /* which-jobs-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "which-jobs-supported", sizeof(which_jobs) / sizeof(which_jobs[0]), NULL, which_jobs);

  debug_attributes("Printer", printer->attrs, 0);

 /*
  * Register the printer with Bonjour...
  */

  if (!register_printer(printer, subtypes))
    goto bad_printer;

 /*
  * Return it!
  */

  return (printer);


 /*
  * If we get here we were unable to create the printer...
  */

  bad_printer:

  delete_printer(printer);

  return (NULL);
}


/*
 * 'debug_attributes()' - Print attributes in a request or response.
 */

static void
debug_attributes(const char *title,	/* I - Title */
                 ipp_t      *ipp,	/* I - Request/response */
                 int        type)	/* I - 0 = object, 1 = request, 2 = response */
{
  ipp_tag_t		group_tag;	/* Current group */
  ipp_attribute_t	*attr;		/* Current attribute */
  char			buffer[2048];	/* String buffer for value */
  int			major, minor;	/* Version */


  if (Verbosity <= 1)
    return;

  fprintf(stderr, "%s:\n", title);
  major = ippGetVersion(ipp, &minor);
  fprintf(stderr, "  version=%d.%d\n", major, minor);
  if (type == 1)
    fprintf(stderr, "  operation-id=%s(%04x)\n",
            ippOpString(ippGetOperation(ipp)), ippGetOperation(ipp));
  else if (type == 2)
    fprintf(stderr, "  status-code=%s(%04x)\n",
            ippErrorString(ippGetStatusCode(ipp)), ippGetStatusCode(ipp));
  fprintf(stderr, "  request-id=%d\n\n", ippGetRequestId(ipp));

  for (attr = ippFirstAttribute(ipp), group_tag = IPP_TAG_ZERO;
       attr;
       attr = ippNextAttribute(ipp))
  {
    if (ippGetGroupTag(attr) != group_tag)
    {
      group_tag = ippGetGroupTag(attr);
      fprintf(stderr, "  %s\n", ippTagString(group_tag));
    }

    if (ippGetName(attr))
    {
      ippAttributeString(attr, buffer, sizeof(buffer));
      fprintf(stderr, "    %s (%s%s) %s\n", ippGetName(attr),
	      ippGetCount(attr) > 1 ? "1setOf " : "",
	      ippTagString(ippGetValueTag(attr)), buffer);
    }
  }
}


/*
 * 'delete_client()' - Close the socket and free all memory used by a client
 *                     object.
 */

static void
delete_client(ipp3d_client_t *client)	/* I - Client */
{
  if (Verbosity)
    fprintf(stderr, "Closing connection from %s\n", client->hostname);

 /*
  * Flush pending writes before closing...
  */

  httpFlushWrite(client->http);

 /*
  * Free memory...
  */

  httpClose(client->http);

  ippDelete(client->request);
  ippDelete(client->response);

  free(client);
}


/*
 * 'delete_job()' - Remove from the printer and free all memory used by a job
 *                  object.
 */

static void
delete_job(ipp3d_job_t *job)		/* I - Job */
{
  if (Verbosity)
    fprintf(stderr, "[Job %d] Removing job from history.\n", job->id);

  ippDelete(job->attrs);

  if (job->message)
    free(job->message);

  if (job->filename)
  {
    if (!KeepFiles)
      unlink(job->filename);

    free(job->filename);
  }

  free(job);
}


/*
 * 'delete_printer()' - Unregister, close listen sockets, and free all memory
 *                      used by a printer object.
 */

static void
delete_printer(ipp3d_printer_t *printer)	/* I - Printer */
{
  if (printer->ipv4 >= 0)
    close(printer->ipv4);

  if (printer->ipv6 >= 0)
    close(printer->ipv6);

#if HAVE_DNSSD
  if (printer->printer_ref)
    DNSServiceRefDeallocate(printer->printer_ref);
  if (printer->ipp_ref)
    DNSServiceRefDeallocate(printer->ipp_ref);
  if (printer->ipps_ref)
    DNSServiceRefDeallocate(printer->ipps_ref);
  if (printer->http_ref)
    DNSServiceRefDeallocate(printer->http_ref);
#elif defined(HAVE_AVAHI)
  avahi_threaded_poll_lock(DNSSDMaster);

  if (printer->printer_ref)
    avahi_entry_group_free(printer->printer_ref);
  if (printer->ipp_ref)
    avahi_entry_group_free(printer->ipp_ref);
  if (printer->ipps_ref)
    avahi_entry_group_free(printer->ipps_ref);
  if (printer->http_ref)
    avahi_entry_group_free(printer->http_ref);

  avahi_threaded_poll_unlock(DNSSDMaster);
#endif /* HAVE_DNSSD */

  if (printer->dns_sd_name)
    free(printer->dns_sd_name);
  if (printer->name)
    free(printer->name);
  if (printer->icon)
    free(printer->icon);
  if (printer->command)
    free(printer->command);
  if (printer->device_uri)
    free(printer->device_uri);
#if !CUPS_LITE
  if (printer->ppdfile)
    free(printer->ppdfile);
#endif /* !CUPS_LITE */
  if (printer->directory)
    free(printer->directory);
  if (printer->hostname)
    free(printer->hostname);
  if (printer->uri)
    free(printer->uri);

  ippDelete(printer->attrs);
  cupsArrayDelete(printer->jobs);

  free(printer);
}


#ifdef HAVE_DNSSD
/*
 * 'dnssd_callback()' - Handle Bonjour registration events.
 */

static void DNSSD_API
dnssd_callback(
    DNSServiceRef       sdRef,		/* I - Service reference */
    DNSServiceFlags     flags,		/* I - Status flags */
    DNSServiceErrorType errorCode,	/* I - Error, if any */
    const char          *name,		/* I - Service name */
    const char          *regtype,	/* I - Service type */
    const char          *domain,	/* I - Domain for service */
    ipp3d_printer_t      *printer)	/* I - Printer */
{
  (void)sdRef;
  (void)flags;
  (void)domain;

  if (errorCode)
  {
    fprintf(stderr, "DNSServiceRegister for %s failed with error %d.\n", regtype, (int)errorCode);
    return;
  }
  else if (strcasecmp(name, printer->dns_sd_name))
  {
    if (Verbosity)
      fprintf(stderr, "Now using DNS-SD service name \"%s\".\n", name);

    /* No lock needed since only the main thread accesses/changes this */
    free(printer->dns_sd_name);
    printer->dns_sd_name = strdup(name);
  }
}


#elif defined(HAVE_AVAHI)
/*
 * 'dnssd_callback()' - Handle Bonjour registration events.
 */

static void
dnssd_callback(
    AvahiEntryGroup      *srv,		/* I - Service */
    AvahiEntryGroupState state,		/* I - Registration state */
    void                 *context)	/* I - Printer */
{
  (void)srv;
  (void)state;
  (void)context;
}


/*
 * 'dnssd_client_cb()' - Client callback for Avahi.
 *
 * Called whenever the client or server state changes...
 */

static void
dnssd_client_cb(
    AvahiClient      *c,		/* I - Client */
    AvahiClientState state,		/* I - Current state */
    void             *userdata)		/* I - User data (unused) */
{
  (void)userdata;

  if (!c)
    return;

  switch (state)
  {
    default :
        fprintf(stderr, "Ignored Avahi state %d.\n", state);
	break;

    case AVAHI_CLIENT_FAILURE:
	if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED)
	{
	  fputs("Avahi server crashed, exiting.\n", stderr);
	  exit(1);
	}
	break;
  }
}
#endif /* HAVE_DNSSD */


/*
 * 'dnssd_init()' - Initialize the DNS-SD service connections...
 */

static void
dnssd_init(void)
{
#ifdef HAVE_DNSSD
  if (DNSServiceCreateConnection(&DNSSDMaster) != kDNSServiceErr_NoError)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

#elif defined(HAVE_AVAHI)
  int error;			/* Error code, if any */

  if ((DNSSDMaster = avahi_threaded_poll_new()) == NULL)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

  if ((DNSSDClient = avahi_client_new(avahi_threaded_poll_get(DNSSDMaster), AVAHI_CLIENT_NO_FAIL, dnssd_client_cb, NULL, &error)) == NULL)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

  avahi_threaded_poll_start(DNSSDMaster);
#endif /* HAVE_DNSSD */
}


/*
 * 'filter_cb()' - Filter printer attributes based on the requested array.
 */

static int				/* O - 1 to copy, 0 to ignore */
filter_cb(ipp3d_filter_t   *filter,	/* I - Filter parameters */
          ipp_t           *dst,		/* I - Destination (unused) */
	  ipp_attribute_t *attr)	/* I - Source attribute */
{
 /*
  * Filter attributes as needed...
  */

#ifndef _WIN32 /* Avoid MS compiler bug */
  (void)dst;
#endif /* !_WIN32 */

  ipp_tag_t group = ippGetGroupTag(attr);
  const char *name = ippGetName(attr);

  if ((filter->group_tag != IPP_TAG_ZERO && group != filter->group_tag && group != IPP_TAG_ZERO) || !name || (!strcmp(name, "media-col-database") && !cupsArrayFind(filter->ra, (void *)name)))
    return (0);

  return (!filter->ra || cupsArrayFind(filter->ra, (void *)name) != NULL);
}


/*
 * 'find_job()' - Find a job specified in a request.
 */

static ipp3d_job_t *			/* O - Job or NULL */
find_job(ipp3d_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*attr;		/* job-id or job-uri attribute */
  ipp3d_job_t		key,		/* Job search key */
			*job;		/* Matching job, if any */


  if ((attr = ippFindAttribute(client->request, "job-uri", IPP_TAG_URI)) != NULL)
  {
    const char *uri = ippGetString(attr, 0, NULL);

    if (!strncmp(uri, client->printer->uri, client->printer->urilen) &&
        uri[client->printer->urilen] == '/')
      key.id = atoi(uri + client->printer->urilen + 1);
    else
      return (NULL);
  }
  else if ((attr = ippFindAttribute(client->request, "job-id", IPP_TAG_INTEGER)) != NULL)
    key.id = ippGetInteger(attr, 0);

  _cupsRWLockRead(&(client->printer->rwlock));
  job = (ipp3d_job_t *)cupsArrayFind(client->printer->jobs, &key);
  _cupsRWUnlock(&(client->printer->rwlock));

  return (job);
}


/*
 * 'finish_document()' - Finish receiving a document file and start processing.
 */

static void
finish_document_data(
    ipp3d_client_t *client,		/* I - Client */
    ipp3d_job_t    *job)		/* I - Job */
{
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  cups_array_t		*ra;		/* Attributes to send in response */
  _cups_thread_t        t;              /* Thread */


 /*
  * Create a file for the request data...
  *
  * TODO: Update code to support piping large raster data to the print command.
  */

  if ((job->fd = create_job_file(job, filename, sizeof(filename), client->printer->directory, NULL)) < 0)
  {
    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to create print file: %s", strerror(errno));

    goto abort_job;
  }

  if (Verbosity)
    fprintf(stderr, "Created job file \"%s\", format \"%s\".\n", filename, job->format);

  while ((bytes = httpRead2(client->http, buffer, sizeof(buffer))) > 0)
  {
    if (write(job->fd, buffer, (size_t)bytes) < bytes)
    {
      int error = errno;		/* Write error */

      close(job->fd);
      job->fd = -1;

      unlink(filename);

      respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));

      goto abort_job;
    }
  }

  if (bytes < 0)
  {
   /*
    * Got an error while reading the print data, so abort this job.
    */

    close(job->fd);
    job->fd = -1;

    unlink(filename);

    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to read print file.");

    goto abort_job;
  }

  if (close(job->fd))
  {
    int error = errno;			/* Write error */

    job->fd = -1;

    unlink(filename);

    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));

    goto abort_job;
  }

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

 /*
  * Process the job...
  */

  t = _cupsThreadCreate((_cups_thread_func_t)process_job, job);

  if (t)
  {
    _cupsThreadDetach(t);
  }
  else
  {
    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to process job.");
    goto abort_job;
  }

 /*
  * Return the job info...
  */

  respond_ipp(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-message");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
  return;

 /*
  * If we get here we had to abort the job...
  */

  abort_job:

  job->state     = IPP_JSTATE_ABORTED;
  job->completed = time(NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
}


/*
 * 'finish_uri()' - Finish fetching a document URI and start processing.
 */

static void
finish_document_uri(
    ipp3d_client_t *client,		/* I - Client */
    ipp3d_job_t    *job)		/* I - Job */
{
  ipp_attribute_t	*uri;		/* document-uri */
  char			scheme[256],	/* URI scheme */
			userpass[256],	/* Username and password info */
			hostname[256],	/* Hostname */
			resource[1024];	/* Resource path */
  int			port;		/* Port number */
  http_uri_status_t	uri_status;	/* URI decode status */
  http_encryption_t	encryption;	/* Encryption to use, if any */
  http_t		*http;		/* Connection for http/https URIs */
  http_status_t		status;		/* Access status for http/https URIs */
  int			infile;		/* Input file for local file URIs */
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  ipp_attribute_t	*attr;		/* Current attribute */
  cups_array_t		*ra;		/* Attributes to send in response */


 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Unexpected document data following request.");

    goto abort_job;
  }

 /*
  * Do we have a document URI?
  */

  if ((uri = ippFindAttribute(client->request, "document-uri", IPP_TAG_URI)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing document-uri.");

    goto abort_job;
  }

  if (ippGetCount(uri) != 1)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Too many document-uri values.");

    goto abort_job;
  }

  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL),
                               scheme, sizeof(scheme), userpass,
                               sizeof(userpass), hostname, sizeof(hostname),
                               &port, resource, sizeof(resource));
  if (uri_status < HTTP_URI_STATUS_OK)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad document-uri: %s", httpURIStatusString(uri_status));

    goto abort_job;
  }

  if (strcmp(scheme, "file") &&
#ifdef HAVE_SSL
      strcmp(scheme, "https") &&
#endif /* HAVE_SSL */
      strcmp(scheme, "http"))
  {
    respond_ipp(client, IPP_STATUS_ERROR_URI_SCHEME, "URI scheme \"%s\" not supported.", scheme);

    goto abort_job;
  }

  if (!strcmp(scheme, "file") && access(resource, R_OK))
  {
    respond_ipp(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to access URI: %s", strerror(errno));

    goto abort_job;
  }

 /*
  * Get the document format for the job...
  */

  _cupsRWLockWrite(&(client->printer->rwlock));

  if ((attr = ippFindAttribute(job->attrs, "document-format", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else
    job->format = "application/octet-stream";

 /*
  * Create a file for the request data...
  */

  if ((job->fd = create_job_file(job, filename, sizeof(filename), client->printer->directory, NULL)) < 0)
  {
    _cupsRWUnlock(&(client->printer->rwlock));

    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to create print file: %s", strerror(errno));

    goto abort_job;
  }

  _cupsRWUnlock(&(client->printer->rwlock));

  if (!strcmp(scheme, "file"))
  {
    if ((infile = open(resource, O_RDONLY)) < 0)
    {
      respond_ipp(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to access URI: %s", strerror(errno));

      goto abort_job;
    }

    do
    {
      if ((bytes = read(infile, buffer, sizeof(buffer))) < 0 &&
          (errno == EAGAIN || errno == EINTR))
      {
        bytes = 1;
      }
      else if (bytes > 0 && write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	close(infile);

	respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));

        goto abort_job;
      }
    }
    while (bytes > 0);

    close(infile);
  }
  else
  {
#ifdef HAVE_SSL
    if (port == 443 || !strcmp(scheme, "https"))
      encryption = HTTP_ENCRYPTION_ALWAYS;
    else
#endif /* HAVE_SSL */
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

    if ((http = httpConnect2(hostname, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
    {
      respond_ipp(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to connect to %s: %s", hostname, cupsLastErrorString());

      close(job->fd);
      job->fd = -1;

      unlink(filename);

      goto abort_job;
    }

    httpClearFields(http);
    httpSetField(http, HTTP_FIELD_ACCEPT_LANGUAGE, "en");
    if (httpGet(http, resource))
    {
      respond_ipp(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to GET URI: %s", strerror(errno));

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);

      goto abort_job;
    }

    while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE);

    if (status != HTTP_STATUS_OK)
    {
      respond_ipp(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to GET URI: %s", httpStatus(status));

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);

      goto abort_job;
    }

    while ((bytes = httpRead2(http, buffer, sizeof(buffer))) > 0)
    {
      if (write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	httpClose(http);

	respond_ipp(client, IPP_STATUS_ERROR_INTERNAL,
		    "Unable to write print file: %s", strerror(error));

        goto abort_job;
      }
    }

    httpClose(http);
  }

  if (close(job->fd))
  {
    int error = errno;		/* Write error */

    job->fd = -1;

    unlink(filename);

    respond_ipp(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));

    goto abort_job;
  }

  _cupsRWLockWrite(&(client->printer->rwlock));

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

  _cupsRWUnlock(&(client->printer->rwlock));

 /*
  * Process the job...
  */

  process_job(job);

 /*
  * Return the job info...
  */

  respond_ipp(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
  return;

 /*
  * If we get here we had to abort the job...
  */

  abort_job:

  job->state     = IPP_JSTATE_ABORTED;
  job->completed = time(NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
}


/*
 * 'html_escape()' - Write a HTML-safe string.
 */

static void
html_escape(ipp3d_client_t *client,	/* I - Client */
	    const char    *s,		/* I - String to write */
	    size_t        slen)		/* I - Number of characters to write */
{
  const char	*start,			/* Start of segment */
		*end;			/* End of string */


  start = s;
  end   = s + (slen > 0 ? slen : strlen(s));

  while (*s && s < end)
  {
    if (*s == '&' || *s == '<')
    {
      if (s > start)
        httpWrite2(client->http, start, (size_t)(s - start));

      if (*s == '&')
        httpWrite2(client->http, "&amp;", 5);
      else
        httpWrite2(client->http, "&lt;", 4);

      start = s + 1;
    }

    s ++;
  }

  if (s > start)
    httpWrite2(client->http, start, (size_t)(s - start));
}


/*
 * 'html_footer()' - Show the web interface footer.
 *
 * This function also writes the trailing 0-length chunk.
 */

static void
html_footer(ipp3d_client_t *client)	/* I - Client */
{
  html_printf(client,
	      "</div>\n"
	      "</body>\n"
	      "</html>\n");
  httpWrite2(client->http, "", 0);
}


/*
 * 'html_header()' - Show the web interface header and title.
 */

static void
html_header(ipp3d_client_t *client,	/* I - Client */
            const char    *title,	/* I - Title */
            int           refresh)	/* I - Refresh timer, if any */
{
  html_printf(client,
	      "<!doctype html>\n"
	      "<html>\n"
	      "<head>\n"
	      "<title>%s</title>\n"
	      "<link rel=\"shortcut icon\" href=\"/icon.png\" type=\"image/png\">\n"
	      "<link rel=\"apple-touch-icon\" href=\"/icon.png\" type=\"image/png\">\n"
	      "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=9\">\n", title);
  if (refresh > 0)
    html_printf(client, "<meta http-equiv=\"refresh\" content=\"%d\">\n", refresh);
  html_printf(client,
	      "<meta name=\"viewport\" content=\"width=device-width\">\n"
	      "<style>\n"
	      "body { font-family: sans-serif; margin: 0; }\n"
	      "div.body { padding: 0px 10px 10px; }\n"
	      "span.badge { background: #090; border-radius: 5px; color: #fff; padding: 5px 10px; }\n"
	      "span.bar { box-shadow: 0px 1px 5px #333; font-size: 75%%; }\n"
	      "table.form { border-collapse: collapse; margin-left: auto; margin-right: auto; margin-top: 10px; width: auto; }\n"
	      "table.form td, table.form th { padding: 5px 2px; }\n"
	      "table.form td.meter { border-right: solid 1px #ccc; padding: 0px; width: 400px; }\n"
	      "table.form th { text-align: right; }\n"
	      "table.striped { border-bottom: solid thin black; border-collapse: collapse; width: 100%%; }\n"
	      "table.striped tr:nth-child(even) { background: #fcfcfc; }\n"
	      "table.striped tr:nth-child(odd) { background: #f0f0f0; }\n"
	      "table.striped th { background: white; border-bottom: solid thin black; text-align: left; vertical-align: bottom; }\n"
	      "table.striped td { margin: 0; padding: 5px; vertical-align: top; }\n"
	      "table.nav { border-collapse: collapse; width: 100%%; }\n"
	      "table.nav td { margin: 0; text-align: center; }\n"
	      "td.nav a, td.nav a:active, td.nav a:hover, td.nav a:hover:link, td.nav a:hover:link:visited, td.nav a:link, td.nav a:link:visited, td.nav a:visited { background: inherit; color: inherit; font-size: 80%%; text-decoration: none; }\n"
	      "td.nav { background: #333; color: #fff; padding: 4px 8px; width: 50%%; }\n"
	      "td.nav.sel { background: #fff; color: #000; font-weight: bold; }\n"
	      "td.nav:hover { background: #666; color: #fff; }\n"
	      "td.nav:active { background: #000; color: #ff0; }\n"
	      "</style>\n"
	      "</head>\n"
	      "<body>\n"
	      "<table class=\"nav\"><tr>"
	      "<td class=\"nav%s\"><a href=\"/\">Status</a></td>"
	      "<td class=\"nav%s\"><a href=\"/materials\">Materials</a></td>"
	      "</tr></table>\n"
	      "<div class=\"body\">\n", !strcmp(client->uri, "/") ? " sel" : "", !strcmp(client->uri, "/materials") ? " sel" : "");
}


/*
 * 'html_printf()' - Send formatted text to the client, quoting as needed.
 */

static void
html_printf(ipp3d_client_t *client,	/* I - Client */
	    const char    *format,	/* I - Printf-style format string */
	    ...)			/* I - Additional arguments as needed */
{
  va_list	ap;			/* Pointer to arguments */
  const char	*start;			/* Start of string */
  char		size,			/* Size character (h, l, L) */
		type;			/* Format type character */
  int		width,			/* Width of field */
		prec;			/* Number of characters of precision */
  char		tformat[100],		/* Temporary format string for sprintf() */
		*tptr,			/* Pointer into temporary format */
		temp[1024];		/* Buffer for formatted numbers */
  char		*s;			/* Pointer to string */


 /*
  * Loop through the format string, formatting as needed...
  */

  va_start(ap, format);
  start = format;

  while (*format)
  {
    if (*format == '%')
    {
      if (format > start)
        httpWrite2(client->http, start, (size_t)(format - start));

      tptr    = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        httpWrite2(client->http, "%", 1);
        format ++;
	start = format;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        *tptr++ = *format++;

      if (*format == '*')
      {
       /*
        * Get width from argument...
	*/

	format ++;
	width = va_arg(ap, int);

	snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", width);
	tptr += strlen(tptr);
      }
      else
      {
	width = 0;

	while (isdigit(*format & 255))
	{
	  if (tptr < (tformat + sizeof(tformat) - 1))
	    *tptr++ = *format;

	  width = width * 10 + *format++ - '0';
	}
      }

      if (*format == '.')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        format ++;

        if (*format == '*')
	{
         /*
	  * Get precision from argument...
	  */

	  format ++;
	  prec = va_arg(ap, int);

	  snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", prec);
	  tptr += strlen(tptr);
	}
	else
	{
	  prec = 0;

	  while (isdigit(*format & 255))
	  {
	    if (tptr < (tformat + sizeof(tformat) - 1))
	      *tptr++ = *format;

	    prec = prec * 10 + *format++ - '0';
	  }
	}
      }

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';

	if (tptr < (tformat + sizeof(tformat) - 2))
	{
	  *tptr++ = 'l';
	  *tptr++ = 'l';
	}

	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        size = *format++;
      }
      else
        size = 0;


      if (!*format)
      {
        start = format;
        break;
      }

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';
      start = format;

      switch (type)
      {
	case 'E' : /* Floating point formats */
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    sprintf(temp, tformat, va_arg(ap, double));

            httpWrite2(client->http, temp, strlen(temp));
	    break;

        case 'B' : /* Integer formats */
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      sprintf(temp, tformat, va_arg(ap, long long));
	    else
#  endif /* HAVE_LONG_LONG */
            if (size == 'l')
	      sprintf(temp, tformat, va_arg(ap, long));
	    else
	      sprintf(temp, tformat, va_arg(ap, int));

            httpWrite2(client->http, temp, strlen(temp));
	    break;

	case 'p' : /* Pointer value */
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    sprintf(temp, tformat, va_arg(ap, void *));

            httpWrite2(client->http, temp, strlen(temp));
	    break;

        case 'c' : /* Character or character array */
            if (width <= 1)
            {
              temp[0] = (char)va_arg(ap, int);
              temp[1] = '\0';
              html_escape(client, temp, 1);
            }
            else
              html_escape(client, va_arg(ap, char *), (size_t)width);
	    break;

	case 's' : /* String */
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

            html_escape(client, s, strlen(s));
	    break;
      }
    }
    else
      format ++;
  }

  if (format > start)
    httpWrite2(client->http, start, (size_t)(format - start));

  va_end(ap);
}


/*
 * 'ipp_cancel_job()' - Cancel a job.
 */

static void
ipp_cancel_job(ipp3d_client_t *client)	/* I - Client */
{
  ipp3d_job_t		*job;		/* Job information */


 /*
  * Get the job...
  */

  if ((job = find_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    return;
  }

 /*
  * See if the job is already completed, canceled, or aborted; if so,
  * we can't cancel...
  */

  switch (job->state)
  {
    case IPP_JSTATE_CANCELED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already canceled - can\'t cancel.", job->id);
        break;

    case IPP_JSTATE_ABORTED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already aborted - can\'t cancel.", job->id);
        break;

    case IPP_JSTATE_COMPLETED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already completed - can\'t cancel.", job->id);
        break;

    default :
       /*
        * Cancel the job...
	*/

	_cupsRWLockWrite(&(client->printer->rwlock));

	if (job->state == IPP_JSTATE_PROCESSING ||
	    (job->state == IPP_JSTATE_HELD && job->fd >= 0))
          job->cancel = 1;
	else
	{
	  job->state     = IPP_JSTATE_CANCELED;
	  job->completed = time(NULL);
	}

	_cupsRWUnlock(&(client->printer->rwlock));

	respond_ipp(client, IPP_STATUS_OK, NULL);
        break;
  }
}


/*
 * 'ipp_close_job()' - Close an open job.
 */

static void
ipp_close_job(ipp3d_client_t *client)	/* I - Client */
{
  ipp3d_job_t		*job;		/* Job information */


 /*
  * Get the job...
  */

  if ((job = find_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    return;
  }

 /*
  * See if the job is already completed, canceled, or aborted; if so,
  * we can't cancel...
  */

  switch (job->state)
  {
    case IPP_JSTATE_CANCELED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is canceled - can\'t close.", job->id);
        break;

    case IPP_JSTATE_ABORTED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is aborted - can\'t close.", job->id);
        break;

    case IPP_JSTATE_COMPLETED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is completed - can\'t close.", job->id);
        break;

    case IPP_JSTATE_PROCESSING :
    case IPP_JSTATE_STOPPED :
	respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already closed.", job->id);
        break;

    default :
	respond_ipp(client, IPP_STATUS_OK, NULL);
        break;
  }
}


/*
 * 'ipp_create_job()' - Create a job object.
 */

static void
ipp_create_job(ipp3d_client_t *client)	/* I - Client */
{
  ipp3d_job_t		*job;		/* New job */
  cups_array_t		*ra;		/* Attributes to send in response */


 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Unexpected document data following request.");
    return;
  }

 /*
  * Create the job...
  */

  if ((job = create_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BUSY,
                "Currently printing another job.");
    return;
  }

 /*
  * Return the job info...
  */

  respond_ipp(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-message");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_job_attributes()' - Get the attributes for a job object.
 */

static void
ipp_get_job_attributes(
    ipp3d_client_t *client)		/* I - Client */
{
  ipp3d_job_t	*job;			/* Job */
  cups_array_t	*ra;			/* requested-attributes */


  if ((job = find_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "Job not found.");
    return;
  }

  respond_ipp(client, IPP_STATUS_OK, NULL);

  ra = ippCreateRequestedArray(client->request);
  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_jobs()' - Get a list of job objects.
 */

static void
ipp_get_jobs(ipp3d_client_t *client)	/* I - Client */
{
  ipp_attribute_t	*attr;		/* Current attribute */
  const char		*which_jobs = NULL;
					/* which-jobs values */
  int			job_comparison;	/* Job comparison */
  ipp_jstate_t		job_state;	/* job-state value */
  int			first_job_id,	/* First job ID */
			limit,		/* Maximum number of jobs to return */
			count;		/* Number of jobs that match */
  const char		*username;	/* Username */
  ipp3d_job_t		*job;		/* Current job pointer */
  cups_array_t		*ra;		/* Requested attributes array */


 /*
  * See if the "which-jobs" attribute have been specified...
  */

  if ((attr = ippFindAttribute(client->request, "which-jobs",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    which_jobs = ippGetString(attr, 0, NULL);
    fprintf(stderr, "%s Get-Jobs which-jobs=%s", client->hostname, which_jobs);
  }

  if (!which_jobs || !strcmp(which_jobs, "not-completed"))
  {
    job_comparison = -1;
    job_state      = IPP_JSTATE_STOPPED;
  }
  else if (!strcmp(which_jobs, "completed"))
  {
    job_comparison = 1;
    job_state      = IPP_JSTATE_CANCELED;
  }
  else if (!strcmp(which_jobs, "aborted"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_ABORTED;
  }
  else if (!strcmp(which_jobs, "all"))
  {
    job_comparison = 1;
    job_state      = IPP_JSTATE_PENDING;
  }
  else if (!strcmp(which_jobs, "canceled"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_CANCELED;
  }
  else if (!strcmp(which_jobs, "pending"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_PENDING;
  }
  else if (!strcmp(which_jobs, "pending-held"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_HELD;
  }
  else if (!strcmp(which_jobs, "processing"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_PROCESSING;
  }
  else if (!strcmp(which_jobs, "processing-stopped"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_STOPPED;
  }
  else
  {
    respond_ipp(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES,
                "The which-jobs value \"%s\" is not supported.", which_jobs);
    ippAddString(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
                 "which-jobs", NULL, which_jobs);
    return;
  }

 /*
  * See if they want to limit the number of jobs reported...
  */

  if ((attr = ippFindAttribute(client->request, "limit",
                               IPP_TAG_INTEGER)) != NULL)
  {
    limit = ippGetInteger(attr, 0);

    fprintf(stderr, "%s Get-Jobs limit=%d", client->hostname, limit);
  }
  else
    limit = 0;

  if ((attr = ippFindAttribute(client->request, "first-job-id",
                               IPP_TAG_INTEGER)) != NULL)
  {
    first_job_id = ippGetInteger(attr, 0);

    fprintf(stderr, "%s Get-Jobs first-job-id=%d", client->hostname, first_job_id);
  }
  else
    first_job_id = 1;

 /*
  * See if we only want to see jobs for a specific user...
  */

  username = NULL;

  if ((attr = ippFindAttribute(client->request, "my-jobs",
                               IPP_TAG_BOOLEAN)) != NULL)
  {
    int my_jobs = ippGetBoolean(attr, 0);

    fprintf(stderr, "%s Get-Jobs my-jobs=%s\n", client->hostname, my_jobs ? "true" : "false");

    if (my_jobs)
    {
      if ((attr = ippFindAttribute(client->request, "requesting-user-name",
					IPP_TAG_NAME)) == NULL)
      {
	respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Need requesting-user-name with my-jobs.");
	return;
      }

      username = ippGetString(attr, 0, NULL);

      fprintf(stderr, "%s Get-Jobs requesting-user-name=\"%s\"\n", client->hostname, username);
    }
  }

 /*
  * OK, build a list of jobs for this printer...
  */

  ra = ippCreateRequestedArray(client->request);

  respond_ipp(client, IPP_STATUS_OK, NULL);

  _cupsRWLockRead(&(client->printer->rwlock));

  for (count = 0, job = (ipp3d_job_t *)cupsArrayFirst(client->printer->jobs);
       (limit <= 0 || count < limit) && job;
       job = (ipp3d_job_t *)cupsArrayNext(client->printer->jobs))
  {
   /*
    * Filter out jobs that don't match...
    */

    if ((job_comparison < 0 && job->state > job_state) ||
	(job_comparison == 0 && job->state != job_state) ||
	(job_comparison > 0 && job->state < job_state) ||
	job->id < first_job_id ||
	(username && job->username &&
	 strcasecmp(username, job->username)))
      continue;

    if (count > 0)
      ippAddSeparator(client->response);

    count ++;
    copy_job_attributes(client, job, ra);
  }

  cupsArrayDelete(ra);

  _cupsRWUnlock(&(client->printer->rwlock));
}


/*
 * 'ipp_get_printer_attributes()' - Get the attributes for a printer object.
 */

static void
ipp_get_printer_attributes(
    ipp3d_client_t *client)		/* I - Client */
{
  cups_array_t		*ra;		/* Requested attributes array */
  ipp3d_printer_t	*printer;	/* Printer */


 /*
  * Send the attributes...
  */

  ra      = ippCreateRequestedArray(client->request);
  printer = client->printer;

  respond_ipp(client, IPP_STATUS_OK, NULL);

  _cupsRWLockRead(&(printer->rwlock));

  copy_attributes(client->response, printer->attrs, ra, IPP_TAG_ZERO,
		  IPP_TAG_CUPS_CONST);

  if (!ra || cupsArrayFind(ra, "printer-config-change-date-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-config-change-date-time", ippTimeToDate(printer->config_time));

  if (!ra || cupsArrayFind(ra, "printer-config-change-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-config-change-time", (int)(printer->config_time - printer->start_time));

  if (!ra || cupsArrayFind(ra, "printer-current-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-current-time", ippTimeToDate(time(NULL)));


  if (!ra || cupsArrayFind(ra, "printer-state"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", (int)printer->state);

  if (!ra || cupsArrayFind(ra, "printer-state-change-date-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-state-change-date-time", ippTimeToDate(printer->state_time));

  if (!ra || cupsArrayFind(ra, "printer-state-change-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-state-change-time", (int)(printer->state_time - printer->start_time));

  if (!ra || cupsArrayFind(ra, "printer-state-message"))
  {
    static const char * const messages[] = { "Idle.", "Printing.", "Stopped." };

    ippAddString(client->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-state-message", NULL, messages[printer->state - IPP_PSTATE_IDLE]);
  }

  if (!ra || cupsArrayFind(ra, "printer-state-reasons"))
  {
    if (printer->state_reasons == IPP3D_PREASON_NONE)
    {
      ippAddString(client->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-state-reasons", NULL, "none");
    }
    else
    {
      ipp_attribute_t	*attr = NULL;		/* printer-state-reasons */
      ipp3d_preason_t	bit;			/* Reason bit */
      int		i;			/* Looping var */
      char		reason[32];		/* Reason string */

      for (i = 0, bit = 1; i < (int)(sizeof(ipp3d_preason_strings) / sizeof(ipp3d_preason_strings[0])); i ++, bit *= 2)
      {
        if (printer->state_reasons & bit)
	{
	  snprintf(reason, sizeof(reason), "%s-%s", ipp3d_preason_strings[i], printer->state == IPP_PSTATE_IDLE ? "report" : printer->state == IPP_PSTATE_PROCESSING ? "warning" : "error");
	  if (attr)
	    ippSetString(client->response, &attr, ippGetCount(attr), reason);
	  else
	    attr = ippAddString(client->response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "printer-state-reasons", NULL, reason);
	}
      }
    }
  }

  if (!ra || cupsArrayFind(ra, "printer-up-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-up-time", (int)(time(NULL) - printer->start_time));

  if (!ra || cupsArrayFind(ra, "queued-job-count"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "queued-job-count", printer->active_job && printer->active_job->state < IPP_JSTATE_CANCELED);

  _cupsRWUnlock(&(printer->rwlock));

  cupsArrayDelete(ra);
}


/*
 * 'ipp_identify_printer()' - Beep or display a message.
 */

static void
ipp_identify_printer(
    ipp3d_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*actions,	/* identify-actions */
			*message;	/* message */


  actions = ippFindAttribute(client->request, "identify-actions", IPP_TAG_KEYWORD);
  message = ippFindAttribute(client->request, "message", IPP_TAG_TEXT);

  if (!actions || ippContainsString(actions, "sound"))
  {
    putchar(0x07);
    fflush(stdout);
  }

  if (ippContainsString(actions, "display"))
    printf("IDENTIFY from %s: %s\n", client->hostname, message ? ippGetString(message, 0, NULL) : "No message supplied");

  respond_ipp(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_send_document()' - Add an attached document to a job object created with
 *                         Create-Job.
 */

static void
ipp_send_document(
    ipp3d_client_t *client)		/* I - Client */
{
  ipp3d_job_t		*job;		/* Job information */
  ipp_attribute_t	*attr;		/* Current attribute */


 /*
  * Get the job...
  */

  if ((job = find_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
    return;
  }

 /*
  * See if we already have a document for this job or the job has already
  * in a non-pending state...
  */

  if (job->state > IPP_JSTATE_HELD)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job is not in a pending state.");
    httpFlush(client->http);
    return;
  }
  else if (job->filename || job->fd >= 0)
  {
    respond_ipp(client, IPP_STATUS_ERROR_MULTIPLE_JOBS_NOT_SUPPORTED, "Multiple document jobs are not supported.");
    httpFlush(client->http);
    return;
  }

 /*
  * Make sure we have the "last-document" operation attribute...
  */

  if ((attr = ippFindAttribute(client->request, "last-document", IPP_TAG_ZERO)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing required last-document attribute.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetGroupTag(attr) != IPP_TAG_OPERATION)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "The last-document attribute is not in the operation group.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetValueTag(attr) != IPP_TAG_BOOLEAN || ippGetCount(attr) != 1 || !ippGetBoolean(attr, 0))
  {
    respond_unsupported(client, attr);
    httpFlush(client->http);
    return;
  }

 /*
  * Validate document attributes...
  */

  if (!valid_doc_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

 /*
  * Then finish getting the document data and process things...
  */

  _cupsRWLockWrite(&(client->printer->rwlock));

  copy_attributes(job->attrs, client->request, NULL, IPP_TAG_JOB, 0);

  if ((attr = ippFindAttribute(job->attrs, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else if ((attr = ippFindAttribute(job->attrs, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else
    job->format = "application/octet-stream";

  _cupsRWUnlock(&(client->printer->rwlock));

  finish_document_data(client, job);
}


/*
 * 'ipp_send_uri()' - Add a referenced document to a job object created with
 *                    Create-Job.
 */

static void
ipp_send_uri(ipp3d_client_t *client)	/* I - Client */
{
  ipp3d_job_t		*job;		/* Job information */
  ipp_attribute_t	*attr;		/* Current attribute */


 /*
  * Get the job...
  */

  if ((job = find_job(client)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
    return;
  }

 /*
  * See if we already have a document for this job or the job has already
  * in a non-pending state...
  */

  if (job->state > IPP_JSTATE_HELD)
  {
    respond_ipp(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job is not in a pending state.");
    httpFlush(client->http);
    return;
  }
  else if (job->filename || job->fd >= 0)
  {
    respond_ipp(client, IPP_STATUS_ERROR_MULTIPLE_JOBS_NOT_SUPPORTED, "Multiple document jobs are not supported.");
    httpFlush(client->http);
    return;
  }

  if ((attr = ippFindAttribute(client->request, "last-document", IPP_TAG_ZERO)) == NULL)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing required last-document attribute.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetGroupTag(attr) != IPP_TAG_OPERATION)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "The last-document attribute is not in the operation group.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetValueTag(attr) != IPP_TAG_BOOLEAN || ippGetCount(attr) != 1 || !ippGetBoolean(attr, 0))
  {
    respond_unsupported(client, attr);
    httpFlush(client->http);
    return;
  }

 /*
  * Validate document attributes...
  */

  if (!valid_doc_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

 /*
  * Then finish getting the document data and process things...
  */

  _cupsRWLockWrite(&(client->printer->rwlock));

  copy_attributes(job->attrs, client->request, NULL, IPP_TAG_JOB, 0);

  if ((attr = ippFindAttribute(job->attrs, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else if ((attr = ippFindAttribute(job->attrs, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else
    job->format = "application/octet-stream";

  _cupsRWUnlock(&(client->printer->rwlock));

  finish_document_uri(client, job);
}


/*
 * 'ipp_validate_job()' - Validate job creation attributes.
 */

static void
ipp_validate_job(ipp3d_client_t *client)	/* I - Client */
{
  if (valid_job_attributes(client))
    respond_ipp(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ippserver_attr_cb()' - Determine whether an attribute should be loaded.
 */

static int				/* O - 1 to use, 0 to ignore */
ippserver_attr_cb(
    _ipp_file_t    *f,			/* I - IPP file */
    void           *user_data,		/* I - User data pointer (unused) */
    const char     *attr)		/* I - Attribute name */
{
  int		i,			/* Current element */
		result;			/* Result of comparison */
  static const char * const ignored[] =
  {					/* Ignored attributes */
    "attributes-charset",
    "attributes-natural-language",
    "charset-configured",
    "charset-supported",
    "device-service-count",
    "device-uuid",
    "document-format-varying-attributes",
    "generated-natural-language-supported",
    "identify-actions-default",
    "identify-actions-supported",
    "ipp-features-supported",
    "ipp-versions-supproted",
    "ippget-event-life",
    "job-hold-until-supported",
    "job-hold-until-time-supported",
    "job-ids-supported",
    "job-k-octets-supported",
    "job-settable-attributes-supported",
    "multiple-document-jobs-supported",
    "multiple-operation-time-out",
    "multiple-operation-time-out-action",
    "natural-language-configured",
    "notify-attributes-supported",
    "notify-events-default",
    "notify-events-supported",
    "notify-lease-duration-default",
    "notify-lease-duration-supported",
    "notify-max-events-supported",
    "notify-pull-method-supported",
    "operations-supported",
    "printer-alert",
    "printer-alert-description",
    "printer-camera-image-uri",
    "printer-charge-info",
    "printer-charge-info-uri",
    "printer-config-change-date-time",
    "printer-config-change-time",
    "printer-current-time",
    "printer-detailed-status-messages",
    "printer-dns-sd-name",
    "printer-fax-log-uri",
    "printer-get-attributes-supported",
    "printer-icons",
    "printer-id",
    "printer-info",
    "printer-is-accepting-jobs",
    "printer-message-date-time",
    "printer-message-from-operator",
    "printer-message-time",
    "printer-more-info",
    "printer-service-type",
    "printer-settable-attributes-supported",
    "printer-state",
    "printer-state-message",
    "printer-state-reasons",
    "printer-static-resource-directory-uri",
    "printer-static-resource-k-octets-free",
    "printer-static-resource-k-octets-supported",
    "printer-strings-languages-supported",
    "printer-strings-uri",
    "printer-supply-info-uri",
    "printer-up-time",
    "printer-uri-supported",
    "printer-xri-supported",
    "queued-job-count",
    "reference-uri-scheme-supported",
    "uri-authentication-supported",
    "uri-security-supported",
    "which-jobs-supported",
    "xri-authentication-supported",
    "xri-security-supported",
    "xri-uri-scheme-supported"
  };


  (void)f;
  (void)user_data;

  for (i = 0, result = 1; i < (int)(sizeof(ignored) / sizeof(ignored[0])); i ++)
  {
    if ((result = strcmp(attr, ignored[i])) <= 0)
      break;
  }

  return (result != 0);
}


/*
 * 'ippserver_error_cb()' - Log an error message.
 */

static int				/* O - 1 to continue, 0 to stop */
ippserver_error_cb(
    _ipp_file_t    *f,			/* I - IPP file data */
    void           *user_data,		/* I - User data pointer (unused) */
    const char     *error)		/* I - Error message */
{
  (void)f;
  (void)user_data;

  _cupsLangPrintf(stderr, "%s\n", error);

  return (1);
}


/*
 * 'ippserver_token_cb()' - Process ippserver-specific config file tokens.
 */

static int				/* O - 1 to continue, 0 to stop */
ippserver_token_cb(
    _ipp_file_t    *f,			/* I - IPP file data */
    _ipp_vars_t    *vars,		/* I - IPP variables */
    void           *user_data,		/* I - User data pointer (unused) */
    const char     *token)		/* I - Current token */
{
  (void)vars;
  (void)user_data;

  if (!token)
  {
   /*
    * NULL token means do the initial setup - create an empty IPP message and
    * return...
    */

    f->attrs     = ippNew();
    f->group_tag = IPP_TAG_PRINTER;
  }
  else
  {
    _cupsLangPrintf(stderr, _("Unknown directive \"%s\" on line %d of \"%s\" ignored."), token, f->linenum, f->filename);
  }

  return (1);
}


/*
 * 'load_ippserver_attributes()' - Load IPP attributes from an ippserver file.
 */

static ipp_t *				/* O - IPP attributes or `NULL` on error */
load_ippserver_attributes(
    const char   *servername,		/* I - Server name or `NULL` for default */
    int          serverport,		/* I - Server port number */
    const char   *filename,		/* I - ippserver attribute filename */
    cups_array_t *docformats)		/* I - document-format-supported values */
{
  ipp_t		*attrs;			/* IPP attributes */
  _ipp_vars_t	vars;			/* IPP variables */
  char		temp[256];		/* Temporary string */


  (void)docformats; /* for now */

 /*
  * Setup callbacks and variables for the printer configuration file...
  *
  * The following additional variables are supported:
  *
  * - SERVERNAME: The host name of the server.
  * - SERVERPORT: The default port of the server.
  */

  _ippVarsInit(&vars, (_ipp_fattr_cb_t)ippserver_attr_cb, (_ipp_ferror_cb_t)ippserver_error_cb, (_ipp_ftoken_cb_t)ippserver_token_cb);

  if (servername)
  {
    _ippVarsSet(&vars, "SERVERNAME", servername);
  }
  else
  {
    httpGetHostname(NULL, temp, sizeof(temp));
    _ippVarsSet(&vars, "SERVERNAME", temp);
  }

  snprintf(temp, sizeof(temp), "%d", serverport);
  _ippVarsSet(&vars, "SERVERPORT", temp);

 /*
  * Load attributes and values for the printer...
  */

  attrs = _ippFileParse(&vars, filename, NULL);

 /*
  * Free memory and return...
  */

  _ippVarsDeinit(&vars);

  return (attrs);
}


/*
 * 'parse_options()' - Parse URL options into CUPS options.
 *
 * The client->options string is destroyed by this function.
 */

static int				/* O - Number of options */
parse_options(ipp3d_client_t *client,	/* I - Client */
              cups_option_t   **options)/* O - Options */
{
  char	*name,				/* Name */
      	*value,				/* Value */
	*next;				/* Next name=value pair */
  int	num_options = 0;		/* Number of options */


  *options = NULL;

  for (name = client->options; name && *name; name = next)
  {
    if ((value = strchr(name, '=')) == NULL)
      break;

    *value++ = '\0';
    if ((next = strchr(value, '&')) != NULL)
      *next++ = '\0';

    num_options = cupsAddOption(name, value, num_options, options);
  }

  return (num_options);
}


/*
 * 'process_attr_message()' - Process an ATTR: message from a command.
 */

static void
process_attr_message(
    ipp3d_job_t *job,			/* I - Job */
    char       *message)		/* I - Message */
{
  int		i,			/* Looping var */
		num_options = 0;	/* Number of name=value pairs */
  cups_option_t	*options = NULL,	/* name=value pairs from message */
		*option;		/* Current option */
  ipp_attribute_t *attr;		/* Current attribute */


 /*
  * Grab attributes from the message line...
  */

  num_options = cupsParseOptions(message + 5, num_options, &options);

 /*
  * Loop through the options and record them in the printer or job objects...
  */

  for (i = num_options, option = options; i > 0; i --, option ++)
  {
    if (!strcmp(option->name, "job-impressions"))
    {
     /*
      * Update job-impressions attribute...
      */

      job->impressions = atoi(option->value);
    }
    else if (!strcmp(option->name, "job-impressions-completed"))
    {
     /*
      * Update job-impressions-completed attribute...
      */

      job->impcompleted = atoi(option->value);
    }
    else if (!strncmp(option->name, "marker-", 7) || !strcmp(option->name, "printer-alert") || !strcmp(option->name, "printer-alert-description") || !strcmp(option->name, "printer-supply") || !strcmp(option->name, "printer-supply-description"))
    {
     /*
      * Update Printer Status attribute...
      */

      _cupsRWLockWrite(&job->printer->rwlock);

      if ((attr = ippFindAttribute(job->printer->attrs, option->name, IPP_TAG_ZERO)) != NULL)
        ippDeleteAttribute(job->printer->attrs, attr);

      cupsEncodeOption(job->printer->attrs, IPP_TAG_PRINTER, option->name, option->value);

      _cupsRWUnlock(&job->printer->rwlock);
    }
    else
    {
     /*
      * Something else that isn't currently supported...
      */

      fprintf(stderr, "[Job %d] Ignoring update of attribute \"%s\" with value \"%s\".\n", job->id, option->name, option->value);
    }
  }

  cupsFreeOptions(num_options, options);
}


/*
 * 'process_client()' - Process client requests on a thread.
 */

static void *				/* O - Exit status */
process_client(ipp3d_client_t *client)	/* I - Client */
{
 /*
  * Loop until we are out of requests or timeout (30 seconds)...
  */

#ifdef HAVE_SSL
  int first_time = 1;			/* First time request? */
#endif /* HAVE_SSL */

  while (httpWait(client->http, 30000))
  {
#ifdef HAVE_SSL
    if (first_time)
    {
     /*
      * See if we need to negotiate a TLS connection...
      */

      char buf[1];			/* First byte from client */

      if (recv(httpGetFd(client->http), buf, 1, MSG_PEEK) == 1 && (!buf[0] || !strchr("DGHOPT", buf[0])))
      {
        fprintf(stderr, "%s Starting HTTPS session.\n", client->hostname);

	if (httpEncryption(client->http, HTTP_ENCRYPTION_ALWAYS))
	{
	  fprintf(stderr, "%s Unable to encrypt connection: %s\n", client->hostname, cupsLastErrorString());
	  break;
        }

        fprintf(stderr, "%s Connection now encrypted.\n", client->hostname);
      }

      first_time = 0;
    }
#endif /* HAVE_SSL */

    if (!process_http(client))
      break;
  }

 /*
  * Close the conection to the client and return...
  */

  delete_client(client);

  return (NULL);
}


/*
 * 'process_http()' - Process a HTTP request.
 */

int					/* O - 1 on success, 0 on failure */
process_http(ipp3d_client_t *client)	/* I - Client connection */
{
  char			uri[1024];	/* URI */
  http_state_t		http_state;	/* HTTP state */
  http_status_t		http_status;	/* HTTP status */
  ipp_state_t		ipp_state;	/* State of IPP transfer */
  char			scheme[32],	/* Method/scheme */
			userpass[128],	/* Username:password */
			hostname[HTTP_MAX_HOST];
					/* Hostname */
  int			port;		/* Port number */
  static const char * const http_states[] =
  {					/* Strings for logging HTTP method */
    "WAITING",
    "OPTIONS",
    "GET",
    "GET_SEND",
    "HEAD",
    "POST",
    "POST_RECV",
    "POST_SEND",
    "PUT",
    "PUT_RECV",
    "DELETE",
    "TRACE",
    "CONNECT",
    "STATUS",
    "UNKNOWN_METHOD",
    "UNKNOWN_VERSION"
  };


 /*
  * Clear state variables...
  */

  ippDelete(client->request);
  ippDelete(client->response);

  client->request   = NULL;
  client->response  = NULL;
  client->operation = HTTP_STATE_WAITING;

 /*
  * Read a request from the connection...
  */

  while ((http_state = httpReadRequest(client->http, uri,
                                       sizeof(uri))) == HTTP_STATE_WAITING)
    usleep(1);

 /*
  * Parse the request line...
  */

  if (http_state == HTTP_STATE_ERROR)
  {
    if (httpError(client->http) == EPIPE)
      fprintf(stderr, "%s Client closed connection.\n", client->hostname);
    else
      fprintf(stderr, "%s Bad request line (%s).\n", client->hostname, strerror(httpError(client->http)));

    return (0);
  }
  else if (http_state == HTTP_STATE_UNKNOWN_METHOD)
  {
    fprintf(stderr, "%s Bad/unknown operation.\n", client->hostname);
    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }
  else if (http_state == HTTP_STATE_UNKNOWN_VERSION)
  {
    fprintf(stderr, "%s Bad HTTP version.\n", client->hostname);
    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  fprintf(stderr, "%s %s %s\n", client->hostname, http_states[http_state], uri);

 /*
  * Separate the URI into its components...
  */

  if (httpSeparateURI(HTTP_URI_CODING_MOST, uri, scheme, sizeof(scheme),
		      userpass, sizeof(userpass),
		      hostname, sizeof(hostname), &port,
		      client->uri, sizeof(client->uri)) < HTTP_URI_STATUS_OK &&
      (http_state != HTTP_STATE_OPTIONS || strcmp(uri, "*")))
  {
    fprintf(stderr, "%s Bad URI \"%s\".\n", client->hostname, uri);
    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  if ((client->options = strchr(client->uri, '?')) != NULL)
    *(client->options)++ = '\0';

 /*
  * Process the request...
  */

  client->start     = time(NULL);
  client->operation = httpGetState(client->http);

 /*
  * Parse incoming parameters until the status changes...
  */

  while ((http_status = httpUpdate(client->http)) == HTTP_STATUS_CONTINUE);

  if (http_status != HTTP_STATUS_OK)
  {
    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  if (!httpGetField(client->http, HTTP_FIELD_HOST)[0] &&
      httpGetVersion(client->http) >= HTTP_VERSION_1_1)
  {
   /*
    * HTTP/1.1 and higher require the "Host:" field...
    */

    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

 /*
  * Handle HTTP Upgrade...
  */

  if (!strcasecmp(httpGetField(client->http, HTTP_FIELD_CONNECTION),
                        "Upgrade"))
  {
#ifdef HAVE_SSL
    if (strstr(httpGetField(client->http, HTTP_FIELD_UPGRADE), "TLS/") != NULL && !httpIsEncrypted(client->http))
    {
      if (!respond_http(client, HTTP_STATUS_SWITCHING_PROTOCOLS, NULL, NULL, 0))
        return (0);

      fprintf(stderr, "%s Upgrading to encrypted connection.\n", client->hostname);

      if (httpEncryption(client->http, HTTP_ENCRYPTION_REQUIRED))
      {
        fprintf(stderr, "%s Unable to encrypt connection: %s\n", client->hostname, cupsLastErrorString());
	return (0);
      }

      fprintf(stderr, "%s Connection now encrypted.\n", client->hostname);
    }
    else
#endif /* HAVE_SSL */

    if (!respond_http(client, HTTP_STATUS_NOT_IMPLEMENTED, NULL, NULL, 0))
      return (0);
  }

 /*
  * Handle HTTP Expect...
  */

  if (httpGetExpect(client->http) &&
      (client->operation == HTTP_STATE_POST ||
       client->operation == HTTP_STATE_PUT))
  {
    if (httpGetExpect(client->http) == HTTP_STATUS_CONTINUE)
    {
     /*
      * Send 100-continue header...
      */

      if (!respond_http(client, HTTP_STATUS_CONTINUE, NULL, NULL, 0))
	return (0);
    }
    else
    {
     /*
      * Send 417-expectation-failed header...
      */

      if (!respond_http(client, HTTP_STATUS_EXPECTATION_FAILED, NULL, NULL, 0))
	return (0);
    }
  }

 /*
  * Handle new transfers...
  */

  switch (client->operation)
  {
    case HTTP_STATE_OPTIONS :
       /*
	* Do OPTIONS command...
	*/

	return (respond_http(client, HTTP_STATUS_OK, NULL, NULL, 0));

    case HTTP_STATE_HEAD :
        if (!strcmp(client->uri, "/icon.png"))
	  return (respond_http(client, HTTP_STATUS_OK, NULL, "image/png", 0));
	else if (!strcmp(client->uri, "/") || !strcmp(client->uri, "/materials"))
	  return (respond_http(client, HTTP_STATUS_OK, NULL, "text/html", 0));
	else
	  return (respond_http(client, HTTP_STATUS_NOT_FOUND, NULL, NULL, 0));

    case HTTP_STATE_GET :
        if (!strcmp(client->uri, "/icon.png"))
	{
	 /*
	  * Send PNG icon file.
	  */

          if (client->printer->icon)
          {
	    int		fd;		/* Icon file */
	    struct stat	fileinfo;	/* Icon file information */
	    char	buffer[4096];	/* Copy buffer */
	    ssize_t	bytes;		/* Bytes */

	    fprintf(stderr, "Icon file is \"%s\".\n", client->printer->icon);

	    if (!stat(client->printer->icon, &fileinfo) && (fd = open(client->printer->icon, O_RDONLY)) >= 0)
	    {
	      if (!respond_http(client, HTTP_STATUS_OK, NULL, "image/png", (size_t)fileinfo.st_size))
	      {
		close(fd);
		return (0);
	      }

	      while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
		httpWrite2(client->http, buffer, (size_t)bytes);

	      httpFlushWrite(client->http);

	      close(fd);
	    }
	    else
	      return (respond_http(client, HTTP_STATUS_NOT_FOUND, NULL, NULL, 0));
	  }
	  else
	  {
	    fputs("Icon file is internal printer.png.\n", stderr);

	    if (!respond_http(client, HTTP_STATUS_OK, NULL, "image/png", sizeof(printer3d_png)))
	      return (0);

            httpWrite2(client->http, (const char *)printer3d_png, sizeof(printer3d_png));
	    httpFlushWrite(client->http);
	  }
	}
	else if (!strcmp(client->uri, "/"))
	{
	 /*
	  * Show web status page...
	  */

          return (show_status(client));
	}
	else if (!strcmp(client->uri, "/materials"))
	{
	 /*
	  * Show web materials page...
	  */

          return (show_materials(client));
	}
	else
	  return (respond_http(client, HTTP_STATUS_NOT_FOUND, NULL, NULL, 0));
	break;

    case HTTP_STATE_POST :
	if (strcmp(httpGetField(client->http, HTTP_FIELD_CONTENT_TYPE), "application/ipp"))
        {
	 /*
	  * Not an IPP request...
	  */

	  return (respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0));
	}

       /*
        * Read the IPP request...
	*/

	client->request = ippNew();

        while ((ipp_state = ippRead(client->http, client->request)) != IPP_STATE_DATA)
	{
	  if (ipp_state == IPP_STATE_ERROR)
	  {
            fprintf(stderr, "%s IPP read error (%s).\n", client->hostname, cupsLastErrorString());
	    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
	    return (0);
	  }
	}

       /*
        * Now that we have the IPP request, process the request...
	*/

        return (process_ipp(client));

    default :
        break; /* Anti-compiler-warning-code */
  }

  return (1);
}


/*
 * 'process_ipp()' - Process an IPP request.
 */

static int				/* O - 1 on success, 0 on error */
process_ipp(ipp3d_client_t *client)	/* I - Client */
{
  ipp_tag_t		group;		/* Current group tag */
  ipp_attribute_t	*attr;		/* Current attribute */
  ipp_attribute_t	*charset;	/* Character set attribute */
  ipp_attribute_t	*language;	/* Language attribute */
  ipp_attribute_t	*uri;		/* Printer URI attribute */
  int			major, minor;	/* Version number */
  const char		*name;		/* Name of attribute */


  debug_attributes("Request", client->request, 1);

 /*
  * First build an empty response message for this request...
  */

  client->operation_id = ippGetOperation(client->request);
  client->response     = ippNewResponse(client->request);

 /*
  * Then validate the request header and required attributes...
  */

  major = ippGetVersion(client->request, &minor);

  if (major < 1 || major > 2)
  {
   /*
    * Return an error, since we only support IPP 1.x and 2.x.
    */

    respond_ipp(client, IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED, "Bad request version number %d.%d.", major, minor);
  }
  else if ((major * 10 + minor) > MaxVersion)
  {
    if (httpGetState(client->http) != HTTP_STATE_POST_SEND)
      httpFlush(client->http);		/* Flush trailing (junk) data */

    respond_http(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }
  else if (ippGetRequestId(client->request) <= 0)
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad request-id %d.", ippGetRequestId(client->request));
  }
  else if (!ippFirstAttribute(client->request))
  {
    respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST, "No attributes in request.");
  }
  else
  {
   /*
    * Make sure that the attributes are provided in the correct order and
    * don't repeat groups...
    */

    for (attr = ippFirstAttribute(client->request),
             group = ippGetGroupTag(attr);
	 attr;
	 attr = ippNextAttribute(client->request))
    {
      if (ippGetGroupTag(attr) < group && ippGetGroupTag(attr) != IPP_TAG_ZERO)
      {
       /*
	* Out of order; return an error...
	*/

	respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST,
		    "Attribute groups are out of order (%x < %x).",
		    ippGetGroupTag(attr), group);
	break;
      }
      else
	group = ippGetGroupTag(attr);
    }

    if (!attr)
    {
     /*
      * Then make sure that the first three attributes are:
      *
      *     attributes-charset
      *     attributes-natural-language
      *     printer-uri/job-uri
      */

      attr = ippFirstAttribute(client->request);
      name = ippGetName(attr);
      if (attr && name && !strcmp(name, "attributes-charset") &&
	  ippGetValueTag(attr) == IPP_TAG_CHARSET)
	charset = attr;
      else
	charset = NULL;

      attr = ippNextAttribute(client->request);
      name = ippGetName(attr);

      if (attr && name && !strcmp(name, "attributes-natural-language") &&
	  ippGetValueTag(attr) == IPP_TAG_LANGUAGE)
	language = attr;
      else
	language = NULL;

      if ((attr = ippFindAttribute(client->request, "printer-uri",
                                   IPP_TAG_URI)) != NULL)
	uri = attr;
      else if ((attr = ippFindAttribute(client->request, "job-uri",
                                        IPP_TAG_URI)) != NULL)
	uri = attr;
      else
	uri = NULL;

      if (charset &&
          strcasecmp(ippGetString(charset, 0, NULL), "us-ascii") &&
          strcasecmp(ippGetString(charset, 0, NULL), "utf-8"))
      {
       /*
        * Bad character set...
	*/

	respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Unsupported character set \"%s\".",
	            ippGetString(charset, 0, NULL));
      }
      else if (!charset || !language || !uri)
      {
       /*
	* Return an error, since attributes-charset,
	* attributes-natural-language, and printer-uri/job-uri are required
	* for all operations.
	*/

	respond_ipp(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Missing required attributes.");
      }
      else
      {
        char		scheme[32],	/* URI scheme */
			userpass[32],	/* Username/password in URI */
			host[256],	/* Host name in URI */
			resource[256];	/* Resource path in URI */
	int		port;		/* Port number in URI */

        name = ippGetName(uri);

        if (httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL),
                            scheme, sizeof(scheme),
                            userpass, sizeof(userpass),
                            host, sizeof(host), &port,
                            resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
	  respond_ipp(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES,
	              "Bad %s value '%s'.", name, ippGetString(uri, 0, NULL));
        else if ((!strcmp(name, "job-uri") &&
                  strncmp(resource, "/ipp/print3d/", 13)) ||
                 (!strcmp(name, "printer-uri") &&
                  strcmp(resource, "/ipp/print3d")))
	  respond_ipp(client, IPP_STATUS_ERROR_NOT_FOUND, "%s %s not found.",
		      name, ippGetString(uri, 0, NULL));
	else
	{
	 /*
	  * Try processing the operation...
	  */

	  switch (ippGetOperation(client->request))
	  {
	    case IPP_OP_VALIDATE_JOB :
		ipp_validate_job(client);
		break;

	    case IPP_OP_CREATE_JOB :
		ipp_create_job(client);
		break;

	    case IPP_OP_SEND_DOCUMENT :
		ipp_send_document(client);
		break;

	    case IPP_OP_SEND_URI :
		ipp_send_uri(client);
		break;

	    case IPP_OP_CANCEL_JOB :
		ipp_cancel_job(client);
		break;

	    case IPP_OP_GET_JOB_ATTRIBUTES :
		ipp_get_job_attributes(client);
		break;

	    case IPP_OP_GET_JOBS :
		ipp_get_jobs(client);
		break;

	    case IPP_OP_GET_PRINTER_ATTRIBUTES :
		ipp_get_printer_attributes(client);
		break;

	    case IPP_OP_CLOSE_JOB :
	        ipp_close_job(client);
		break;

	    case IPP_OP_IDENTIFY_PRINTER :
	        ipp_identify_printer(client);
		break;

	    default :
		respond_ipp(client, IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED,
			    "Operation not supported.");
		break;
	  }
	}
      }
    }
  }

 /*
  * Send the HTTP header and return...
  */

  if (httpGetState(client->http) != HTTP_STATE_POST_SEND)
    httpFlush(client->http);		/* Flush trailing (junk) data */

  return (respond_http(client, HTTP_STATUS_OK, NULL, "application/ipp",
                       ippLength(client->response)));
}


/*
 * 'process_job()' - Process a print job.
 */

static void *				/* O - Thread exit status */
process_job(ipp3d_job_t *job)		/* I - Job */
{
  job->state          = IPP_JSTATE_PROCESSING;
  job->printer->state = IPP_PSTATE_PROCESSING;
  job->processing     = time(NULL);

  while (job->printer->state_reasons & IPP3D_PREASON_MATERIAL_EMPTY)
  {
    job->printer->state_reasons |= IPP3D_PREASON_MATERIAL_NEEDED;

    sleep(1);
  }

  job->printer->state_reasons &= (ipp3d_preason_t)~IPP3D_PREASON_MATERIAL_NEEDED;

  if (job->printer->command)
  {
   /*
    * Execute a command with the job spool file and wait for it to complete...
    */

    int 		pid,		/* Process ID */
			status;		/* Exit status */
    struct timeval	start,		/* Start time */
			end;		/* End time */
    char		*myargv[3],	/* Command-line arguments */
			*myenvp[400];	/* Environment variables */
    int			myenvc;		/* Number of environment variables */
    ipp_attribute_t	*attr;		/* Job attribute */
    char		val[1280],	/* IPP_NAME=value */
			*valptr;	/* Pointer into string */
#ifndef _WIN32
    int			mystdout = -1;	/* File for stdout */
    int			mypipe[2];	/* Pipe for stderr */
    char		line[2048],	/* Line from stderr */
			*ptr,		/* Pointer into line */
			*endptr;	/* End of line */
    ssize_t		bytes;		/* Bytes read */
#endif /* !_WIN32 */

    fprintf(stderr, "[Job %d] Running command \"%s %s\".\n", job->id, job->printer->command, job->filename);
    gettimeofday(&start, NULL);

   /*
    * Setup the command-line arguments...
    */

    myargv[0] = job->printer->command;
    myargv[1] = job->filename;
    myargv[2] = NULL;

   /*
    * Copy the current environment, then add environment variables for every
    * Job attribute and Printer -default attributes...
    */

    for (myenvc = 0; environ[myenvc] && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); myenvc ++)
      myenvp[myenvc] = strdup(environ[myenvc]);

    if (myenvc > (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 32))
    {
      fprintf(stderr, "[Job %d] Too many environment variables to process job.\n", job->id);
      job->state = IPP_JSTATE_ABORTED;
      goto error;
    }

    snprintf(val, sizeof(val), "CONTENT_TYPE=%s", job->format);
    myenvp[myenvc ++] = strdup(val);

    if (job->printer->device_uri)
    {
      snprintf(val, sizeof(val), "DEVICE_URI=%s", job->printer->device_uri);
      myenvp[myenvc ++] = strdup(val);
    }

    for (attr = ippFirstAttribute(job->printer->attrs); attr && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); attr = ippNextAttribute(job->printer->attrs))
    {
     /*
      * Convert "attribute-name-default" to "IPP_ATTRIBUTE_NAME_DEFAULT=" and
      * "pwg-xxx" to "IPP_PWG_XXX", then add the value(s) from the attribute.
      */

      const char	*name = ippGetName(attr),
					/* Attribute name */
			*suffix = strstr(name, "-default");
					/* Suffix on attribute name */

      if (strncmp(name, "pwg-", 4) && (!suffix || suffix[8]))
        continue;

      valptr = val;
      *valptr++ = 'I';
      *valptr++ = 'P';
      *valptr++ = 'P';
      *valptr++ = '_';
      while (*name && valptr < (val + sizeof(val) - 2))
      {
        if (*name == '-')
	  *valptr++ = '_';
	else
	  *valptr++ = (char)toupper(*name & 255);

	name ++;
      }
      *valptr++ = '=';
      ippAttributeString(attr, valptr, sizeof(val) - (size_t)(valptr - val));

      myenvp[myenvc++] = strdup(val);
    }

    for (attr = ippFirstAttribute(job->attrs); attr && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); attr = ippNextAttribute(job->attrs))
    {
     /*
      * Convert "attribute-name" to "IPP_ATTRIBUTE_NAME=" and then add the
      * value(s) from the attribute.
      */

      const char *name = ippGetName(attr);
					/* Attribute name */

      if (!name)
        continue;

      valptr = val;
      *valptr++ = 'I';
      *valptr++ = 'P';
      *valptr++ = 'P';
      *valptr++ = '_';
      while (*name && valptr < (val + sizeof(val) - 2))
      {
        if (*name == '-')
	  *valptr++ = '_';
	else
	  *valptr++ = (char)toupper(*name & 255);

	name ++;
      }
      *valptr++ = '=';
      ippAttributeString(attr, valptr, sizeof(val) - (size_t)(valptr - val));

      myenvp[myenvc++] = strdup(val);
    }

    if (attr)
    {
      fprintf(stderr, "[Job %d] Too many environment variables to process job.\n", job->id);
      job->state = IPP_JSTATE_ABORTED;
      goto error;
    }

    myenvp[myenvc] = NULL;

   /*
    * Now run the program...
    */

#ifdef _WIN32
    status = _spawnvpe(_P_WAIT, job->printer->command, myargv, myenvp);

#else
    if (job->printer->device_uri)
    {
      char	scheme[32],		/* URI scheme */
		userpass[256],		/* username:password (unused) */
		host[256],		/* Hostname or IP address */
		resource[256];		/* Resource path */
      int	port;			/* Port number */


      if (httpSeparateURI(HTTP_URI_CODING_ALL, job->printer->device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
      {
        fprintf(stderr, "[Job %d] Bad device URI \"%s\".\n", job->id, job->printer->device_uri);
      }
      else if (!strcmp(scheme, "file"))
      {
        struct stat	fileinfo;	/* See if this is a file or directory... */

        if (stat(resource, &fileinfo))
        {
          if (errno == ENOENT)
          {
            if ((mystdout = open(resource, O_WRONLY | O_CREAT | O_TRUNC, 0666)) >= 0)
	      fprintf(stderr, "[Job %d] Saving print command output to \"%s\".\n", job->id, resource);
	    else
	      fprintf(stderr, "[Job %d] Unable to create \"%s\": %s\n", job->id, resource, strerror(errno));
          }
          else
            fprintf(stderr, "[Job %d] Unable to access \"%s\": %s\n", job->id, resource, strerror(errno));
        }
        else if (S_ISDIR(fileinfo.st_mode))
        {
          if ((mystdout = create_job_file(job, line, sizeof(line), resource, "prn")) >= 0)
	    fprintf(stderr, "[Job %d] Saving print command output to \"%s\".\n", job->id, line);
          else
            fprintf(stderr, "[Job %d] Unable to create \"%s\": %s\n", job->id, line, strerror(errno));
        }
	else if (!S_ISREG(fileinfo.st_mode))
	{
	  if ((mystdout = open(resource, O_WRONLY | O_CREAT | O_TRUNC, 0666)) >= 0)
	    fprintf(stderr, "[Job %d] Saving print command output to \"%s\".\n", job->id, resource);
	  else
            fprintf(stderr, "[Job %d] Unable to create \"%s\": %s\n", job->id, resource, strerror(errno));
	}
        else if ((mystdout = open(resource, O_WRONLY)) >= 0)
	  fprintf(stderr, "[Job %d] Saving print command output to \"%s\".\n", job->id, resource);
	else
	  fprintf(stderr, "[Job %d] Unable to open \"%s\": %s\n", job->id, resource, strerror(errno));
      }
      else if (!strcmp(scheme, "socket"))
      {
        http_addrlist_t	*addrlist;	/* List of addresses */
        char		service[32];	/* Service number */

        snprintf(service, sizeof(service), "%d", port);

        if ((addrlist = httpAddrGetList(host, AF_UNSPEC, service)) == NULL)
          fprintf(stderr, "[Job %d] Unable to find \"%s\": %s\n", job->id, host, cupsLastErrorString());
        else if (!httpAddrConnect2(addrlist, &mystdout, 30000, &(job->cancel)))
          fprintf(stderr, "[Job %d] Unable to connect to \"%s\": %s\n", job->id, host, cupsLastErrorString());

        httpAddrFreeList(addrlist);
      }
      else
      {
        fprintf(stderr, "[Job %d] Unsupported device URI scheme \"%s\".\n", job->id, scheme);
      }
    }
    else if ((mystdout = create_job_file(job, line, sizeof(line), job->printer->directory, "prn")) >= 0)
    {
      fprintf(stderr, "[Job %d] Saving print command output to \"%s\".\n", job->id, line);
    }

    if (mystdout < 0)
      mystdout = open("/dev/null", O_WRONLY);

    if (pipe(mypipe))
    {
      fprintf(stderr, "[Job %d] Unable to create pipe for stderr: %s\n", job->id, strerror(errno));
      mypipe[0] = mypipe[1] = -1;
    }

    if ((pid = fork()) == 0)
    {
     /*
      * Child comes here...
      */

      close(1);
      dup2(mystdout, 1);
      close(mystdout);

      close(2);
      dup2(mypipe[1], 2);
      close(mypipe[0]);
      close(mypipe[1]);

      execve(job->printer->command, myargv, myenvp);
      exit(errno);
    }
    else if (pid < 0)
    {
     /*
      * Unable to fork process...
      */

      fprintf(stderr, "[Job %d] Unable to start job processing command: %s\n", job->id, strerror(errno));
      status = -1;

      close(mystdout);
      close(mypipe[0]);
      close(mypipe[1]);

     /*
      * Free memory used for environment...
      */

      while (myenvc > 0)
	free(myenvp[-- myenvc]);
    }
    else
    {
     /*
      * Free memory used for environment...
      */

      while (myenvc > 0)
	free(myenvp[-- myenvc]);

     /*
      * Close the output file in the parent process...
      */

      close(mystdout);

     /*
      * If the pipe exists, read from it until EOF...
      */

      if (mypipe[0] >= 0)
      {
	close(mypipe[1]);

	endptr = line;
	while ((bytes = read(mypipe[0], endptr, sizeof(line) - (size_t)(endptr - line) - 1)) > 0)
	{
	  endptr += bytes;
	  *endptr = '\0';

          while ((ptr = strchr(line, '\n')) != NULL)
	  {
	    int level = 3;		/* Message log level */

	    *ptr++ = '\0';

	    if (!strncmp(line, "ATTR:", 5))
	    {
	     /*
	      * Process job/printer attribute updates.
	      */

	      process_attr_message(job, line);
	    }
	    else if (!strncmp(line, "DEBUG:", 6))
	    {
	     /*
	      * Debug message...
	      */

              level = 2;
	    }
	    else if (!strncmp(line, "ERROR:", 6))
	    {
	     /*
	      * Error message...
	      */

              level         = 0;
              job->message  = strdup(line + 6);
              job->msglevel = 0;
	    }
	    else if (!strncmp(line, "INFO:", 5))
	    {
	     /*
	      * Informational/progress message...
	      */

              level = 1;
              if (job->msglevel)
              {
                job->message  = strdup(line + 5);
                job->msglevel = 1;
	      }
	    }
	    else if (!strncmp(line, "STATE:", 6))
	    {
	     /*
	      * Process printer-state-reasons keywords.
	      */

	      process_state_message(job, line);
	    }

	    if (Verbosity >= level)
	      fprintf(stderr, "[Job %d] Command - %s\n", job->id, line);

	    bytes = ptr - line;
            if (ptr < endptr)
	      memmove(line, ptr, (size_t)(endptr - ptr));
	    endptr -= bytes;
	    *endptr = '\0';
	  }
	}

	close(mypipe[0]);
      }

     /*
      * Wait for child to complete...
      */

#  ifdef HAVE_WAITPID
      while (waitpid(pid, &status, 0) < 0);
#  else
      while (wait(&status) < 0);
#  endif /* HAVE_WAITPID */
    }
#endif /* _WIN32 */

    if (status)
    {
#ifndef _WIN32
      if (WIFEXITED(status))
#endif /* !_WIN32 */
	fprintf(stderr, "[Job %d] Command \"%s\" exited with status %d.\n", job->id,  job->printer->command, WEXITSTATUS(status));
#ifndef _WIN32
      else
	fprintf(stderr, "[Job %d] Command \"%s\" terminated with signal %d.\n", job->id, job->printer->command, WTERMSIG(status));
#endif /* !_WIN32 */
      job->state = IPP_JSTATE_ABORTED;
    }
    else if (status < 0)
      job->state = IPP_JSTATE_ABORTED;
    else
      fprintf(stderr, "[Job %d] Command \"%s\" completed successfully.\n", job->id, job->printer->command);

   /*
    * Report the total processing time...
    */

    gettimeofday(&end, NULL);

    fprintf(stderr, "[Job %d] Processing time was %.3f seconds.\n", job->id, end.tv_sec - start.tv_sec + 0.000001 * (end.tv_usec - start.tv_usec));
  }
  else
  {
   /*
    * Sleep for a random amount of time to simulate job processing.
    */

    sleep((unsigned)(5 + (CUPS_RAND() % 11)));
  }

  if (job->cancel)
    job->state = IPP_JSTATE_CANCELED;
  else if (job->state == IPP_JSTATE_PROCESSING)
    job->state = IPP_JSTATE_COMPLETED;

  error:

  job->completed           = time(NULL);
  job->printer->state      = IPP_PSTATE_IDLE;
  job->printer->active_job = NULL;

  return (NULL);
}


/*
 * 'process_state_message()' - Process a STATE: message from a command.
 */

static void
process_state_message(
    ipp3d_job_t *job,			/* I - Job */
    char       *message)		/* I - Message */
{
  int		i;			/* Looping var */
  ipp3d_preason_t state_reasons,		/* printer-state-reasons values */
		bit;			/* Current reason bit */
  char		*ptr,			/* Pointer into message */
		*next;			/* Next keyword in message */
  int		remove;			/* Non-zero if we are removing keywords */


 /*
  * Skip leading "STATE:" and any whitespace...
  */

  for (message += 6; *message; message ++)
    if (*message != ' ' && *message != '\t')
      break;

 /*
  * Support the following forms of message:
  *
  * "keyword[,keyword,...]" to set the printer-state-reasons value(s).
  *
  * "-keyword[,keyword,...]" to remove keywords.
  *
  * "+keyword[,keyword,...]" to add keywords.
  *
  * Keywords may or may not have a suffix (-report, -warning, -error) per
  * RFC 8011.
  */

  if (*message == '-')
  {
    remove        = 1;
    state_reasons = job->printer->state_reasons;
    message ++;
  }
  else if (*message == '+')
  {
    remove        = 0;
    state_reasons = job->printer->state_reasons;
    message ++;
  }
  else
  {
    remove        = 0;
    state_reasons = IPP3D_PREASON_NONE;
  }

  while (*message)
  {
    if ((next = strchr(message, ',')) != NULL)
      *next++ = '\0';

    if ((ptr = strstr(message, "-error")) != NULL)
      *ptr = '\0';
    else if ((ptr = strstr(message, "-report")) != NULL)
      *ptr = '\0';
    else if ((ptr = strstr(message, "-warning")) != NULL)
      *ptr = '\0';

    for (i = 0, bit = 1; i < (int)(sizeof(ipp3d_preason_strings) / sizeof(ipp3d_preason_strings[0])); i ++, bit *= 2)
    {
      if (!strcmp(message, ipp3d_preason_strings[i]))
      {
        if (remove)
	  state_reasons &= ~bit;
	else
	  state_reasons |= bit;
      }
    }

    if (next)
      message = next;
    else
      break;
  }

  job->printer->state_reasons = state_reasons;
}


/*
 * 'register_printer()' - Register a printer object via Bonjour.
 */

static int				/* O - 1 on success, 0 on error */
register_printer(
    ipp3d_printer_t *printer,		/* I - Printer */
    const char       *subtypes)		/* I - Service subtype(s) */
{
#if defined(HAVE_DNSSD) || defined(HAVE_AVAHI)
  ipp3d_txt_t		ipp_txt;	/* Bonjour IPP TXT record */
  int			i,		/* Looping var */
			count;		/* Number of values */
  ipp_attribute_t	*document_format_supported,
			*printer_location,
			*printer_make_and_model,
			*printer_more_info,
			*printer_uuid;
  const char		*value;		/* Value string */
  char			formats[252],	/* List of supported formats */
			*ptr;		/* Pointer into string */

  document_format_supported = ippFindAttribute(printer->attrs, "document-format-supported", IPP_TAG_MIMETYPE);
  printer_location          = ippFindAttribute(printer->attrs, "printer-location", IPP_TAG_TEXT);
  printer_make_and_model    = ippFindAttribute(printer->attrs, "printer-make-and-model", IPP_TAG_TEXT);
  printer_more_info         = ippFindAttribute(printer->attrs, "printer-more-info", IPP_TAG_URI);
  printer_uuid              = ippFindAttribute(printer->attrs, "printer-uuid", IPP_TAG_URI);

  for (i = 0, count = ippGetCount(document_format_supported), ptr = formats; i < count; i ++)
  {
    value = ippGetString(document_format_supported, i, NULL);

    if (!strcasecmp(value, "application/octet-stream"))
      continue;

    if (ptr > formats && ptr < (formats + sizeof(formats) - 1))
      *ptr++ = ',';

    strlcpy(ptr, value, sizeof(formats) - (size_t)(ptr - formats));
    ptr += strlen(ptr);

    if (ptr >= (formats + sizeof(formats) - 1))
      break;
  }

#endif /* HAVE_DNSSD || HAVE_AVAHI */
#ifdef HAVE_DNSSD
  DNSServiceErrorType	error;		/* Error from Bonjour */
  char			regtype[256];	/* Bonjour service type */


 /*
  * Build the TXT record for IPP...
  */

  TXTRecordCreate(&ipp_txt, 1024, NULL);
  TXTRecordSetValue(&ipp_txt, "rp", 9, "ipp/print3d");
  if ((value = ippGetString(printer_make_and_model, 0, NULL)) != NULL)
    TXTRecordSetValue(&ipp_txt, "ty", (uint8_t)strlen(value), value);
  if ((value = ippGetString(printer_more_info, 0, NULL)) != NULL)
    TXTRecordSetValue(&ipp_txt, "adminurl", (uint8_t)strlen(value), value);
  if ((value = ippGetString(printer_location, 0, NULL)) != NULL)
    TXTRecordSetValue(&ipp_txt, "note", (uint8_t)strlen(value), value);
  TXTRecordSetValue(&ipp_txt, "pdl", (uint8_t)strlen(formats), formats);
  if ((value = ippGetString(printer_uuid, 0, NULL)) != NULL)
    TXTRecordSetValue(&ipp_txt, "UUID", (uint8_t)strlen(value) - 9, value + 9);
#  ifdef HAVE_SSL
  TXTRecordSetValue(&ipp_txt, "TLS", 3, "1.2");
#  endif /* HAVE_SSL */
  TXTRecordSetValue(&ipp_txt, "txtvers", 1, "1");
  TXTRecordSetValue(&ipp_txt, "qtotal", 1, "1");

 /*
  * Register the _printer._tcp (LPD) service type with a port number of 0 to
  * defend our service name but not actually support LPD...
  */

  printer->printer_ref = DNSSDMaster;

  if ((error = DNSServiceRegister(&(printer->printer_ref), kDNSServiceFlagsShareConnection, 0 /* interfaceIndex */, printer->dns_sd_name, "_printer._tcp", NULL /* domain */, NULL /* host */, 0 /* port */, 0 /* txtLen */, NULL /* txtRecord */, (DNSServiceRegisterReply)dnssd_callback, printer)) != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("Unable to register \"%s.%s\": %d"), printer->dns_sd_name, "_printer._tcp", error);
    return (0);
  }

 /*
  * Then register the _ipp-3d._tcp (IPP) service type with the real port number
  * to advertise our IPP printer...
  */

  printer->ipp_ref = DNSSDMaster;

  if (subtypes && *subtypes)
    snprintf(regtype, sizeof(regtype), "_ipp-3d._tcp,%s", subtypes);
  else
    strlcpy(regtype, "_ipp-3d._tcp", sizeof(regtype));

  if ((error = DNSServiceRegister(&(printer->ipp_ref), kDNSServiceFlagsShareConnection, 0 /* interfaceIndex */, printer->dns_sd_name, regtype, NULL /* domain */, NULL /* host */, htons(printer->port), TXTRecordGetLength(&ipp_txt), TXTRecordGetBytesPtr(&ipp_txt), (DNSServiceRegisterReply)dnssd_callback, printer)) != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("Unable to register \"%s.%s\": %d"), printer->dns_sd_name, regtype, error);
    return (0);
  }

#  ifdef HAVE_SSL
 /*
  * Then register the _ipps._tcp (IPP) service type with the real port number to
  * advertise our IPPS printer...
  */

  printer->ipps_ref = DNSSDMaster;

  if (subtypes && *subtypes)
    snprintf(regtype, sizeof(regtype), "_ipps-3d._tcp,%s", subtypes);
  else
    strlcpy(regtype, "_ipps-3d._tcp", sizeof(regtype));

  if ((error = DNSServiceRegister(&(printer->ipps_ref), kDNSServiceFlagsShareConnection, 0 /* interfaceIndex */, printer->dns_sd_name, regtype, NULL /* domain */, NULL /* host */, htons(printer->port), TXTRecordGetLength(&ipp_txt), TXTRecordGetBytesPtr(&ipp_txt), (DNSServiceRegisterReply)dnssd_callback, printer)) != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("Unable to register \"%s.%s\": %d"), printer->dns_sd_name, regtype, error);
    return (0);
  }
#  endif /* HAVE_SSL */

 /*
  * Similarly, register the _http._tcp,_printer (HTTP) service type with the
  * real port number to advertise our IPP printer...
  */

  printer->http_ref = DNSSDMaster;

  if ((error = DNSServiceRegister(&(printer->http_ref), kDNSServiceFlagsShareConnection, 0 /* interfaceIndex */, printer->dns_sd_name, "_http._tcp,_printer", NULL /* domain */, NULL /* host */, htons(printer->port), 0 /* txtLen */, NULL /* txtRecord */, (DNSServiceRegisterReply)dnssd_callback, printer)) != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("Unable to register \"%s.%s\": %d"), printer->dns_sd_name, "_http._tcp,_printer", error);
    return (0);
  }

  TXTRecordDeallocate(&ipp_txt);

#elif defined(HAVE_AVAHI)
  char		temp[256];		/* Subtype service string */

 /*
  * Create the TXT record...
  */

  ipp_txt = NULL;
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "rp=ipp/print");
  if ((value = ippGetString(printer_make_and_model, 0, NULL)) != NULL)
    ipp_txt = avahi_string_list_add_printf(ipp_txt, "ty=%s", value);
  if ((value = ippGetString(printer_more_info, 0, NULL)) != NULL)
    ipp_txt = avahi_string_list_add_printf(ipp_txt, "adminurl=%s", value);
  if ((value = ippGetString(printer_location, 0, NULL)) != NULL)
    ipp_txt = avahi_string_list_add_printf(ipp_txt, "note=%s", value);
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "pdl=%s", formats);
  if ((value = ippGetString(printer_uuid, 0, NULL)) != NULL)
    ipp_txt = avahi_string_list_add_printf(ipp_txt, "UUID=%s", value + 9);
#  ifdef HAVE_SSL
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "TLS=1.2");
#  endif /* HAVE_SSL */
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "txtvers=1");
  ipp_txt = avahi_string_list_add_printf(ipp_txt, "qtotal=1");

 /*
  * Register _printer._tcp (LPD) with port 0 to reserve the service name...
  */

  avahi_threaded_poll_lock(DNSSDMaster);

  printer->ipp_ref = avahi_entry_group_new(DNSSDClient, dnssd_callback, NULL);

  avahi_entry_group_add_service_strlst(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_printer._tcp", NULL, NULL, 0, NULL);

 /*
  * Then register the _ipp-3d._tcp (IPP)...
  */

  avahi_entry_group_add_service_strlst(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_ipp._tcp", NULL, NULL, printer->port, ipp_txt);
  if (subtypes && *subtypes)
  {
    char *temptypes = strdup(subtypes), *start, *end;

    for (start = temptypes; *start; start = end)
    {
      if ((end = strchr(start, ',')) != NULL)
        *end++ = '\0';
      else
        end = start + strlen(start);

      snprintf(temp, sizeof(temp), "%s._sub._ipp-3d._tcp", start);
      avahi_entry_group_add_service_subtype(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_ipp-3d._tcp", NULL, temp);
    }

    free(temptypes);
  }

#ifdef HAVE_SSL
 /*
  * _ipps-3d._tcp (IPPS) for secure printing...
  */

  avahi_entry_group_add_service_strlst(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_ipps-3d._tcp", NULL, NULL, printer->port, ipp_txt);
  if (subtypes && *subtypes)
  {
    char *temptypes = strdup(subtypes), *start, *end;

    for (start = temptypes; *start; start = end)
    {
      if ((end = strchr(start, ',')) != NULL)
        *end++ = '\0';
      else
        end = start + strlen(start);

      snprintf(temp, sizeof(temp), "%s._sub._ipps-3d._tcp", start);
      avahi_entry_group_add_service_subtype(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_ipps-3d._tcp", NULL, temp);
    }

    free(temptypes);
  }
#endif /* HAVE_SSL */

 /*
  * Finally _http.tcp (HTTP) for the web interface...
  */

  avahi_entry_group_add_service_strlst(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_http._tcp", NULL, NULL, printer->port, NULL);
  avahi_entry_group_add_service_subtype(printer->ipp_ref, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, printer->dns_sd_name, "_http._tcp", NULL, "_printer._sub._http._tcp");

 /*
  * Commit it...
  */

  avahi_entry_group_commit(printer->ipp_ref);
  avahi_threaded_poll_unlock(DNSSDMaster);

  avahi_string_list_free(ipp_txt);
#endif /* HAVE_DNSSD */

  return (1);
}


/*
 * 'respond_http()' - Send a HTTP response.
 */

int					/* O - 1 on success, 0 on failure */
respond_http(
    ipp3d_client_t *client,		/* I - Client */
    http_status_t  code,		/* I - HTTP status of response */
    const char     *content_encoding,	/* I - Content-Encoding of response */
    const char     *type,		/* I - MIME media type of response */
    size_t         length)		/* I - Length of response */
{
  char	message[1024];			/* Text message */


  fprintf(stderr, "%s %s\n", client->hostname, httpStatus(code));

  if (code == HTTP_STATUS_CONTINUE)
  {
   /*
    * 100-continue doesn't send any headers...
    */

    return (httpWriteResponse(client->http, HTTP_STATUS_CONTINUE) == 0);
  }

 /*
  * Format an error message...
  */

  if (!type && !length && code != HTTP_STATUS_OK && code != HTTP_STATUS_SWITCHING_PROTOCOLS)
  {
    snprintf(message, sizeof(message), "%d - %s\n", code, httpStatus(code));

    type   = "text/plain";
    length = strlen(message);
  }
  else
    message[0] = '\0';

 /*
  * Send the HTTP response header...
  */

  httpClearFields(client->http);

  if (code == HTTP_STATUS_METHOD_NOT_ALLOWED ||
      client->operation == HTTP_STATE_OPTIONS)
    httpSetField(client->http, HTTP_FIELD_ALLOW, "GET, HEAD, OPTIONS, POST");

  if (type)
  {
    if (!strcmp(type, "text/html"))
      httpSetField(client->http, HTTP_FIELD_CONTENT_TYPE,
                   "text/html; charset=utf-8");
    else
      httpSetField(client->http, HTTP_FIELD_CONTENT_TYPE, type);

    if (content_encoding)
      httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, content_encoding);
  }

  httpSetLength(client->http, length);

  if (httpWriteResponse(client->http, code) < 0)
    return (0);

 /*
  * Send the response data...
  */

  if (message[0])
  {
   /*
    * Send a plain text message.
    */

    if (httpPrintf(client->http, "%s", message) < 0)
      return (0);

    if (httpWrite2(client->http, "", 0) < 0)
      return (0);
  }
  else if (client->response)
  {
   /*
    * Send an IPP response...
    */

    debug_attributes("Response", client->response, 2);

    ippSetState(client->response, IPP_STATE_IDLE);

    if (ippWrite(client->http, client->response) != IPP_STATE_DATA)
      return (0);
  }

  return (1);
}


/*
 * 'respond_ipp()' - Send an IPP response.
 */

static void
respond_ipp(ipp3d_client_t *client,	/* I - Client */
            ipp_status_t  status,	/* I - status-code */
	    const char    *message,	/* I - printf-style status-message */
	    ...)			/* I - Additional args as needed */
{
  const char	*formatted = NULL;	/* Formatted message */


  ippSetStatusCode(client->response, status);

  if (message)
  {
    va_list		ap;		/* Pointer to additional args */
    ipp_attribute_t	*attr;		/* New status-message attribute */

    va_start(ap, message);
    if ((attr = ippFindAttribute(client->response, "status-message", IPP_TAG_TEXT)) != NULL)
      ippSetStringfv(client->response, &attr, 0, message, ap);
    else
      attr = ippAddStringfv(client->response, IPP_TAG_OPERATION, IPP_TAG_TEXT, "status-message", NULL, message, ap);
    va_end(ap);

    formatted = ippGetString(attr, 0, NULL);
  }

  if (formatted)
    fprintf(stderr, "%s %s %s (%s)\n", client->hostname, ippOpString(client->operation_id), ippErrorString(status), formatted);
  else
    fprintf(stderr, "%s %s %s\n", client->hostname, ippOpString(client->operation_id), ippErrorString(status));
}


/*
 * 'respond_unsupported()' - Respond with an unsupported attribute.
 */

static void
respond_unsupported(
    ipp3d_client_t   *client,		/* I - Client */
    ipp_attribute_t *attr)		/* I - Atribute */
{
  ipp_attribute_t	*temp;		/* Copy of attribute */


  respond_ipp(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Unsupported %s %s%s value.", ippGetName(attr), ippGetCount(attr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attr)));

  temp = ippCopyAttribute(client->response, attr, 0);
  ippSetGroupTag(client->response, &temp, IPP_TAG_UNSUPPORTED_GROUP);
}


/*
 * 'run_printer()' - Run the printer service.
 */

static void
run_printer(ipp3d_printer_t *printer)	/* I - Printer */
{
  int		num_fds;		/* Number of file descriptors */
  struct pollfd	polldata[3];		/* poll() data */
  int		timeout;		/* Timeout for poll() */
  ipp3d_client_t	*client;		/* New client */


 /*
  * Setup poll() data for the Bonjour service socket and IPv4/6 listeners...
  */

  polldata[0].fd     = printer->ipv4;
  polldata[0].events = POLLIN;

  polldata[1].fd     = printer->ipv6;
  polldata[1].events = POLLIN;

  num_fds = 2;

#ifdef HAVE_DNSSD
  polldata[num_fds   ].fd     = DNSServiceRefSockFD(DNSSDMaster);
  polldata[num_fds ++].events = POLLIN;
#endif /* HAVE_DNSSD */

 /*
  * Loop until we are killed or have a hard error...
  */

  for (;;)
  {
    if (cupsArrayCount(printer->jobs))
      timeout = 10;
    else
      timeout = -1;

    if (poll(polldata, (nfds_t)num_fds, timeout) < 0 && errno != EINTR)
    {
      perror("poll() failed");
      break;
    }

    if (polldata[0].revents & POLLIN)
    {
      if ((client = create_client(printer, printer->ipv4)) != NULL)
      {
        _cups_thread_t t = _cupsThreadCreate((_cups_thread_func_t)process_client, client);

        if (t)
        {
          _cupsThreadDetach(t);
        }
        else
	{
	  perror("Unable to create client thread");
	  delete_client(client);
	}
      }
    }

    if (polldata[1].revents & POLLIN)
    {
      if ((client = create_client(printer, printer->ipv6)) != NULL)
      {
        _cups_thread_t t = _cupsThreadCreate((_cups_thread_func_t)process_client, client);

        if (t)
        {
          _cupsThreadDetach(t);
        }
        else
	{
	  perror("Unable to create client thread");
	  delete_client(client);
	}
      }
    }

#ifdef HAVE_DNSSD
    if (polldata[2].revents & POLLIN)
      DNSServiceProcessResult(DNSSDMaster);
#endif /* HAVE_DNSSD */

   /*
    * Clean out old jobs...
    */

    clean_jobs(printer);
  }
}


/*
 * 'show_materials()' - Show material load state.
 */

static int				/* O - 1 on success, 0 on failure */
show_materials(ipp3d_client_t  *client)	/* I - Client connection */
{
  ipp3d_printer_t *printer = client->printer;
					/* Printer */
  int			i, j,		/* Looping vars */
			count;		/* Number of values */
  ipp_attribute_t	*materials_db,	/* materials-col-database attribute */
			*materials_ready,/* materials-col-ready attribute */
                        *attr;		/* Other attribute */
  ipp_t			*materials_col;	/* materials-col-xxx value */
  const char            *material_name,	/* materials-col-database material-name value */
                        *material_key,	/* materials-col-database material-key value */
                        *ready_key,	/* materials-col-ready material-key value */
                        *ready_name;	/* materials-col-ready marterial-name value */
  int			max_materials;	/* max-materials-col-supported value */
  int			num_options = 0;/* Number of form options */
  cups_option_t		*options = NULL;/* Form options */


 /*
  * Grab the available, ready, and number of materials from the printer.
  */

  if (!respond_http(client, HTTP_STATUS_OK, NULL, "text/html", 0))
    return (0);

  html_header(client, printer->dns_sd_name, 0);

  html_printf(client, "<p class=\"buttons\"><a class=\"button\" href=\"/\">Show Jobs</a></p>\n");
  html_printf(client, "<h1><img align=\"left\" src=\"/icon.png\" width=\"64\" height=\"64\">%s Materials</h1>\n", printer->dns_sd_name);

  if ((materials_db = ippFindAttribute(printer->attrs, "materials-col-database", IPP_TAG_BEGIN_COLLECTION)) == NULL)
  {
    html_printf(client, "<p>Error: No materials-col-database defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((materials_ready = ippFindAttribute(printer->attrs, "materials-col-ready", IPP_TAG_ZERO)) == NULL)
  {
    html_printf(client, "<p>Error: No materials-col-ready defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((attr = ippFindAttribute(printer->attrs, "max-materials-col-supported", IPP_TAG_INTEGER)) == NULL)
  {
    html_printf(client, "<p>Error: No max-materials-col-supported defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  max_materials = ippGetInteger(attr, 0);

 /*
  * Process form data if present...
  */

  if (printer->web_forms)
    num_options = parse_options(client, &options);

  if (num_options > 0)
  {
   /*
    * WARNING: A real printer/server implementation MUST NOT implement
    * material updates via a GET request - GET requests are supposed to be
    * idempotent (without side-effects) and we obviously are not
    * authenticating access here.  This form is provided solely to
    * enable testing and development!
    */

    char	name[255];		/* Form name */
    const char	*val;			/* Form value */

    _cupsRWLockWrite(&printer->rwlock);

    ippDeleteAttribute(printer->attrs, materials_ready);
    materials_ready = NULL;

    for (i = 0; i < max_materials; i ++)
    {
      snprintf(name, sizeof(name), "material%d", i);
      if ((val = cupsGetOption(name, num_options, options)) == NULL || !*val)
        continue;

      for (j = 0, count = ippGetCount(materials_db); j < count; j ++)
      {
        materials_col = ippGetCollection(materials_db, j);
        material_key  = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);

        if (!strcmp(material_key, val))
        {
          if (!materials_ready)
            materials_ready = ippAddCollection(printer->attrs, IPP_TAG_PRINTER, "materials-col-ready", materials_col);
          else
            ippSetCollection(printer->attrs, &materials_ready, ippGetCount(materials_ready), materials_col);
          break;
        }
      }
    }

    if (!materials_ready)
      materials_ready = ippAddOutOfBand(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "materials-col-ready");

    _cupsRWUnlock(&printer->rwlock);
  }

 /*
  * Show the currently loaded materials and allow the user to make selections...
  */

  if (printer->web_forms)
    html_printf(client, "<form method=\"GET\" action=\"/materials\">\n");

  html_printf(client, "<table class=\"form\" summary=\"Materials\">\n");

  for (i = 0; i < max_materials; i ++)
  {
    materials_col = ippGetCollection(materials_ready, i);
    ready_key     = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);

    html_printf(client, "<tr><th>Material %d:</th>", i + 1);
    if (printer->web_forms)
    {
      html_printf(client, "<td><select name=\"material%d\"><option value=\"\">None</option>", i);
      for (j = 0, count = ippGetCount(materials_db); j < count; j ++)
      {
	materials_col = ippGetCollection(materials_db, j);
	material_key  = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);
	material_name = ippGetString(ippFindAttribute(materials_col, "material-name", IPP_TAG_NAME), 0, NULL);

	if (material_key && material_name)
	  html_printf(client, "<option value=\"%s\"%s>%s</option>", material_key, ready_key && material_key && !strcmp(ready_key, material_key) ? " selected" : "", material_name);
	else if (material_key)
	  html_printf(client, "<!-- Error: no material-name for material-key=\"%s\" -->", material_key);
	else if (material_name)
	  html_printf(client, "<!-- Error: no material-key for material-name=\"%s\" -->", material_name);
	else
	  html_printf(client, "<!-- Error: no material-key or material-name for materials-col-database[%d] -->", j + 1);
      }
      html_printf(client, "</select></td></tr>\n");
    }
    else if ((ready_name = ippGetString(ippFindAttribute(materials_col, "material-name", IPP_TAG_ZERO), 0, NULL)) != NULL)
      html_printf(client, "%s</td></tr>\n", ready_name);
    else if (ready_key)
      html_printf(client, "%s</td></tr>\n", ready_key);
    else
      html_printf(client, "None</td></tr>\n");
  }

  if (printer->web_forms)
  {
    html_printf(client, "<tr><td></td><td><input type=\"submit\" value=\"Update Materials\">");
    if (num_options > 0)
      html_printf(client, " <span class=\"badge\" id=\"status\">Material updated.</span>\n");
    html_printf(client, "</td></tr></table></form>\n");

    if (num_options > 0)
      html_printf(client, "<script>\n"
			  "setTimeout(hide_status, 3000);\n"
			  "function hide_status() {\n"
			  "  var status = document.getElementById('status');\n"
			  "  status.style.display = 'none';\n"
			  "}\n"
			  "</script>\n");
  }
  else
    html_printf(client, "</table>\n");

  html_footer(client);

  return (1);
}


/*
 * 'show_status()' - Show printer/system state.
 */

static int				/* O - 1 on success, 0 on failure */
show_status(ipp3d_client_t  *client)	/* I - Client connection */
{
  ipp3d_printer_t *printer = client->printer;
					/* Printer */
  ipp3d_job_t		*job;		/* Current job */
  int			i;		/* Looping var */
  ipp3d_preason_t	reason;		/* Current reason */
  static const char * const reasons[] =	/* Reason strings */
  {
    "Other",
    "Moving to Paused",
    "Paused",
    "Spool Area Full",
    "Chamber Heating",
    "Cover Open",
    "Extruder Heating",
    "Fan Failure",
    "Material Empty",
    "Material Low",
    "Material Needed",
    "Motor Failure",
    "Platform Heating"
  };
  static const char * const state_colors[] =
  {					/* State colors */
    "#0C0",				/* Idle */
    "#EE0",				/* Processing */
    "#C00"				/* Stopped */
  };


  if (!respond_http(client, HTTP_STATUS_OK, NULL, "text/html", 0))
    return (0);

  html_header(client, printer->name, printer->state == IPP_PSTATE_PROCESSING ? 5 : 15);
  html_printf(client, "<h1><img style=\"background: %s; border-radius: 10px; float: left; margin-right: 10px; padding: 10px;\" src=\"/icon.png\" width=\"64\" height=\"64\">%s Jobs</h1>\n", state_colors[printer->state - IPP_PSTATE_IDLE], printer->name);
  html_printf(client, "<p>%s, %d job(s).", printer->state == IPP_PSTATE_IDLE ? "Idle" : printer->state == IPP_PSTATE_PROCESSING ? "Printing" : "Stopped", cupsArrayCount(printer->jobs));
  for (i = 0, reason = 1; i < (int)(sizeof(reasons) / sizeof(reasons[0])); i ++, reason <<= 1)
    if (printer->state_reasons & reason)
      html_printf(client, "\n<br>&nbsp;&nbsp;&nbsp;&nbsp;%s", reasons[i]);
  html_printf(client, "</p>\n");

  if (cupsArrayCount(printer->jobs) > 0)
  {
    _cupsRWLockRead(&(printer->rwlock));

    html_printf(client, "<table class=\"striped\" summary=\"Jobs\"><thead><tr><th>Job #</th><th>Name</th><th>Owner</th><th>Status</th></tr></thead><tbody>\n");
    for (job = (ipp3d_job_t *)cupsArrayFirst(printer->jobs); job; job = (ipp3d_job_t *)cupsArrayNext(printer->jobs))
    {
      char	when[256],		/* When job queued/started/finished */
	      hhmmss[64];		/* Time HH:MM:SS */

      switch (job->state)
      {
	case IPP_JSTATE_PENDING :
	case IPP_JSTATE_HELD :
	    snprintf(when, sizeof(when), "Queued at %s", time_string(job->created, hhmmss, sizeof(hhmmss)));
	    break;
	case IPP_JSTATE_PROCESSING :
	case IPP_JSTATE_STOPPED :
	    snprintf(when, sizeof(when), "Started at %s", time_string(job->processing, hhmmss, sizeof(hhmmss)));
	    break;
	case IPP_JSTATE_ABORTED :
	    snprintf(when, sizeof(when), "Aborted at %s", time_string(job->completed, hhmmss, sizeof(hhmmss)));
	    break;
	case IPP_JSTATE_CANCELED :
	    snprintf(when, sizeof(when), "Canceled at %s", time_string(job->completed, hhmmss, sizeof(hhmmss)));
	    break;
	case IPP_JSTATE_COMPLETED :
	    snprintf(when, sizeof(when), "Completed at %s", time_string(job->completed, hhmmss, sizeof(hhmmss)));
	    break;
      }

      html_printf(client, "<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>\n", job->id, job->name, job->username, when);
    }
    html_printf(client, "</tbody></table>\n");

    _cupsRWUnlock(&(printer->rwlock));
  }

  html_footer(client);

  return (1);
}


/*
 * 'time_string()' - Return the local time in hours, minutes, and seconds.
 */

static char *
time_string(time_t tv,			/* I - Time value */
            char   *buffer,		/* I - Buffer */
	    size_t bufsize)		/* I - Size of buffer */
{
  struct tm	*curtime = localtime(&tv);
					/* Local time */

  strftime(buffer, bufsize, "%X", curtime);
  return (buffer);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(int status)			/* O - Exit status */
{
  _cupsLangPuts(stdout, _("Usage: ipp3dprinter [options] \"name\""));
  _cupsLangPuts(stdout, _("Options:"));
  _cupsLangPuts(stderr, _("--help                  Show program help"));
  _cupsLangPuts(stderr, _("--no-web-forms          Disable web forms for media and supplies"));
  _cupsLangPuts(stderr, _("--version               Show program version"));
  _cupsLangPuts(stdout, _("-D device-uri           Set the device URI for the printer"));
#ifdef HAVE_SSL
  _cupsLangPuts(stdout, _("-K keypath              Set location of server X.509 certificates and keys."));
#endif /* HAVE_SSL */
  _cupsLangPuts(stdout, _("-M manufacturer         Set manufacturer name (default=Test)"));
  _cupsLangPuts(stdout, _("-a filename.conf        Load printer attributes from conf file"));
  _cupsLangPuts(stdout, _("-c command              Set print command"));
  _cupsLangPuts(stdout, _("-d spool-directory      Set spool directory"));
  _cupsLangPuts(stdout, _("-f type/subtype[,...]   Set supported file types"));
  _cupsLangPuts(stdout, _("-i iconfile.png         Set icon file"));
  _cupsLangPuts(stdout, _("-k                      Keep job spool files"));
  _cupsLangPuts(stdout, _("-l location             Set location of printer"));
  _cupsLangPuts(stdout, _("-m model                Set model name (default=Printer)"));
  _cupsLangPuts(stdout, _("-n hostname             Set hostname for printer"));
  _cupsLangPuts(stdout, _("-p port                 Set port number for printer"));
  _cupsLangPuts(stdout, _("-r subtype,[subtype]    Set DNS-SD service subtype"));
  _cupsLangPuts(stderr, _("-v                      Be verbose"));

  exit(status);
}


/*
 * 'valid_doc_attributes()' - Determine whether the document attributes are
 *                            valid.
 *
 * When one or more document attributes are invalid, this function adds a
 * suitable response and attributes to the unsupported group.
 */

static int				/* O - 1 if valid, 0 if not */
valid_doc_attributes(
    ipp3d_client_t *client)		/* I - Client */
{
  int			valid = 1;	/* Valid attributes? */
  ipp_op_t		op = ippGetOperation(client->request);
					/* IPP operation */
  const char		*op_name = ippOpString(op);
					/* IPP operation name */
  ipp_attribute_t	*attr,		/* Current attribute */
			*supported;	/* xxx-supported attribute */
  const char		*compression = NULL,
					/* compression value */
			*format = NULL;	/* document-format value */


 /*
  * Check operation attributes...
  */

  if ((attr = ippFindAttribute(client->request, "compression", IPP_TAG_ZERO)) != NULL)
  {
   /*
    * If compression is specified, only accept a supported value in a Print-Job
    * or Send-Document request...
    */

    compression = ippGetString(attr, 0, NULL);
    supported   = ippFindAttribute(client->printer->attrs,
                                   "compression-supported", IPP_TAG_KEYWORD);

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
        ippGetGroupTag(attr) != IPP_TAG_OPERATION ||
        (op != IPP_OP_PRINT_JOB && op != IPP_OP_SEND_DOCUMENT &&
         op != IPP_OP_VALIDATE_JOB) ||
        !ippContainsString(supported, compression))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
    else
    {
      fprintf(stderr, "%s %s compression=\"%s\"\n", client->hostname, op_name, compression);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "compression-supplied", NULL, compression);

      if (strcmp(compression, "none"))
      {
	if (Verbosity)
	  fprintf(stderr, "Receiving job file with \"%s\" compression.\n", compression);
        httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, compression);
      }
    }
  }

 /*
  * Is it a format we support?
  */

  if ((attr = ippFindAttribute(client->request, "document-format", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_MIMETYPE ||
        ippGetGroupTag(attr) != IPP_TAG_OPERATION)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
    else
    {
      format = ippGetString(attr, 0, NULL);

      fprintf(stderr, "%s %s document-format=\"%s\"\n", client->hostname, op_name, format);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-supplied", NULL, format);
    }
  }
  else
  {
    format = ippGetString(ippFindAttribute(client->printer->attrs, "document-format-default", IPP_TAG_MIMETYPE), 0, NULL);
    if (!format)
      format = "application/octet-stream"; /* Should never happen */

    attr = ippAddString(client->request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
  }

  if (format && !strcmp(format, "application/octet-stream") && (ippGetOperation(client->request) == IPP_OP_PRINT_JOB || ippGetOperation(client->request) == IPP_OP_SEND_DOCUMENT))
  {
   /*
    * Auto-type the file using the first 8 bytes of the file...
    */

    unsigned char	header[8];	/* First 8 bytes of file */

    memset(header, 0, sizeof(header));
    httpPeek(client->http, (char *)header, sizeof(header));

    if (!memcmp(header, "%PDF", 4))
      format = "application/pdf";
    else if (!memcmp(header, "%!", 2))
      format = "application/postscript";
    else if (!memcmp(header, "\377\330\377", 3) && header[3] >= 0xe0 && header[3] <= 0xef)
      format = "image/jpeg";
    else if (!memcmp(header, "\211PNG", 4))
      format = "image/png";
    else if (!memcmp(header, "RAS2", 4))
      format = "image/pwg-raster";
    else if (!memcmp(header, "UNIRAST", 8))
      format = "image/urf";
    else
      format = NULL;

    if (format)
    {
      fprintf(stderr, "%s %s Auto-typed document-format=\"%s\"\n", client->hostname, op_name, format);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-detected", NULL, format);
    }
  }

  if (op != IPP_OP_CREATE_JOB && (supported = ippFindAttribute(client->printer->attrs, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL && !ippContainsString(supported, format))
  {
    respond_unsupported(client, attr);
    valid = 0;
  }

 /*
  * document-name
  */

  if ((attr = ippFindAttribute(client->request, "document-name", IPP_TAG_NAME)) != NULL)
    ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_NAME, "document-name-supplied", NULL, ippGetString(attr, 0, NULL));

  return (valid);
}


/*
 * 'valid_job_attributes()' - Determine whether the job attributes are valid.
 *
 * When one or more job attributes are invalid, this function adds a suitable
 * response and attributes to the unsupported group.
 */

static int				/* O - 1 if valid, 0 if not */
valid_job_attributes(
    ipp3d_client_t *client)		/* I - Client */
{
  int			i,		/* Looping var */
			count,		/* Number of values */
			valid = 1;	/* Valid attributes? */
  ipp_attribute_t	*attr,		/* Current attribute */
			*supported;	/* xxx-supported attribute */


 /*
  * Check operation attributes...
  */

  valid = valid_doc_attributes(client);

 /*
  * Check the various job template attributes...
  */

  if ((attr = ippFindAttribute(client->request, "copies", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER ||
        ippGetInteger(attr, 0) < 1 || ippGetInteger(attr, 0) > 999)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "ipp-attribute-fidelity", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_BOOLEAN)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-hold-until", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD) ||
	strcmp(ippGetString(attr, 0, NULL), "no-hold"))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-impressions", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetInteger(attr, 0) < 0)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-name", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }

    ippSetGroupTag(client->request, &attr, IPP_TAG_JOB);
  }
  else
    ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, "Untitled");

  if ((attr = ippFindAttribute(client->request, "job-priority", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER ||
        ippGetInteger(attr, 0) < 1 || ippGetInteger(attr, 0) > 100)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-sheets", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD) ||
	strcmp(ippGetString(attr, 0, NULL), "none"))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "media", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
    else
    {
      supported = ippFindAttribute(client->printer->attrs, "media-supported", IPP_TAG_KEYWORD);

      if (!ippContainsString(supported, ippGetString(attr, 0, NULL)))
      {
	respond_unsupported(client, attr);
	valid = 0;
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "media-col", IPP_TAG_ZERO)) != NULL)
  {
    ipp_t		*col,		/* media-col collection */
			*size;		/* media-size collection */
    ipp_attribute_t	*member,	/* Member attribute */
			*x_dim,		/* x-dimension */
			*y_dim;		/* y-dimension */
    int			x_value,	/* y-dimension value */
			y_value;	/* x-dimension value */

    if (ippGetCount(attr) != 1 ||
        ippGetValueTag(attr) != IPP_TAG_BEGIN_COLLECTION)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }

    col = ippGetCollection(attr, 0);

    if ((member = ippFindAttribute(col, "media-size-name", IPP_TAG_ZERO)) != NULL)
    {
      if (ippGetCount(member) != 1 ||
	  (ippGetValueTag(member) != IPP_TAG_NAME &&
	   ippGetValueTag(member) != IPP_TAG_NAMELANG &&
	   ippGetValueTag(member) != IPP_TAG_KEYWORD))
      {
	respond_unsupported(client, attr);
	valid = 0;
      }
      else
      {
	supported = ippFindAttribute(client->printer->attrs, "media-supported", IPP_TAG_KEYWORD);

	if (!ippContainsString(supported, ippGetString(member, 0, NULL)))
	{
	  respond_unsupported(client, attr);
	  valid = 0;
	}
      }
    }
    else if ((member = ippFindAttribute(col, "media-size", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      if (ippGetCount(member) != 1)
      {
	respond_unsupported(client, attr);
	valid = 0;
      }
      else
      {
	size = ippGetCollection(member, 0);

	if ((x_dim = ippFindAttribute(size, "x-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(x_dim) != 1 ||
	    (y_dim = ippFindAttribute(size, "y-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(y_dim) != 1)
	{
	  respond_unsupported(client, attr);
	  valid = 0;
	}
	else
	{
	  x_value   = ippGetInteger(x_dim, 0);
	  y_value   = ippGetInteger(y_dim, 0);
	  supported = ippFindAttribute(client->printer->attrs, "media-size-supported", IPP_TAG_BEGIN_COLLECTION);
	  count     = ippGetCount(supported);

	  for (i = 0; i < count ; i ++)
	  {
	    size  = ippGetCollection(supported, i);
	    x_dim = ippFindAttribute(size, "x-dimension", IPP_TAG_ZERO);
	    y_dim = ippFindAttribute(size, "y-dimension", IPP_TAG_ZERO);

	    if (ippContainsInteger(x_dim, x_value) && ippContainsInteger(y_dim, y_value))
	      break;
	  }

	  if (i >= count)
	  {
	    respond_unsupported(client, attr);
	    valid = 0;
	  }
	}
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "multiple-document-handling", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
        (strcmp(ippGetString(attr, 0, NULL),
		"separate-documents-uncollated-copies") &&
	 strcmp(ippGetString(attr, 0, NULL),
		"separate-documents-collated-copies")))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "orientation-requested", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_ENUM ||
        ippGetInteger(attr, 0) < IPP_ORIENT_PORTRAIT ||
        ippGetInteger(attr, 0) > IPP_ORIENT_REVERSE_PORTRAIT)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "page-ranges", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetValueTag(attr) != IPP_TAG_RANGE)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "print-quality", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_ENUM ||
        ippGetInteger(attr, 0) < IPP_QUALITY_DRAFT ||
        ippGetInteger(attr, 0) > IPP_QUALITY_HIGH)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "printer-resolution", IPP_TAG_ZERO)) != NULL)
  {
    supported = ippFindAttribute(client->printer->attrs, "printer-resolution-supported", IPP_TAG_RESOLUTION);

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_RESOLUTION ||
        !supported)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
    else
    {
      int	xdpi,			/* Horizontal resolution for job template attribute */
		ydpi,			/* Vertical resolution for job template attribute */
		sydpi;			/* Vertical resolution for supported value */
      ipp_res_t	units,			/* Units for job template attribute */
		sunits;			/* Units for supported value */

      xdpi  = ippGetResolution(attr, 0, &ydpi, &units);
      count = ippGetCount(supported);

      for (i = 0; i < count; i ++)
      {
        if (xdpi == ippGetResolution(supported, i, &sydpi, &sunits) && ydpi == sydpi && units == sunits)
          break;
      }

      if (i >= count)
      {
	respond_unsupported(client, attr);
	valid = 0;
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "sides", IPP_TAG_ZERO)) != NULL)
  {
    const char *sides = ippGetString(attr, 0, NULL);
					/* "sides" value... */

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD)
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
    else if ((supported = ippFindAttribute(client->printer->attrs, "sides-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      if (!ippContainsString(supported, sides))
      {
	respond_unsupported(client, attr);
	valid = 0;
      }
    }
    else if (strcmp(sides, "one-sided"))
    {
      respond_unsupported(client, attr);
      valid = 0;
    }
  }

  return (valid);
}

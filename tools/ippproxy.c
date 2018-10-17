/*
 * IPP Proxy implementation for HP PCL and IPP Everywhere printers.
 *
 * Copyright © 2016-2018 by the IEEE-ISTO Printer Working Group.
 * Copyright © 2014-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cups/cups.h>
#include <cups/thread-private.h>


/*
 * Local types...
 */

typedef struct proxy_info_s		/* Proxy thread information */
{
  int		done;			/* Non-zero when done */
  http_t	*http;			/* Connection to Infrastructure Printer */
  char		resource[256];		/* Resource path */
  const char	*printer_uri,		/* Infrastructure Printer URI */
		*device_uri,		/* Output device URI */
		*device_uuid,		/* Output device UUID */
		*outformat;		/* Desired output format (NULL for auto) */
  ipp_t		*device_attrs;		/* Output device attributes */
} proxy_info_t;

typedef struct proxy_job_s		/* Proxy job information */
{
  ipp_jstate_t	local_job_state;	/* Local job-state value */
  int		local_job_id,		/* Local job-id value */
		remote_job_id,		/* Remote job-id value */
		remote_job_state;	/* Remote job-state value */
} proxy_job_t;


/*
 * Local globals...
 */

static cups_array_t	*jobs;		/* Local jobs */
static _cups_cond_t	jobs_cond = _CUPS_COND_INITIALIZER;
					/* Condition variable to signal changes */
static _cups_mutex_t	jobs_mutex = _CUPS_MUTEX_INITIALIZER;
					/* Mutex for condition variable */
static _cups_rwlock_t	jobs_rwlock = _CUPS_RWLOCK_INITIALIZER;
					/* Read/write lock for jobs array */
static char		*password = NULL;
					/* Password, if any */

static const char * const printer_attrs[] =
		{			/* Printer attributes we care about */
		  "copies-supported",
		  "document-format-supported",
		  "jpeg-k-octets-supported",
		  "media-bottom-margin-supported",
		  "media-col-database",
		  "media-col-default",
		  "media-col-ready",
		  "media-col-supported",
		  "media-default",
		  "media-left-margin-supported",
		  "media-ready",
		  "media-right-margin-supported",
		  "media-size-supported",
		  "media-source-supported",
		  "media-supported",
		  "media-top-margin-supported",
		  "media-type-supported",
		  "pdf-k-octets-supported",
		  "print-color-mode-default",
		  "print-color-mode-supported",
		  "print-quality-default",
		  "print-quality-supported",
		  "printer-resolution-default",
		  "printer-resolution-supported",
		  "printer-state",
		  "printer-state-message",
		  "printer-state-reasons",
		  "pwg-raster-document-resolution-supported",
		  "pwg-raster-document-sheet-back",
		  "pwg-raster-document-type-supported",
		  "sides-default",
		  "sides-supported",
		  "urf-supported"
		};
static int	stop_running = 0;
static int	verbosity = 0;


/*
 * Local functions...
 */

static void	acknowledge_identify_printer(http_t *http, const char *printer_uri, const char *resource, const char *device_uuid);
static int	attrs_are_equal(ipp_attribute_t *a, ipp_attribute_t *b);
static int	compare_jobs(proxy_job_t *a, proxy_job_t *b);
static ipp_t	*create_media_col(const char *media, const char *source, const char *type, int width, int length, int margins);
static ipp_t	*create_media_size(int width, int length);
static void	deregister_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uuid);
static proxy_job_t *find_job(int remote_job_id);
static ipp_t	*get_device_attrs(const char *device_uri);
static void	make_uuid(const char *device_uri, char *uuid, size_t uuidsize);
static const char *password_cb(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
static void	plogf(proxy_job_t *pjob, const char *message, ...);
static void	*proxy_jobs(proxy_info_t *info);
static int	register_printer(http_t *http, const char *printer_uri, const char *resource, const char *device_uri, const char *device_uuid);
static void	run_job(proxy_info_t *info, proxy_job_t *pjob);
static void	run_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uri, const char *device_uuid, const char *outformat);
static void	send_document(proxy_info_t *info, proxy_job_t *pjob, ipp_t *job_attrs, ipp_t *doc_attrs, int doc_number);
static void	sighandler(int sig);
static int	update_device_attrs(http_t *http, const char *printer_uri, const char *resource, const char *device_uuid, ipp_t *old_attrs, ipp_t *new_attrs);
static void	update_document_status(proxy_info_t *info, proxy_job_t *pjob, int doc_number, ipp_dstate_t doc_state);
static void	update_job_status(proxy_info_t *info, proxy_job_t *pjob);
static void	usage(int status) _CUPS_NORETURN;


/*
 * 'main()' - Main entry for ippproxy.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  char		*opt,			/* Current option */
		*device_uri = NULL,	/* Device URI */
		*printer_uri = NULL;	/* Infrastructure printer URI */
  cups_dest_t	*dest;			/* Destination for printer URI */
  http_t	*http;			/* Connection to printer */
  char		resource[1024];		/* Resource path */
  int		subscription_id;	/* Event subscription ID */
  char		device_uuid[45];	/* Device UUID URN */
  const char	*outformat = NULL;	/* Output format */


 /*
  * Parse command-line...
  */

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-' && argv[i][1] == '-')
    {
      if (!strcmp(argv[i], "--help"))
      {
        usage(0);
      }
      else if (!strcmp(argv[i], "--version"))
      {
        puts(CUPS_SVERSION);
      }
      else
      {
        fprintf(stderr, "ippproxy: Unknown option '%s'.\n", argv[i]);
        usage(1);
      }
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
	  case 'd' : /* -d device-uri */
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ippproxy: Missing device URI after '-d' option.\n", stderr);
		usage(1);
	      }

	      if (strncmp(argv[i], "ipp://", 6) && strncmp(argv[i], "ipps://", 7) && strncmp(argv[i], "socket://", 9))
	      {
	        fputs("ippproxy: Unsupported device URI scheme.\n", stderr);
	        usage(1);
	      }

	      device_uri = argv[i];
	      break;

          case 'm' : /* -m mime/type */
              i ++;
              if (i >= argc)
	      {
	        fputs("ippproxy: Missing MIME media type after '-m' option.\n", stderr);
	        usage(1);
	      }

	      outformat = argv[i];
	      break;

	  case 'p' : /* -p password */
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ippproxy: Missing password after '-p' option.\n", stderr);
		usage(1);
	      }

	      password = argv[i];
	      break;

	  case 'u' : /* -u user */
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ippproxy: Missing username after '-u' option.\n", stderr);
		usage(1);
	      }

	      cupsSetUser(argv[i]);
	      break;

          case 'v' : /* Be verbose */
              verbosity ++;
              break;

	  default :
	      fprintf(stderr, "ippproxy: Unknown option '-%c'.\n", *opt);
	      usage(1);
	      break;
	}
      }
    }
    else if (printer_uri)
    {
      fprintf(stderr, "ippproxy: Unexpected option '%s'.\n", argv[i]);
      usage(1);
    }
    else
      printer_uri = argv[i];
  }

  if (!printer_uri)
    usage(1);

  if (!device_uri)
  {
    fputs("ippproxy: Must specify '-d device-uri'.\n", stderr);
    usage(1);
  }

  if (!password)
    password = getenv("IPPPROXY_PASSWORD");

  if (password)
    cupsSetPasswordCB2(password_cb, password);

  make_uuid(device_uri, device_uuid, sizeof(device_uuid));

 /*
  * Connect to the infrastructure printer...
  */

  dest = cupsGetDestWithURI("infra", printer_uri);

  if (verbosity)
    plogf(NULL, "Main thread connecting to '%s'.", printer_uri);

  while ((http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE, 30000, NULL, resource, sizeof(resource), NULL, NULL)) == NULL)
  {
    int interval = 1 + (CUPS_RAND() % 30);
					/* Retry interval */

    plogf(NULL, "'%s' is not responding, retrying in %d seconds.", printer_uri, interval);
    sleep((unsigned)interval);
  }

  if (verbosity)
    plogf(NULL, "Connected to '%s'.", printer_uri);

  cupsFreeDests(1, dest);

 /*
  * Register the printer and wait for jobs to process...
  */

  signal(SIGHUP, sighandler);
  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);

  if ((subscription_id = register_printer(http, printer_uri, resource, device_uri, device_uuid)) == 0)
  {
    httpClose(http);
    return (1);
  }

  run_printer(http, printer_uri, resource, subscription_id, device_uri, device_uuid, outformat);

  deregister_printer(http, printer_uri, resource, subscription_id, device_uuid);
  httpClose(http);

  return (0);
}


/*
 * 'acknowledge_identify_printer()' - Acknowledge an Identify-Printer request.
 */

static void
acknowledge_identify_printer(
    http_t     *http,			/* I - HTTP connection */
    const char *printer_uri,		/* I - Printer URI */
    const char *resource,		/* I - Resource path */
    const char *device_uuid)		/* I - Device UUID */
{
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *actions,		/* "identify-actions" attribute */
		*message;		/* "message" attribute */


  request = ippNewRequest(IPP_OP_ACKNOWLEDGE_IDENTIFY_PRINTER);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "device-uuid", NULL, device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  response = cupsDoRequest(http, request, resource);

  actions = ippFindAttribute(response, "identify-actions", IPP_TAG_KEYWORD);
  message = ippFindAttribute(response, "message", IPP_TAG_TEXT);

  if (ippContainsString(actions, "display"))
    printf("IDENTIFY-PRINTER: display (%s)\n", message ? ippGetString(message, 0, NULL) : "No message supplied");

  if (!actions || ippContainsString(actions, "sound"))
    puts("IDENTIFY-PRINTER: sound\007");

  ippDelete(response);
}


/*
 * 'attrs_are_equal()' - Compare two attributes for equality.
 */

static int				/* O - 1 if equal, 0 otherwise */
attrs_are_equal(ipp_attribute_t *a,	/* I - First attribute */
                ipp_attribute_t *b)	/* I - Second attribute */
{
  int		i,			/* Looping var */
		count;			/* Number of values */
  ipp_tag_t	tag;			/* Type of value */


 /*
  * Check that both 'a' and 'b' point to something first...
  */

  if ((a != NULL) != (b != NULL))
    return (0);

  if (a == NULL && b == NULL)
    return (1);

 /*
  * Check that 'a' and 'b' are of the same type with the same number
  * of values...
  */

  if ((tag = ippGetValueTag(a)) != ippGetValueTag(b))
    return (0);

  if ((count = ippGetCount(a)) != ippGetCount(b))
    return (0);

 /*
  * Compare values...
  */

  switch (tag)
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
        for (i = 0; i < count; i ++)
	  if (ippGetInteger(a, i) != ippGetInteger(b, i))
	    return (0);
	break;

    case IPP_TAG_BOOLEAN :
        for (i = 0; i < count; i ++)
	  if (ippGetBoolean(a, i) != ippGetBoolean(b, i))
	    return (0);
	break;

    case IPP_TAG_KEYWORD :
        for (i = 0; i < count; i ++)
	  if (strcmp(ippGetString(a, i, NULL), ippGetString(b, i, NULL)))
	    return (0);
	break;

    default :
        return (0);
  }

 /*
  * If we get this far we must be the same...
  */

  return (1);
}


/*
 * 'compare_jobs()' - Compare two jobs.
 */

static int
compare_jobs(proxy_job_t *a,		/* I - First job */
             proxy_job_t *b)		/* I - Second job */
{
  return (a->remote_job_id - b->remote_job_id);
}


/*
 * 'create_media_col()' - Create a media-col value.
 */

static ipp_t *				/* O - media-col collection */
create_media_col(const char *media,	/* I - Media name */
		 const char *source,	/* I - Media source */
		 const char *type,	/* I - Media type */
		 int        width,	/* I - x-dimension in 2540ths */
		 int        length,	/* I - y-dimension in 2540ths */
		 int        margins)	/* I - Value for margins */
{
  ipp_t	*media_col = ippNew(),		/* media-col value */
	*media_size = create_media_size(width, length);
					/* media-size value */
  char	media_key[256];			/* media-key value */


  if (type && source)
    snprintf(media_key, sizeof(media_key), "%s_%s_%s%s", media, source, type, margins == 0 ? "_borderless" : "");
  else if (type)
    snprintf(media_key, sizeof(media_key), "%s__%s%s", media, type, margins == 0 ? "_borderless" : "");
  else if (source)
    snprintf(media_key, sizeof(media_key), "%s_%s%s", media, source, margins == 0 ? "_borderless" : "");
  else
    snprintf(media_key, sizeof(media_key), "%s%s", media, margins == 0 ? "_borderless" : "");

  ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-key", NULL,
               media_key);
  ippAddCollection(media_col, IPP_TAG_PRINTER, "media-size", media_size);
  ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-size-name", NULL, media);
  ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "media-bottom-margin", margins);
  ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "media-left-margin", margins);
  ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "media-right-margin", margins);
  ippAddInteger(media_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "media-top-margin", margins);
  if (source)
    ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-source", NULL, source);
  if (type)
    ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-type", NULL, type);

  ippDelete(media_size);

  return (media_col);
}


/*
 * 'create_media_size()' - Create a media-size value.
 */

static ipp_t *				/* O - media-col collection */
create_media_size(int width,		/* I - x-dimension in 2540ths */
		  int length)		/* I - y-dimension in 2540ths */
{
  ipp_t	*media_size = ippNew();		/* media-size value */


  ippAddInteger(media_size, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-dimension",
                width);
  ippAddInteger(media_size, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-dimension",
                length);

  return (media_size);
}


/*
 * 'deregister_printer()' - Unregister the output device and cancel the printer subscription.
 */

static void
deregister_printer(
    http_t     *http,			/* I - Connection to printer */
    const char *printer_uri,		/* I - Printer URI */
    const char *resource,		/* I - Resource path */
    int        subscription_id,		/* I - Subscription ID */
    const char *device_uuid)		/* I - Device UUID */
{
  ipp_t	*request;			/* IPP request */


 /*
  * Deregister the output device...
  */

  request = ippNewRequest(IPP_OP_DEREGISTER_OUTPUT_DEVICE);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(http, request, resource));

 /*
  * Then cancel the subscription we are using...
  */

  request = ippNewRequest(IPP_OP_CANCEL_SUBSCRIPTION);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-subscription-id", subscription_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(http, request, resource));
}


/*
 * 'find_job()' - Find a remote job that has been queued for proxying...
 */

static proxy_job_t *			/* O - Proxy job or @code NULL@ if not found */
find_job(int remote_job_id)		/* I - Remote job ID */
{
  proxy_job_t	key,			/* Search key */
		*match;			/* Matching job, if any */


  key.remote_job_id = remote_job_id;

  _cupsRWLockRead(&jobs_rwlock);
  match = (proxy_job_t *)cupsArrayFind(jobs, &key);
  _cupsRWUnlock(&jobs_rwlock);

  return (match);
}


/*
 * 'get_device_attrs()' - Get current attributes for a device.
 */

static ipp_t *				/* O - IPP attributes */
get_device_attrs(const char *device_uri)/* I - Device URI */
{
  ipp_t	*response = NULL;		/* IPP attributes */


  if (!strncmp(device_uri, "ipp://", 6) || !strncmp(device_uri, "ipps://", 7))
  {
   /*
    * Query the IPP printer...
    */

    int		i,			/* Looping var */
		count;			/* Number of values */
    cups_dest_t	*dest;			/* Destination for printer URI */
    http_t	*http;			/* Connection to printer */
    char	resource[1024];		/* Resource path */
    ipp_t	*request;		/* Get-Printer-Attributes request */
    ipp_attribute_t *urf_supported,	/* urf-supported */
		*pwg_supported;		/* pwg-raster-document-xxx-supported */


   /*
    * Connect to the printer...
    */

    dest = cupsGetDestWithURI("device", device_uri);

    while ((http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE, 30000, NULL, resource, sizeof(resource), NULL, NULL)) == NULL)
    {
      int interval = 1 + (CUPS_RAND() % 30);
					/* Retry interval */

      plogf(NULL, "'%s' is not responding, retrying in %d seconds.", device_uri, interval);
      sleep((unsigned)interval);
    }

    cupsFreeDests(1, dest);

   /*
    * Get the attributes...
    */

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(printer_attrs) / sizeof(printer_attrs[0])), NULL, printer_attrs);

    response = cupsDoRequest(http, request, resource);

    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
      fprintf(stderr, "ippproxy: Device at '%s' returned error: %s\n", device_uri, cupsLastErrorString());
      ippDelete(response);
      response = NULL;
    }

    httpClose(http);

   /*
    * Convert urf-supported to pwg-raster-document-xxx-supported, as needed...
    */

    urf_supported = ippFindAttribute(response, "urf-supported", IPP_TAG_KEYWORD);
    pwg_supported = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION);
    if (urf_supported && !pwg_supported)
    {
      for (i = 0, count = ippGetCount(urf_supported); i < count; i ++)
      {
        const char *keyword = ippGetString(urf_supported, i, NULL);
					/* Value from urf_supported */

        if (!strncmp(keyword, "RS", 2))
        {
	  char	*ptr;			/* Pointer into value */
	  int	res;			/* Resolution */

          for (res = (int)strtol(keyword + 2, &ptr, 10); res > 0; res = (int)strtol(ptr + 1, &ptr, 10))
	  {
	    if (pwg_supported)
	      ippSetResolution(response, &pwg_supported, ippGetCount(pwg_supported), IPP_RES_PER_INCH, res, res);
	    else
	      pwg_supported = ippAddResolution(response, IPP_TAG_PRINTER, "pwg-raster-document-resolution-supported", IPP_RES_PER_INCH, res, res);
	  }
        }
      }
    }

    pwg_supported = ippFindAttribute(response, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD);
    if (urf_supported && !pwg_supported)
    {
      for (i = 0, count = ippGetCount(urf_supported); i < count; i ++)
      {
        const char *keyword = ippGetString(urf_supported, i, NULL);
					/* Value from urf_supported */

        if (!strncmp(keyword, "DM", 2))
        {
          if (!strcmp(keyword, "DM1"))
            pwg_supported = ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", NULL, "normal");
          else if (!strcmp(keyword, "DM2"))
            pwg_supported = ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", NULL, "flipped");
          else if (!strcmp(keyword, "DM3"))
            pwg_supported = ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", NULL, "rotated");
          else
            pwg_supported = ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", NULL, "manual-tumble");
        }
      }
    }

    pwg_supported = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD);
    if (urf_supported && !pwg_supported)
    {
      for (i = 0, count = ippGetCount(urf_supported); i < count; i ++)
      {
        const char *keyword = ippGetString(urf_supported, i, NULL);
					/* Value from urf_supported */
        const char *pwg_keyword = NULL;	/* Value for pwg-raster-document-type-supported */

        if (!strcmp(keyword, "ADOBERGB24"))
          pwg_keyword = "adobe-rgb_8";
	else if (!strcmp(keyword, "ADOBERGB48"))
          pwg_keyword = "adobe-rgb_16";
	else if (!strcmp(keyword, "SRGB24"))
          pwg_keyword = "srgb_8";
	else if (!strcmp(keyword, "W8"))
          pwg_keyword = "sgray_8";
	else if (!strcmp(keyword, "W16"))
          pwg_keyword = "sgray_16";

        if (pwg_keyword)
        {
	  if (pwg_supported)
	    ippSetString(response, &pwg_supported, ippGetCount(pwg_supported), pwg_keyword);
	  else
	    pwg_supported = ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-type-supported", NULL, pwg_keyword);
        }
      }
    }
  }
  else
  {
   /*
    * Must be a socket-based HP PCL laser printer, report just standard size
    * information...
    */

    int			i;		/* Looping var */
    ipp_attribute_t	*media_col_database,
					/* media-col-database value */
			*media_size_supported;
					/* media-size-supported value */
    ipp_t		*media_col;	/* media-col-default value */
    static const int media_col_sizes[][2] =
    {					/* Default media-col sizes */
      { 21590, 27940 },			/* Letter */
      { 21590, 35560 },			/* Legal */
      { 21000, 29700 }			/* A4 */
    };
    static const char * const media_col_supported[] =
    {					/* media-col-supported values */
      "media-bottom-margin",
      "media-left-margin",
      "media-right-margin",
      "media-size",
      "media-size-name",
      "media-top-margin"
    };
    static const char * const media_supported[] =
    {					/* Default media sizes */
      "na_letter_8.5x11in",		/* Letter */
      "na_legal_8.5x14in",		/* Legal */
      "iso_a4_210x297mm"			/* A4 */
    };
    static const int quality_supported[] =
    {					/* print-quality-supported values */
      IPP_QUALITY_DRAFT,
      IPP_QUALITY_NORMAL,
      IPP_QUALITY_HIGH
    };
    static const int resolution_supported[] =
    {					/* printer-resolution-supported values */
      300,
      600
    };
    static const char * const sides_supported[] =
    {					/* sides-supported values */
      "one-sided",
      "two-sided-long-edge",
      "two-sided-short-edge"
    };

    response = ippNew();

    ippAddRange(response, IPP_TAG_PRINTER, "copies-supported", 1, 1);
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", NULL, "application/vnd.hp-pcl");
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-bottom-margin-supported", 635);

    media_col_database = ippAddCollections(response, IPP_TAG_PRINTER, "media-col-database", (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])), NULL);
    for (i = 0; i < (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])); i ++)
    {
      media_col = create_media_col(media_supported[i], NULL, NULL, media_col_sizes[i][0], media_col_sizes[i][1], 635);

      ippSetCollection(response, &media_col_database, i, media_col);

      ippDelete(media_col);
    }

    media_col = create_media_col(media_supported[0], NULL, NULL, media_col_sizes[0][0], media_col_sizes[0][1], 635);
    ippAddCollection(response, IPP_TAG_PRINTER, "media-col-default", media_col);
    ippDelete(media_col);

    media_col = create_media_col(media_supported[0], NULL, NULL, media_col_sizes[0][0], media_col_sizes[0][1], 635);
    ippAddCollection(response, IPP_TAG_PRINTER, "media-col-ready", media_col);
    ippDelete(media_col);

    ippAddStrings(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-col-supported", (int)(sizeof(media_col_supported) / sizeof(media_col_supported[0])), NULL, media_col_supported);
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-default", NULL, media_supported[0]);
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-left-margin-supported", 635);
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-ready", NULL, media_supported[0]);
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-right-margin-supported", 635);

    media_size_supported = ippAddCollections(response, IPP_TAG_PRINTER, "media-size-supported", (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])), NULL);
    for (i = 0;
	 i < (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0]));
	 i ++)
    {
      ipp_t *size = create_media_size(media_col_sizes[i][0], media_col_sizes[i][1]);

      ippSetCollection(response, &media_size_supported, i, size);
      ippDelete(size);
    }

    ippAddStrings(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-supported", (int)(sizeof(media_supported) / sizeof(media_supported[0])), NULL, media_supported);
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-top-margin-supported", 635);
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "print-color-mode-default", NULL, "monochrome");
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "print-color-mode-supported", NULL, "monochrome");
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-default", IPP_QUALITY_NORMAL);
    ippAddIntegers(response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-supported", (int)(sizeof(quality_supported) / sizeof(quality_supported[0])), quality_supported);
    ippAddResolution(response, IPP_TAG_PRINTER, "printer-resolution-default", IPP_RES_PER_INCH, 300, 300);
    ippAddResolutions(response, IPP_TAG_PRINTER, "printer-resolution-supported", (int)(sizeof(resolution_supported) / sizeof(resolution_supported[0])), IPP_RES_PER_INCH, resolution_supported, resolution_supported);
    ippAddInteger(response, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", IPP_PSTATE_IDLE);
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "printer-state-reasons", NULL, "none");
    ippAddString(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sides-default", NULL, "two-sided-long-edge");
    ippAddStrings(response, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sides-supported", (int)(sizeof(sides_supported) / sizeof(sides_supported[0])), NULL, sides_supported);
  }

  return (response);
}


/*
 * 'make_uuid()' - Make a RFC 4122 URN UUID from the device URI.
 *
 * NULL device URIs are (appropriately) mapped to "file://hostname/dev/null".
 */

static void
make_uuid(const char *device_uri,	/* I - Device URI or NULL */
          char       *uuid,		/* I - UUID string buffer */
	  size_t     uuidsize)		/* I - Size of UUID buffer */
{
  char			nulluri[1024];	/* NULL URI buffer */
  unsigned char		sha256[32];	/* SHA-256 hash */


 /*
  * Use "file://hostname/dev/null" if the device URI is NULL...
  */

  if (!device_uri)
  {
    char	host[1024];		/* Hostname */


    httpGetHostname(NULL, host, sizeof(host));
    httpAssembleURI(HTTP_URI_CODING_ALL, nulluri, sizeof(nulluri), "file", NULL, host, 0, "/dev/null");
    device_uri = nulluri;
  }

 /*
  * Build a version 3 UUID conforming to RFC 4122 based on the SHA-256 hash of
  * the device URI.
  */

  cupsHashData("sha-256", device_uri, strlen(device_uri), sha256, sizeof(sha256));

  snprintf(uuid, uuidsize, "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", sha256[16], sha256[17], sha256[18], sha256[19], sha256[20], sha256[21], (sha256[22] & 15) | 0x30, sha256[23], (sha256[24] & 0x3f) | 0x40, sha256[25], sha256[26], sha256[27], sha256[28], sha256[29], sha256[30], sha256[31]);

  if (verbosity)
    plogf(NULL, "UUID for '%s' is '%s'.", device_uri, uuid);
}


/*
 * 'password_cb()' - Password callback.
 */

static const char *			/* O - Password string */
password_cb(const char *prompt,		/* I - Prompt (unused) */
            http_t     *http,		/* I - Connection (unused) */
	    const char *method,		/* I - Method (unused) */
	    const char *resource,	/* I - Resource path (unused) */
	    void       *user_data)	/* I - Password string */
{
  (void)prompt;
  (void)http;
  (void)method;
  (void)resource;

  return ((char *)user_data);
}


/*
 * 'plogf()' - Log a message to stderr.
 */

static void
plogf(proxy_job_t *pjob,			/* I - Proxy job, if any */
      const char  *message,		/* I - Message */
      ...)				/* I - Additional arguments as needed */
{
  char		temp[1024];		/* Temporary message string */
  va_list	ap;			/* Pointer to additional arguments */
  struct timeval curtime;		/* Current time */
  struct tm	*curdate;		/* Current date and time */


  gettimeofday(&curtime, NULL);
  curdate = gmtime(&curtime.tv_sec);

  if (pjob)
    snprintf(temp, sizeof(temp), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ  [Job %d] %s\n", curdate->tm_year + 1900, curdate->tm_mon + 1, curdate->tm_mday, curdate->tm_hour, curdate->tm_min, curdate->tm_sec, (int)curtime.tv_usec / 1000, pjob->remote_job_id, message);
  else
    snprintf(temp, sizeof(temp), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ  %s\n", curdate->tm_year + 1900, curdate->tm_mon + 1, curdate->tm_mday, curdate->tm_hour, curdate->tm_min, curdate->tm_sec, (int)curtime.tv_usec / 1000, message);

  va_start(ap, message);
  vfprintf(stderr, temp, ap);
  va_end(ap);
}


/*
 * 'proxy_jobs()' - Relay jobs to the local printer.
 */

static void *				/* O - Thread exit status */
proxy_jobs(proxy_info_t *info)		/* I - Printer and device info */
{
  cups_dest_t	*dest;			/* Destination for printer URI */
  proxy_job_t	*pjob;			/* Current job */
//  ipp_t		*new_attrs;		/* New device attributes */


 /*
  * Connect to the infrastructure printer...
  */

  if (verbosity)
    plogf(NULL, "Job processing thread starting.");

  if (password)
    cupsSetPasswordCB2(password_cb, password);

  dest = cupsGetDestWithURI("infra", info->printer_uri);

  if (verbosity)
    plogf(NULL, "Connecting to '%s'.", info->printer_uri);

  while ((info->http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE, 30000, NULL, info->resource, sizeof(info->resource), NULL, NULL)) == NULL)
  {
    int interval = 1 + (CUPS_RAND() % 30);
					/* Retry interval */

    plogf(NULL, "'%s' is not responding, retrying in %d seconds.", info->printer_uri, interval);
    sleep((unsigned)interval);
  }

  cupsFreeDests(1, dest);

  if (verbosity)
    plogf(NULL, "Connected to '%s'.", info->printer_uri);

  _cupsMutexLock(&jobs_mutex);

  while (!info->done)
  {
   /*
    * Look for a fetchable job...
    */

    if (verbosity)
      plogf(NULL, "Checking for queued jobs.");

    _cupsRWLockRead(&jobs_rwlock);
    for (pjob = (proxy_job_t *)cupsArrayFirst(jobs); pjob; pjob = (proxy_job_t *)cupsArrayNext(jobs))
    {
      if (pjob->local_job_state == IPP_JSTATE_PENDING && pjob->remote_job_state < IPP_JSTATE_CANCELED)
        break;
    }
    _cupsRWUnlock(&jobs_rwlock);

    if (pjob)
    {
     /*
      * Process this job...
      */

      run_job(info, pjob);
    }
    else
    {
     /*
      * We didn't have a fetchable job so purge the job cache and wait for more
      * jobs...
      */

      _cupsRWLockWrite(&jobs_rwlock);
      for (pjob = (proxy_job_t *)cupsArrayFirst(jobs); pjob; pjob = (proxy_job_t *)cupsArrayNext(jobs))
      {
	if (pjob->remote_job_state >= IPP_JSTATE_CANCELED)
	  cupsArrayRemove(jobs, pjob);
      }
      _cupsRWUnlock(&jobs_rwlock);

      if (verbosity)
        plogf(NULL, "Waiting for jobs.");

      _cupsCondWait(&jobs_cond, &jobs_mutex, 15.0);
    }
  }

  _cupsMutexUnlock(&jobs_mutex);

  return (NULL);
}


/*
 * 'register_printer()' - Register the printer (output device) with the Infrastructure Printer.
 */

static int				/* O - Subscription ID */
register_printer(
    http_t     *http,			/* I - Connection to printer */
    const char *printer_uri,		/* I - Printer URI */
    const char *resource,		/* I - Resource path */
    const char *device_uri,		/* I - Device URI, if any */
    const char *device_uuid)		/* I - Device UUID */
{
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* Attribute in response */
  int		subscription_id = 0;	/* Subscription ID */
  static const char * const events[] =	/* Events to monitor */
  {
    "document-config-changed",
    "document-state-changed",
    "job-config-changed",
    "job-fetchable",
    "job-state-changed",
    "printer-config-changed",
    "printer-state-changed"
  };


  (void)device_uri;
  (void)device_uuid;

 /*
  * Create a printer subscription to monitor for events...
  */

  request = ippNewRequest(IPP_OP_CREATE_PRINTER_SUBSCRIPTION);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippAddString(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD, "notify-pull-method", NULL, "ippget");
  ippAddStrings(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD, "notify-events", (int)(sizeof(events) / sizeof(events[0])), NULL, events);
  ippAddInteger(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-lease-duration", 0);

  response = cupsDoRequest(http, request, resource);

  if (cupsLastError() != IPP_STATUS_OK)
  {
    plogf(NULL, "Unable to monitor events on '%s': %s", printer_uri, cupsLastErrorString());
    return (0);
  }

  if ((attr = ippFindAttribute(response, "notify-subscription-id", IPP_TAG_INTEGER)) != NULL)
  {
    subscription_id = ippGetInteger(attr, 0);

    if (verbosity)
      plogf(NULL, "Monitoring events with subscription #%d.", subscription_id);
  }
  else
  {
    plogf(NULL, "Unable to monitor events on '%s': No notify-subscription-id returned.", printer_uri);
  }

  ippDelete(response);

  return (subscription_id);
}


/*
 * 'run_job()' - Fetch and print a job.
 */

static void
run_job(proxy_info_t *info,		/* I - Proxy information */
        proxy_job_t  *pjob)		/* I - Proxy job to fetch and print */
{
  ipp_t		*request,		/* IPP request */
		*job_attrs,		/* Job attributes */
		*doc_attrs;		/* Document attributes */
  int		num_docs,		/* Number of documents */
		doc_number;		/* Current document number */
  ipp_attribute_t *doc_formats;		/* Supported document formats */
  const char	*doc_format = NULL;	/* Document format we want... */


 /*
  * Figure out the output format we want to use...
  */

  doc_formats = ippFindAttribute(info->device_attrs, "document-format-supported", IPP_TAG_MIMETYPE);

  if (info->outformat)
    doc_format = info->outformat;
  else if (!ippContainsString(doc_formats, "application/pdf"))
  {
    if (ippContainsString(doc_formats, "image/urf"))
      doc_format = "image/urf";
    else if (ippContainsString(doc_formats, "image/pwg-raster"))
      doc_format = "image/pwg-raster";
    else if (ippContainsString(doc_formats, "application/vnd.hp-pcl"))
      doc_format = "application/vnd.hp-pcl";
  }

 /*
  * Fetch the job...
  */

  request = ippNewRequest(IPP_OP_FETCH_JOB);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  httpReconnect(info->http);

  job_attrs = cupsDoRequest(info->http, request, info->resource);

  if (!job_attrs || cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
  {
   /*
    * Cannot proxy this job...
    */

    if (cupsLastError() == IPP_STATUS_ERROR_NOT_FETCHABLE)
    {
      plogf(pjob, "Job already fetched by another printer.");
      pjob->local_job_state = IPP_JSTATE_COMPLETED;
      ippDelete(job_attrs);
      return;
    }

    plogf(pjob, "Unable to fetch job: %s", cupsLastErrorString());
    pjob->local_job_state = IPP_JSTATE_ABORTED;
    goto update_job;
  }

  request = ippNewRequest(IPP_OP_ACKNOWLEDGE_JOB);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(info->http, request, info->resource));

  if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
  {
    plogf(pjob, "Unable to acknowledge job: %s", cupsLastErrorString());
    pjob->local_job_state = IPP_JSTATE_ABORTED;
    return;
  }

  num_docs = ippGetInteger(ippFindAttribute(job_attrs, "number-of-documents", IPP_TAG_INTEGER), 0);

  plogf(pjob, "Fetched job with %d documents.", num_docs);

 /*
  * Then get the document data for each document in the job...
  */

  pjob->local_job_state = IPP_JSTATE_PROCESSING;

  update_job_status(info, pjob);

  for (doc_number = 1; doc_number <= num_docs; doc_number ++)
  {
    if (pjob->remote_job_state >= IPP_JSTATE_ABORTED)
      break;

    update_document_status(info, pjob, doc_number, IPP_DSTATE_PROCESSING);

    request = ippNewRequest(IPP_OP_FETCH_DOCUMENT);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "document-number", doc_number);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    if (doc_format)
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format-accepted", NULL, doc_format);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression-accepted", NULL, "gzip");

    cupsSendRequest(info->http, request, info->resource, ippLength(request));
    doc_attrs = cupsGetResponse(info->http, info->resource);
    ippDelete(request);

    if (!doc_attrs || cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
    {
      plogf(pjob, "Unable to fetch document #%d: %s", doc_number, cupsLastErrorString());

      pjob->local_job_state = IPP_JSTATE_ABORTED;
      ippDelete(doc_attrs);
      break;
    }

    if (pjob->remote_job_state < IPP_JSTATE_ABORTED)
    {
     /*
      * Send document to local printer...
      */

      send_document(info, pjob, job_attrs, doc_attrs, doc_number);
    }

   /*
    * Acknowledge receipt of the document data...
    */

    ippDelete(doc_attrs);

    request = ippNewRequest(IPP_OP_ACKNOWLEDGE_DOCUMENT);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "document-number", doc_number);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

    ippDelete(cupsDoRequest(info->http, request, info->resource));
  }

 /*
  * Update the job state and return...
  */

  update_job:

  ippDelete(job_attrs);

  update_job_status(info, pjob);
}


/*
 * 'run_printer()' - Run the printer until no work remains.
 */

static void
run_printer(
    http_t     *http,			/* I - Connection to printer */
    const char *printer_uri,		/* I - Printer URI */
    const char *resource,		/* I - Resource path */
    int        subscription_id,		/* I - Subscription ID */
    const char *device_uri,		/* I - Device URI, if any */
    const char *device_uuid,		/* I - Device UUID */
    const char *outformat)		/* I - Output format */
{
  ipp_t			*device_attrs,	/* Device attributes */
			*request,	/* IPP request */
			*response;	/* IPP response */
  ipp_attribute_t	*attr;		/* IPP attribute */
  const char		*name,		/* Attribute name */
			*event;		/* Current event */
  int			job_id;		/* Job ID, if any */
  ipp_jstate_t		job_state;	/* Job state, if any */
  int			seq_number = 1;	/* Current event sequence number */
  int			get_interval;	/* How long to sleep */
  proxy_info_t		info;		/* Information for proxy thread */
  _cups_thread_t	jobs_thread;	/* Job proxy processing thread */


 /*
  * Query the printer...
  */

  device_attrs = get_device_attrs(device_uri);

 /*
  * Initialize the local jobs array...
  */

  jobs = cupsArrayNew3((cups_array_func_t)compare_jobs, NULL, NULL, 0, NULL, (cups_afree_func_t)free);

  memset(&info, 0, sizeof(info));

  info.printer_uri  = printer_uri;
  info.device_uri   = device_uri;
  info.device_uuid  = device_uuid;
  info.device_attrs = device_attrs;
  info.outformat    = outformat;

  jobs_thread = _cupsThreadCreate((_cups_thread_func_t)proxy_jobs, &info);

 /*
  * Register the output device...
  */

  if (!update_device_attrs(http, printer_uri, resource, device_uuid, NULL, device_attrs))
    return;

  while (!stop_running)
  {
   /*
    * See if we have any work to do...
    */

    request = ippNewRequest(IPP_OP_GET_NOTIFICATIONS);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-subscription-ids", subscription_id);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-sequence-numbers", seq_number);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    ippAddBoolean(request, IPP_TAG_OPERATION, "notify-wait", 1);

    if (verbosity)
      plogf(NULL, "Sending Get-Notifications request.");

    response = cupsDoRequest(http, request, resource);

    if (verbosity)
      plogf(NULL, "Get-Notifications response: %s", ippErrorString(cupsLastError()));

    if ((attr = ippFindAttribute(response, "notify-get-interval", IPP_TAG_INTEGER)) != NULL)
      get_interval = ippGetInteger(attr, 0);
    else
      get_interval = 30;

    if (verbosity)
      plogf(NULL, "notify-get-interval=%d", get_interval);

    for (attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response))
    {
      if (ippGetGroupTag(attr) != IPP_TAG_EVENT_NOTIFICATION || !ippGetName(attr))
        continue;

      event     = NULL;
      job_id    = 0;
      job_state = IPP_JSTATE_PENDING;

      while (ippGetGroupTag(attr) == IPP_TAG_EVENT_NOTIFICATION && (name = ippGetName(attr)) != NULL)
      {
	if (!strcmp(name, "notify-subscribed-event") && ippGetValueTag(attr) == IPP_TAG_KEYWORD)
	  event = ippGetString(attr, 0, NULL);
	else if (!strcmp(name, "notify-job-id") && ippGetValueTag(attr) == IPP_TAG_INTEGER)
	  job_id = ippGetInteger(attr, 0);
	else if (!strcmp(name, "job-state") && ippGetValueTag(attr) == IPP_TAG_ENUM)
	  job_state = (ipp_jstate_t)ippGetInteger(attr, 0);
	else if (!strcmp(name, "notify-sequence-number") && ippGetValueTag(attr) == IPP_TAG_INTEGER)
	{
	  int new_seq = ippGetInteger(attr, 0);

	  if (new_seq >= seq_number)
	    seq_number = new_seq + 1;
	}
	else if (!strcmp(name, "printer-state-reasons") && ippContainsString(attr, "identify-printer-requested"))
	  acknowledge_identify_printer(http, printer_uri, resource, device_uuid);

        attr = ippNextAttribute(response);
      }

      if (event && job_id)
      {
        if (!strcmp(event, "job-fetchable") && job_id)
	{
	 /*
	  * Queue up new job...
	  */

          proxy_job_t *pjob = find_job(job_id);

	  if (!pjob)
	  {
	   /*
	    * Not already queued up, make a new one...
	    */

            if ((pjob = (proxy_job_t *)calloc(1, sizeof(proxy_job_t))) != NULL)
            {
             /*
              * Add job and then let the proxy thread know we added something...
              */

              pjob->remote_job_id    = job_id;
              pjob->remote_job_state = job_state;
              pjob->local_job_state  = IPP_JSTATE_PENDING;

	      plogf(pjob, "Job is now fetchable, queuing up.", pjob);

              _cupsRWLockWrite(&jobs_rwlock);
              cupsArrayAdd(jobs, pjob);
              _cupsRWUnlock(&jobs_rwlock);

	      _cupsCondBroadcast(&jobs_cond);
	    }
	    else
	    {
	      plogf(NULL, "Unable to add job %d to jobs queue.", job_id);
	    }
          }
	}
	else if (!strcmp(event, "job-state-changed") && job_id)
	{
	 /*
	  * Update our cached job info...  If the job is currently being
	  * proxied and the job has been canceled or aborted, the code will see
	  * that and stop printing locally.
	  */

	  proxy_job_t *pjob = find_job(job_id);

          if (pjob)
          {
	    pjob->remote_job_state = job_state;

	    plogf(pjob, "Updated remote job-state to '%s'.", ippEnumString("job-state", job_state));

	    _cupsCondBroadcast(&jobs_cond);
	  }
	}
      }
    }

   /*
    * Pause before our next poll of the Infrastructure Printer...
    */

    if (get_interval > 0 && get_interval < 3600)
      sleep((unsigned)get_interval);
    else
      sleep(30);

    httpReconnect(http);
  }

 /*
  * Stop the job proxy thread...
  */

  _cupsCondBroadcast(&jobs_cond);
  _cupsThreadCancel(jobs_thread);
  _cupsThreadWait(jobs_thread);
}


/*
 * 'send_document()' - Send a proxied document to the local printer.
 */

static void
send_document(proxy_info_t *info,	/* I - Proxy information */
              proxy_job_t  *pjob,	/* I - Proxy job */
              ipp_t        *job_attrs,	/* I - Job attributes */
              ipp_t        *doc_attrs,	/* I - Document attributes */
              int          doc_number)	/* I - Document number */
{
  char		scheme[32],		/* URI scheme */
		userpass[256],		/* URI user:pass */
		host[256],		/* URI host */
		resource[256],		/* URI resource */
		service[32];		/* Service port */
  int		port;			/* URI port number */
  http_addrlist_t *list;		/* Address list for socket */
  const char	*doc_compression;	/* Document compression, if any */
  size_t	doc_total = 0;		/* Total bytes read */
  ssize_t	doc_bytes;		/* Bytes read/written */
  char		doc_buffer[16384];	/* Copy buffer */


  if ((doc_compression = ippGetString(ippFindAttribute(doc_attrs, "compression", IPP_TAG_KEYWORD), 0, NULL)) != NULL && !strcmp(doc_compression, "none"))
    doc_compression = NULL;

  if (httpSeparateURI(HTTP_URI_CODING_ALL, info->device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    plogf(pjob, "Invalid device URI '%s'.", info->device_uri);
    pjob->local_job_state = IPP_JSTATE_ABORTED;
    return;
  }

  snprintf(service, sizeof(service), "%d", port);
  if ((list = httpAddrGetList(host, AF_UNSPEC, service)) == NULL)
  {
    plogf(pjob, "Unable to lookup device URI host '%s': %s", host, cupsLastErrorString());
    pjob->local_job_state = IPP_JSTATE_ABORTED;
    return;
  }

  if (!strcmp(scheme, "socket"))
  {
   /*
    * AppSocket connection...
    */

    int		sock;			/* Output socket */

    if (verbosity)
      plogf(pjob, "Connecting to '%s'.", info->device_uri);

    if (!httpAddrConnect2(list, &sock, 30000, NULL))
    {
      plogf(pjob, "Unable to connect to '%s': %s", info->device_uri, cupsLastErrorString());
      pjob->local_job_state = IPP_JSTATE_ABORTED;
      return;
    }

    if (verbosity)
      plogf(pjob, "Connected to '%s'.", info->device_uri);

    if (doc_compression)
      httpSetField(info->http, HTTP_FIELD_CONTENT_ENCODING, doc_compression);

    while ((doc_bytes = cupsReadResponseData(info->http, doc_buffer, sizeof(doc_buffer))) > 0)
    {
      char	*doc_ptr = doc_buffer,	/* Pointer into buffer */
		*doc_end = doc_buffer + doc_bytes;
					/* End of buffer */

      doc_total += (size_t)doc_bytes;

      while (doc_ptr < doc_end)
      {
        if ((doc_bytes = write(sock, doc_ptr, (size_t)(doc_end - doc_ptr))) > 0)
          doc_ptr += doc_bytes;
      }
    }

    close(sock);

    plogf(pjob, "Local job created, %ld bytes.", (long)doc_total);
  }
  else
  {
    int			i;		/* Looping var */
    http_t		*http;		/* Output HTTP connection */
    http_encryption_t	encryption;	/* Encryption mode */
    ipp_t		*request,	/* IPP request */
			*response;	/* IPP response */
    ipp_attribute_t	*attr;		/* Current attribute */
    int			create_job = 0;	/* Support for Create-Job/Send-Document? */
    const char		*doc_format;	/* Document format */
    ipp_jstate_t	job_state;	/* Current job-state value */
    static const char * const pattrs[] =/* Printer attributes we are interested in */
    {
      "compression-supported",
      "operations-supported"
    };
    static const char * const operation[] =
    {					/* Operation attributes to copy */
      "job-name",
      "job-password",
      "job-password-encryption",
      "job-priority"
    };
    static const char * const job_template[] =
    {					/* Job Template attributes to copy */
      "copies",
      "finishings",
      "finishings-col",
      "job-account-id",
      "job-accounting-user-id",
      "media",
      "media-col",
      "multiple-document-handling",
      "orientation-requested",
      "page-ranges",
      "print-color-mode",
      "print-quality",
      "sides"
    };

    if ((doc_format = ippGetString(ippFindAttribute(doc_attrs, "document-format", IPP_TAG_MIMETYPE), 0, NULL)) == NULL)
      doc_format = "application/octet-stream";

   /*
    * Connect to the IPP/IPPS printer...
    */

    if (port == 443 || !strcmp(scheme, "ipps"))
      encryption = HTTP_ENCRYPTION_ALWAYS;
    else
      encryption = HTTP_ENCRYPTION_IF_REQUESTED;

    if (verbosity)
      plogf(pjob, "Connecting to '%s'.", info->device_uri);

    if ((http = httpConnect2(host, port, list, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
    {
      plogf(pjob, "Unable to connect to '%s': %s\n", info->device_uri, cupsLastErrorString());
      pjob->local_job_state = IPP_JSTATE_ABORTED;
      return;
    }

    if (verbosity)
      plogf(pjob, "Connected to '%s'.", info->device_uri);

   /*
    * See if it supports Create-Job + Send-Document...
    */

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

    response = cupsDoRequest(http, request, resource);

    if ((attr = ippFindAttribute(response, "operations-supported", IPP_TAG_ENUM)) == NULL)
    {
      plogf(pjob, "Unable to get list of supported operations from printer.");
      pjob->local_job_state = IPP_JSTATE_ABORTED;
      ippDelete(response);
      httpClose(http);
      return;
    }

    create_job = ippContainsInteger(attr, IPP_OP_CREATE_JOB) && ippContainsInteger(attr, IPP_OP_SEND_DOCUMENT);

    if (doc_compression && !ippContainsString(ippFindAttribute(response, "compression-supported", IPP_TAG_KEYWORD), doc_compression))
    {
     /*
      * Decompress raster data to send to printer without compression...
      */

      httpSetField(info->http, HTTP_FIELD_CONTENT_ENCODING, doc_compression);
      doc_compression = NULL;
    }

    ippDelete(response);

   /*
    * Create the job and start printing...
    */

    request = ippNewRequest(create_job ? IPP_OP_CREATE_JOB : IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    if (!create_job)
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, doc_format);
    if (!create_job && doc_compression)
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, doc_compression);
    for (i = 0; i < (int)(sizeof(operation) / sizeof(operation[0])); i ++)
    {
      if ((attr = ippFindAttribute(job_attrs, operation[i], IPP_TAG_ZERO)) != NULL)
      {
	attr = ippCopyAttribute(request, attr, 0);
	ippSetGroupTag(request, &attr, IPP_TAG_OPERATION);
      }
    }

    for (i = 0; i < (int)(sizeof(job_template) / sizeof(job_template[0])); i ++)
    {
      if ((attr = ippFindAttribute(job_attrs, job_template[i], IPP_TAG_ZERO)) != NULL)
	ippCopyAttribute(request, attr, 0);
    }

    if (verbosity)
    {
      plogf(pjob, "%s", ippOpString(ippGetOperation(request)));

      for (attr = ippFirstAttribute(request); attr; attr = ippNextAttribute(request))
      {
        const char *name = ippGetName(attr);	/* Attribute name */

        if (!name)
        {
          plogf(pjob, "----");
          continue;
	}

        ippAttributeString(attr, doc_buffer, sizeof(doc_buffer));

        plogf(pjob, "%s %s '%s'", name, ippTagString(ippGetValueTag(attr)), doc_buffer);
      }
    }

    if (create_job)
    {
      response = cupsDoRequest(http, request, resource);
      pjob->local_job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);
      ippDelete(response);

      if (pjob->local_job_id <= 0)
      {
	plogf(pjob, "Unable to create local job: %s", cupsLastErrorString());
	pjob->local_job_state = IPP_JSTATE_ABORTED;
	httpClose(http);
	return;
      }

      request = ippNewRequest(IPP_OP_SEND_DOCUMENT);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->device_uri);
      ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->local_job_id);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, doc_format);
      if (doc_compression)
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, doc_compression);
      ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", 1);

      if (verbosity)
      {
	plogf(pjob, "%s", ippOpString(ippGetOperation(request)));

	for (attr = ippFirstAttribute(request); attr; attr = ippNextAttribute(request))
	{
	  const char *name = ippGetName(attr);	/* Attribute name */

	  if (!name)
	  {
	    plogf(pjob, "----");
	    continue;
	  }

	  ippAttributeString(attr, doc_buffer, sizeof(doc_buffer));

	  plogf(pjob, "%s %s '%s'", name, ippTagString(ippGetValueTag(attr)), doc_buffer);
	}
      }
    }

    if (cupsSendRequest(http, request, resource, 0) == HTTP_STATUS_CONTINUE)
    {
      while ((doc_bytes = cupsReadResponseData(info->http, doc_buffer, sizeof(doc_buffer))) > 0)
      {
	doc_total += (size_t)doc_bytes;

        if (cupsWriteRequestData(http, doc_buffer, (size_t)doc_bytes) != HTTP_STATUS_CONTINUE)
          break;
      }
    }

    response = cupsGetResponse(http, resource);

    if (!pjob->local_job_id)
      pjob->local_job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);

    job_state = (ipp_jstate_t)ippGetInteger(ippFindAttribute(response, "job-state", IPP_TAG_ENUM), 0);

    ippDelete(response);

    if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
    {
      plogf(pjob, "Unable to create local job: %s", cupsLastErrorString());
      pjob->local_job_state = IPP_JSTATE_ABORTED;
      httpClose(http);
      return;
    }

    plogf(pjob, "Local job %d created, %ld bytes.", pjob->local_job_id, (long)doc_total);

    while (pjob->remote_job_state < IPP_JSTATE_CANCELED && job_state < IPP_JSTATE_CANCELED)
    {
      request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->device_uri);
      ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->local_job_id);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", NULL, "job-state");

      response = cupsDoRequest(http, request, resource);

      if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
	job_state = IPP_JSTATE_COMPLETED;
      else
        job_state = (ipp_jstate_t)ippGetInteger(ippFindAttribute(response, "job-state", IPP_TAG_ENUM), 0);

      ippDelete(response);
    }

    if (pjob->remote_job_state == IPP_JSTATE_CANCELED)
    {
     /*
      * Cancel locally...
      */

      plogf(pjob, "Canceling job locally.");

      request = ippNewRequest(IPP_OP_CANCEL_JOB);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->device_uri);
      ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->local_job_id);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

      ippDelete(cupsDoRequest(http, request, resource));

      if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
	plogf(pjob, "Unable to cancel local job: %s", cupsLastErrorString());

      pjob->local_job_state = IPP_JSTATE_CANCELED;
    }

    httpClose(http);
  }

  update_document_status(info, pjob, doc_number, IPP_DSTATE_COMPLETED);
}


/*
 * 'sighandler()' - Handle termination signals so we can clean up...
 */

static void
sighandler(int sig)			/* I - Signal */
{
  (void)sig;

  stop_running = 1;
}


/*
 * 'update_device_attrs()' - Update device attributes on the server.
 */

static int				/* O - 1 on success, 0 on failure */
update_device_attrs(
    http_t     *http,			/* I - Connection to server */
    const char *printer_uri,		/* I - Printer URI */
    const char *resource,		/* I - Resource path */
    const char *device_uuid,		/* I - Device UUID */
    ipp_t      *old_attrs,		/* I - Old attributes */
    ipp_t      *new_attrs)		/* I - New attributes */
{
  int			i,		/* Looping var */
			result;		/* Result of comparison */
  ipp_t			*request;	/* IPP request */
  ipp_attribute_t	*attr;		/* New attribute */
  const char		*name;		/* New attribute name */


 /*
  * Update the configuration of the output device...
  */

  request = ippNewRequest(IPP_OP_UPDATE_OUTPUT_DEVICE_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  for (attr = ippFirstAttribute(new_attrs); attr; attr = ippNextAttribute(new_attrs))
  {
   /*
    * Add any attributes that have changed...
    */

    if (ippGetGroupTag(attr) != IPP_TAG_PRINTER || (name = ippGetName(attr)) == NULL)
      continue;

    for (i = 0, result = 1; i < (int)(sizeof(printer_attrs) / sizeof(printer_attrs[0])) && result > 0; i ++)
    {
      if ((result = strcmp(name, printer_attrs[i])) == 0)
      {
       /*
        * This is an attribute we care about...
	*/

        if (!attrs_are_equal(ippFindAttribute(old_attrs, name, ippGetValueTag(attr)), attr))
	  ippCopyAttribute(request, attr, 1);
      }
    }
  }

  httpReconnect(http);
  ippDelete(cupsDoRequest(http, request, resource));

  if (cupsLastError() != IPP_STATUS_OK)
  {
    plogf(NULL, "Unable to update the output device with '%s': %s", printer_uri, cupsLastErrorString());
    return (0);
  }

  return (1);
}


/*
 * 'update_document_status()' - Update the document status.
 */

static void
update_document_status(
    proxy_info_t *info,			/* I - Proxy info */
    proxy_job_t  *pjob,			/* I - Proxy job */
    int          doc_number,		/* I - Document number */
    ipp_dstate_t doc_state)		/* I - New document-state value */
{
  ipp_t	*request;			/* IPP request */


  request = ippNewRequest(IPP_OP_UPDATE_DOCUMENT_STATUS);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "document-number", doc_number);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "output-device-document-state", doc_state);

  ippDelete(cupsDoRequest(info->http, request, info->resource));

  if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
    plogf(pjob, "Unable to update the state for document #%d: %s", doc_number, cupsLastErrorString());
}


/*
 * 'update_job_status()' - Update the job status.
 */

static void
update_job_status(proxy_info_t *info,	/* I - Proxy info */
                  proxy_job_t  *pjob)	/* I - Proxy job */
{
  ipp_t	*request;			/* IPP request */


  request = ippNewRequest(IPP_OP_UPDATE_JOB_STATUS);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, info->printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", pjob->remote_job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "output-device-uuid", NULL, info->device_uuid);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "output-device-job-state", pjob->local_job_state);

  ippDelete(cupsDoRequest(info->http, request, info->resource));

  if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
    plogf(pjob, "Unable to update the job state: %s", cupsLastErrorString());
}


/*
 * 'usage()' - Show program usage and exit.
 */

static void
usage(int status)			/* O - Exit status */
{
  puts("Usage: ippproxy [options] printer-uri");
  puts("Options:");
  puts("  -d device-uri   Specify local printer device URI.");
  puts("  -m mime/type    Specify the desired print format.");
  puts("  -p password     Password for authentication.");
  puts("                  (Also IPPPROXY_PASSWORD environment variable)");
  puts("  -u username     Username for authentication.");
  puts("  -v              Be verbose.");
  puts("  --help          Show this help.");
  puts("  --version       Show program version.");

  exit(status);
}

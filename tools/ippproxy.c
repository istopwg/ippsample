/*
 * IPP Proxy implementation for HP PCL and IPP Everywhere printers.
 *
 * Copyright 2016-2017 by the IEEE-ISTO Printer Working Group.
 * Copyright 2014-2017 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <cups/cups.h>
#include <cups/thread-private.h>


/*
 * Local types...
 */

typedef struct proxy_info_s
{
  const char	*printer_uri,
		*device_uri,
		*device_uuid;
} proxy_info_t;

typedef struct proxy_job_s
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
					/* Condition for jobs array */
static _cups_mutex_t	jobs_mutex = _CUPS_MUTEX_INITIALIZER;
					/* Mutex for jobs array */

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

static int	attrs_are_equal(ipp_attribute_t *a, ipp_attribute_t *b);
static int	compare_jobs(proxy_job_t *a, proxy_job_t *b);
static proxy_job_t *copy_job(proxy_job_t *src);
static ipp_t	*create_media_col(const char *media, const char *source, const char *type, int width, int length, int margins);
static ipp_t	*create_media_size(int width, int length);
static void	deregister_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uuid);
static int	fetch_job(http_t *http, const char *printer_uri, const char *resource, int job_id, const char *device_uri, const char *device_uuid, ipp_t *device_attrs);
static ipp_t	*get_device_attrs(const char *device_uri);
static void	make_uuid(const char *device_uri, char *uuid, size_t uuidsize);
static const char *password_cb(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
static void	*proxy_jobs(proxy_info_t *info);
static int	register_printer(http_t *http, const char *printer_uri, const char *resource, const char *device_uri, const char *device_uuid);
static void	run_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uri, const char *device_uuid);
static void	sighandler(int sig);
static int	update_device_attrs(http_t *http, const char *printer_uri, const char *resource, const char *device_uuid, ipp_t *old_attrs, ipp_t *new_attrs);
static void	usage(int status) __attribute__((noreturn));


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
		*password = NULL,	/* Password, if any */
		*printer_uri = NULL;	/* Infrastructure printer URI */
  cups_dest_t	*dest;			/* Destination for printer URI */
  http_t	*http;			/* Connection to printer */
  char		resource[1024];		/* Resource path */
  int		subscription_id;		/* Event subscription ID */
  char		device_uuid[45];		/* Device UUID URN */


 /*
  * Parse command-line...
  */

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-' && argv[i][1] != '-')
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

	      device_uri = argv[i];
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
    else if (!strcmp(argv[i], "--help"))
      usage(0);
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

  while ((http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE, 30000, NULL, resource, sizeof(resource), NULL, NULL)) == NULL)
  {
    fprintf(stderr, "ippproxy: Infrastructure printer at '%s' is not responding, retrying in 30 seconds...\n", printer_uri);
    sleep(30);
  }

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

  run_printer(http, printer_uri, resource, subscription_id, device_uri, device_uuid);

  deregister_printer(http, printer_uri, resource, subscription_id, device_uuid);
  httpClose(http);

  return (0);
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
 * 'copy_job()' - Create a job of a job.
 */

static proxy_job_t *			/* O - New job */
copy_job(proxy_job_t *src)		/* I - Original job */
{
  proxy_job_t *dst;			/* New job */


  if ((dst = malloc(sizeof(proxy_job_t))) != NULL)
    memcpy(dst, src, sizeof(proxy_job_t));

  return (dst);
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
 * 'fetch_job()' - Fetch and print a job.
 */

static int				/* O - 1 on success, 0 on failure */
fetch_job(http_t     *http,		/* I - Connection to printer */
          const char *printer_uri,	/* I - Printer URI */
	  const char *resource,		/* I - Resource path */
	  int        job_id,		/* I - Job ID */
	  const char *device_uri,	/* I - Device URI */
	  const char *device_uuid,	/* I - Device UUID */
	  ipp_t      *device_attrs)	/* I - Device attributes */
{
  (void)http;
  (void)printer_uri;
  (void)resource;
  (void)job_id;
  (void)device_uri;
  (void)device_uuid;
  (void)device_attrs;

  return (0);
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

    cups_dest_t	*dest;			/* Destination for printer URI */
    http_t	*http;			/* Connection to printer */
    char	resource[1024];		/* Resource path */
    ipp_t	*request;		/* Get-Printer-Attributes request */

   /*
    * Connect to the printer...
    */

    dest = cupsGetDestWithURI("device", device_uri);

    while ((http = cupsConnectDest(dest, CUPS_DEST_FLAGS_NONE, 30000, NULL, resource, sizeof(resource), NULL, NULL)) == NULL)
    {
      fprintf(stderr, "ippproxy: Device at '%s' is not responding, retrying in 30 seconds...\n", device_uri);
      sleep(30);
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
  }
  else
  {
   /*
    * Must be a socket-based HP PCL laser printer, report just
    * standard size information...
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
  * Build a version 3 UUID conforming to RFC 4122 based on the
  * SHA-256 hash of the device URI
  */

  cupsHashData("sha-256", device_uri, strlen(device_uri), sha256, sizeof(sha256));

  snprintf(uuid, uuidsize, "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", sha256[0], sha256[1], sha256[2], sha256[3], sha256[4], sha256[5], (sha256[6] & 15) | 0x30, sha256[7], (sha256[8] & 0x3f) | 0x40, sha256[9], sha256[10], sha256[11], sha256[12], sha256[13], sha256[14], sha256[15]);

  fprintf(stderr, "ippproxy: UUID for '%s' is '%s'.\n", device_uri, uuid);
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
 * 'proxy_jobs()' - Relay jobs to the local printer.
 */

static void *				/* O - Thread exit status */
proxy_jobs(proxy_info_t *info)	/* I - Printer and device info */
{
  (void)info;

  for (;;)
  {
    _cupsMutexLock(&jobs_mutex);
    _cupsCondWait(&jobs_cond, &jobs_mutex, 0.0);

    _cupsMutexUnlock(&jobs_mutex);
  }

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
    "document-config-change",
    "document-state-change",
    "job-config-change",
    "job-state-change",
    "printer-config-change",
    "printer-state-change"
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
    fprintf(stderr, "ippproxy: Unable to monitor events on '%s': %s\n", printer_uri, cupsLastErrorString());
    return (0);
  }

  if ((attr = ippFindAttribute(response, "notify-subscription-id", IPP_TAG_INTEGER)) != NULL)
  {
    subscription_id = ippGetInteger(attr, 0);
  }
  else
  {
    fprintf(stderr, "ippproxy: Unable to monitor events on '%s': No notify-subscription-id returned.\n", printer_uri);
  }

  ippDelete(response);

  return (subscription_id);
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
    const char *device_uuid)		/* I - Device UUID */
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
  * Initialize the local jobs array...
  */

  jobs = cupsArrayNew3((cups_array_func_t)compare_jobs, NULL, NULL, 0, (cups_acopy_func_t)copy_job, (cups_afree_func_t)free);

  info.printer_uri = printer_uri;
  info.device_uri  = device_uri;
  info.device_uuid = device_uuid;

  jobs_thread = _cupsThreadCreate((_cups_thread_func_t)proxy_jobs, &info);

 /*
  * Query the printer...
  */

  device_attrs = get_device_attrs(device_uri);

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

    response = cupsDoRequest(http, request, resource);

    if ((attr = ippFindAttribute(response, "notify-get-interval", IPP_TAG_INTEGER)) != NULL)
      get_interval = ippGetInteger(attr, 0);
    else
      get_interval = 30;

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

	  if (new_seq > seq_number)
	    seq_number = new_seq;
	}

        attr = ippNextAttribute(response);
      }

      if (event)
      {
        if (!strcmp(event, "job-fetchable") && job_id)
	{
	  /* TODO: queue up fetches */
	  fetch_job(http, printer_uri, resource, job_id, device_uri, device_uuid, device_attrs);
	}
	else if (!strcmp(event, "job-state-changed") && job_id)
	{
	  /* TODO: Support cancellation */
	  if (job_state == IPP_JSTATE_CANCELED || job_state == IPP_JSTATE_ABORTED)
	  {
	    /* Cancel job locally if it is printing... */
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
  }

 /*
  * Stop the job proxy thread...
  */

  _cupsThreadCancel(jobs_thread);
  _cupsThreadWait(jobs_thread);
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

  ippDelete(cupsDoRequest(http, request, resource));

  if (cupsLastError() != IPP_STATUS_OK)
  {
    fprintf(stderr, "ippproxy: Unable to update the output device with '%s': %s\n", printer_uri, cupsLastErrorString());
    return (0);
  }

  return (1);
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
  puts("  -p password     Password for authentication.");
  puts("                  (Also IPPPROXY_PASSWORD environment variable)");
  puts("  -u username     Username for authentication.");
  puts("  -v              Be verbose.");
  puts("  --help          Show this help.");

  exit(status);
}

/*
 * IPP Proxy implementation for HP PCL and IPP Everywhere printers.
 *
 * Copyright 2014-2016 by Apple Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <cups/cups.h>


/*
 * Local globals...
 */

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


/*
 * Local functions...
 */

static int	attrs_are_equal(ipp_attribute_t *a, ipp_attribute_t *b);
static void	deregister_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uuid);
static ipp_t	*get_device_attrs(const char *device_uri);
static void	make_uuid(const char *device_uri, char *uuid, size_t uuidsize);
static const char *password_cb(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
static int	register_printer(http_t *http, const char *printer_uri, const char *resource, const char *device_uri, const char *device_uuid);
static void	run_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uri, const char *device_uuid, const char *command);
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
		*command = NULL,		/* Command */
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
	  case 'c' : /* -c command */
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ippproxy: Missing command after '-c' option.\n", stderr);
		usage(1);
	      }

	      command = argv[i];
	      break;

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

  if (!device_uri && !command)
  {
    fputs("ippproxy: Must specify '-c' and/or '-d'.\n", stderr);
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

  run_printer(http, printer_uri, resource, subscription_id, device_uri, device_uuid, command);

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
  }
  else
  {
   /*
    * Must be a socket-based HP PCL laser printer, report just
    * standard size information...
    */
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
  ipp_attribute_t *attr;			/* Attribute in response */
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
    const char *device_uuid,		/* I - Device UUID */
    const char *command)			/* I - Command, if any */
{
  ipp_t			*device_attrs,	/* Device attributes */
			*request,	/* IPP request */
			*response;	/* IPP response */
  ipp_attribute_t	*attr;		/* IPP attribute */
  int			seq_number = 1;	/* Current event sequence number */
  int			get_interval;	/* How long to sleep */


  (void)command;

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

    /* do work */

   /*
    * Pause before our next poll of the Infrastructure Printer...
    */

    if (get_interval > 0 && get_interval < 3600)
      sleep((unsigned)get_interval);
    else
      sleep(30);
  }
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
  puts("  -c command      Specify a command to run for each job.");
  puts("  -d device-uri   Specify local printer device URI.");
  puts("  -p password     Password for authentication.");
  puts("                  (Also IPPPROXY_PASSWORD environment variable)");
  puts("  -u username     Username for authentication.");
  puts("  --help          Show this help.");

  exit(status);
}

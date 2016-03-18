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

static int	stop_running = 0;


/*
 * Local functions...
 */

static void	deregister_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uuid);
static void	make_uuid(const char *device_uri, char *uuid, size_t uuidsize);
static const char *password_cb(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
static int	register_printer(http_t *http, const char *printer_uri, const char *resource, const char *device_uri, const char *device_uuid);
static void	run_printer(http_t *http, const char *printer_uri, const char *resource, int subscription_id, const char *device_uri, const char *device_uuid, const char *command);
static void	sighandler(int sig);
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
 * 'deregister_pritner()' - Unregister the output device and cancel the printer subscription.
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


  (void)device_uuid;

  request = ippNewRequest(IPP_OP_CANCEL_SUBSCRIPTION);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-subscription-id", subscription_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(http, request, resource));
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
  (void)http;
  (void)printer_uri;
  (void)resource;
  (void)device_uri;
  (void)device_uuid;

  return (0);
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
  ipp_t			*request,	/* IPP request */
			*response;	/* IPP response */
  ipp_attribute_t	*attr;		/* IPP attribute */
  int			seq_number = 1;	/* Current event sequence number */
  int			get_interval;	/* How long to sleep */


  (void)device_uri;
  (void)device_uuid;
  (void)command;

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

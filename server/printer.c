/*
 * Printer object code for sample IPP server implementation.
 *
 * Copyright 2010-2015 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#include "ippserver.h"


/*
 * Local functions...
 */

static int		compare_active_jobs(server_job_t *a, server_job_t *b);
static int		compare_completed_jobs(server_job_t *a, server_job_t *b);
static int		compare_devices(server_device_t *a, server_device_t *b);
static int		compare_jobs(server_job_t *a, server_job_t *b);


/*
 * 'serverCopyPrinterStateReasons()' - Copy printer-state-reasons values.
 */

void
serverCopyPrinterStateReasons(
    ipp_t          *ipp,		/* I - Attributes */
    ipp_tag_t      group_tag,		/* I - Group */
    server_printer_t *printer)		/* I - Printer */
{
  server_preason_t	creasons = printer->state_reasons | printer->dev_reasons;
					/* Combined reasons */


  if (creasons == SERVER_PREASON_NONE)
  {
    ippAddString(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-state-reasons", NULL, "none");
  }
  else
  {
    int			i,		/* Looping var */				num_reasons = 0;/* Number of reasons */
    server_preason_t	reason;		/* Current reason */
    const char		*reasons[32];	/* Reason strings */

    for (i = 0, reason = 1; i < (int)(sizeof(server_preasons) / sizeof(server_preasons[0])); i ++, reason <<= 1)
    {
      if (creasons & reason)
	reasons[num_reasons ++] = server_preasons[i];
    }

    ippAddStrings(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-state-reasons", num_reasons, NULL, reasons);
  }
}


/*
 * 'serverCreatePrinter()' - Create, register, and listen for connections to a
 *                      printer object.
 */

server_printer_t *			/* O - Printer */
serverCreatePrinter(const char *servername,	/* I - Server hostname (NULL for default) */
               int        port,		/* I - Port number */
               const char *name,	/* I - printer-name */
	       const char *directory,	/* I - Spool directory */
	       const char *proxy_user,	/* I - Proxy account username */
	       const char *proxy_pass)	/* I - Proxy account password */
{
  server_printer_t	*printer;	/* Printer */
  char			uri[1024],	/* Printer URI */
			adminurl[1024],	/* printer-more-info URI */
			supplyurl[1024],/* printer-supply-info-uri URI */
			uuid[128];	/* printer-uuid */
  int			k_supported;	/* Maximum file size supported */
#ifdef HAVE_STATVFS
  struct statvfs	spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#elif defined(HAVE_STATFS)
  struct statfs		spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#endif /* HAVE_STATVFS */
  static const char * const versions[] =/* ipp-versions-supported values */
  {
    "1.0",
    "1.1",
    "2.0"
  };
  static const char * const features[] =/* ipp-features-supported values */
  {
    "document-object",
    "ipp-everywhere",
    "infrastructure-printer",
    "page-overrides"
  };
  static const int	ops[] =		/* operations-supported values */
  {
    IPP_OP_PRINT_JOB,
    IPP_OP_PRINT_URI,
    IPP_OP_VALIDATE_JOB,
    IPP_OP_CREATE_JOB,
    IPP_OP_SEND_DOCUMENT,
    IPP_OP_SEND_URI,
    IPP_OP_CANCEL_JOB,
    IPP_OP_GET_JOB_ATTRIBUTES,
    IPP_OP_GET_JOBS,
    IPP_OP_GET_PRINTER_ATTRIBUTES,
    IPP_OP_GET_PRINTER_SUPPORTED_VALUES,
    IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS,
    IPP_OP_CREATE_JOB_SUBSCRIPTIONS,
    IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES,
    IPP_OP_GET_SUBSCRIPTIONS,
    IPP_OP_RENEW_SUBSCRIPTION,
    IPP_OP_CANCEL_SUBSCRIPTION,
    IPP_OP_GET_NOTIFICATIONS,
    IPP_OP_GET_DOCUMENT_ATTRIBUTES,
    IPP_OP_GET_DOCUMENTS,
    IPP_OP_CANCEL_MY_JOBS,
    IPP_OP_CLOSE_JOB,
    IPP_OP_IDENTIFY_PRINTER,
    IPP_OP_VALIDATE_DOCUMENT,
    IPP_OP_ACKNOWLEDGE_DOCUMENT,
    IPP_OP_ACKNOWLEDGE_IDENTIFY_PRINTER,
    IPP_OP_ACKNOWLEDGE_JOB,
    IPP_OP_FETCH_DOCUMENT,
    IPP_OP_FETCH_JOB,
    IPP_OP_GET_OUTPUT_DEVICE_ATTRIBUTES,
    IPP_OP_UPDATE_ACTIVE_JOBS,
    IPP_OP_UPDATE_DOCUMENT_STATUS,
    IPP_OP_UPDATE_JOB_STATUS,
    IPP_OP_UPDATE_OUTPUT_DEVICE_ATTRIBUTES,
    IPP_OP_DEREGISTER_OUTPUT_DEVICE
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
  static const char * const notify_attributes[] =
  {					/* notify-attributes-supported */
    "printer-state-change-time",
    "notify-lease-expiration-time",
    "notify-subscriber-user-name"
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


 /*
  * Allocate memory for the printer...
  */

  if ((printer = calloc(1, sizeof(server_printer_t))) == NULL)
  {
    perror("ippserver: Unable to allocate memory for printer");
    return (NULL);
  }

  printer->ipv4           = -1;
  printer->ipv6           = -1;
  printer->name           = strdup(name);
  printer->directory      = strdup(directory);
  printer->hostname       = strdup(servername);
  printer->port           = port;
  printer->start_time     = time(NULL);
  printer->config_time    = printer->start_time;
  printer->state          = IPP_PSTATE_IDLE;
  printer->state_reasons  = SERVER_PREASON_NONE;
  printer->state_time     = printer->start_time;
  printer->jobs           = cupsArrayNew3((cups_array_func_t)compare_jobs, NULL, NULL, 0, NULL, (cups_afree_func_t)serverDeleteJob);
  printer->active_jobs    = cupsArrayNew((cups_array_func_t)compare_active_jobs, NULL);
  printer->completed_jobs = cupsArrayNew((cups_array_func_t)compare_completed_jobs, NULL);
  printer->next_job_id    = 1;

  httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
		  printer->hostname, printer->port, "/ipp/print");
  printer->uri    = strdup(uri);
  printer->urilen = strlen(uri);

  if (proxy_user)
    printer->proxy_user = strdup(proxy_user);
  if (proxy_pass)
    printer->proxy_pass = strdup(proxy_pass);

  printer->devices = cupsArrayNew((cups_array_func_t)compare_devices, NULL);

  _cupsRWInit(&(printer->rwlock));

 /*
  * Create the listener sockets...
  */

  if ((printer->ipv4 = serverCreateListener(AF_INET, printer->port)) < 0)
  {
    perror("Unable to create IPv4 listener");
    goto bad_printer;
  }

  if ((printer->ipv6 = serverCreateListener(AF_INET6, printer->port)) < 0)
  {
    perror("Unable to create IPv6 listener");
    goto bad_printer;
  }

 /*
  * Prepare values for the printer attributes...
  */

  httpAssembleURI(HTTP_URI_CODING_ALL, adminurl, sizeof(adminurl), "http", NULL, printer->hostname, printer->port, "/");
  httpAssembleURI(HTTP_URI_CODING_ALL, supplyurl, sizeof(supplyurl), "http", NULL, printer->hostname, printer->port, "/supplies");

  if (Verbosity)
  {
    fprintf(stderr, "printer-more-info=\"%s\"\n", adminurl);
    fprintf(stderr, "printer-supply-info-uri=\"%s\"\n", supplyurl);
    fprintf(stderr, "printer-uri=\"%s\"\n", uri);
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
  * Create the printer attributes.  This list of attributes is sorted to improve
  * performance when the client provides a requested-attributes attribute...
  */

  printer->attrs = ippNew();

  /* charset-configured */
  ippAddString(printer->attrs, IPP_TAG_PRINTER,
               IPP_CONST_TAG(IPP_TAG_CHARSET),
               "charset-configured", NULL, "utf-8");

  /* charset-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER,
                IPP_CONST_TAG(IPP_TAG_CHARSET),
                "charset-supported", sizeof(charsets) / sizeof(charsets[0]),
		NULL, charsets);

  /* compression-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER,
                IPP_CONST_TAG(IPP_TAG_KEYWORD),
	        "compression-supported",
	        (int)(sizeof(compressions) / sizeof(compressions[0])), NULL,
	        compressions);

  /* generated-natural-language-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER,
               IPP_CONST_TAG(IPP_TAG_LANGUAGE),
               "generated-natural-language-supported", NULL, "en");

  /* ipp-features-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(features) / sizeof(features[0]), NULL, features);

  /* ipp-versions-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", sizeof(versions) / sizeof(versions[0]), NULL, versions);

  /* ippget-event-life */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "ippget-event-life", 300);

  /* job-ids-supported */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "job-ids-supported", 1);

  /* job-k-octets-supported */
  ippAddRange(printer->attrs, IPP_TAG_PRINTER, "job-k-octets-supported", 0,
	      k_supported);

  /* job-priority-default */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-priority-default", 50);

  /* job-priority-supported */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-priority-supported", 100);

  /* multiple-document-jobs-supported */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "multiple-document-jobs-supported", 0);

  /* multiple-operation-time-out */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "multiple-operation-time-out", 60);

  /* multiple-operation-time-out-action */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-operation-time-out-action", NULL, "abort-job");

  /* natural-language-configured */
  ippAddString(printer->attrs, IPP_TAG_PRINTER,
               IPP_CONST_TAG(IPP_TAG_LANGUAGE),
               "natural-language-configured", NULL, "en");

  /* notify-attributes-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-attributes-supported", sizeof(notify_attributes) / sizeof(notify_attributes[0]), NULL, notify_attributes);

  /* notify-events-default */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-default", NULL, "job-completed");

  /* notify-events-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-supported", sizeof(server_events) / sizeof(server_events[0]), NULL, server_events);

  /* notify-lease-duration-default */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "notify-lease-duration-default", 86400);

  /* notify-lease-duration-supported */
  ippAddRange(printer->attrs, IPP_TAG_PRINTER, "notify-lease-duration-supported", 0, SERVER_NOTIFY_LEASE_DURATION_MAX);

  /* notify-max-events-supported */
  ippAddInteger(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "notify-lease-duration-default", (int)(sizeof(server_events) / sizeof(server_events[0])));

  /* notify-pull-method-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-pull-method-supported", NULL, "ippget");

  /* operations-supported */
  ippAddIntegers(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM,
		 "operations-supported", sizeof(ops) / sizeof(ops[0]), ops);

  /* printer-get-attributes-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-get-attributes-supported", NULL, "document-format");

  /* printer-is-accepting-jobs */
  ippAddBoolean(printer->attrs, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);

  /* printer-info */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, name);

  /* printer-more-info */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-more-info", NULL, adminurl);

  /* printer-name */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, name);

  /* printer-supply-info-uri */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-supply-info-uri", NULL, supplyurl);

  /* printer-uri-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", NULL, uri);

  /* printer-uuid */
  httpAssembleUUID(printer->hostname, port, name, 0, uuid, sizeof(uuid));
  ippAddString(printer->attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid);

  /* reference-uri-scheme-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER,
                IPP_CONST_TAG(IPP_TAG_URISCHEME),
                "reference-uri-schemes-supported",
                (int)(sizeof(reference_uri_schemes_supported) /
                      sizeof(reference_uri_schemes_supported[0])),
                NULL, reference_uri_schemes_supported);

  /* uri-authentication-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER,
               IPP_CONST_TAG(IPP_TAG_KEYWORD),
               "uri-authentication-supported", NULL, "basic");

  /* uri-security-supported */
  ippAddString(printer->attrs, IPP_TAG_PRINTER,
               IPP_CONST_TAG(IPP_TAG_KEYWORD),
               "uri-security-supported", NULL, "tls");

  /* which-jobs-supported */
  ippAddStrings(printer->attrs, IPP_TAG_PRINTER,
                IPP_CONST_TAG(IPP_TAG_KEYWORD),
                "which-jobs-supported",
                sizeof(which_jobs) / sizeof(which_jobs[0]), NULL, which_jobs);

  serverLogAttributes("Printer", printer->attrs, 0);

 /*
  * Return it!
  */

  return (printer);


 /*
  * If we get here we were unable to create the printer...
  */

  bad_printer:

  serverDeletePrinter(printer);
  return (NULL);
}


/*
 * 'serverDeletePrinter()' - Unregister, close listen sockets, and free all memory
 *                      used by a printer object.
 */

void
serverDeletePrinter(server_printer_t *printer)	/* I - Printer */
{
  _cupsRWLockWrite(&printer->rwlock);

  if (printer->ipv4 >= 0)
    close(printer->ipv4);

  if (printer->ipv6 >= 0)
    close(printer->ipv6);

  if (printer->name)
    free(printer->name);
  if (printer->directory)
    free(printer->directory);
  if (printer->hostname)
    free(printer->hostname);
  if (printer->uri)
    free(printer->uri);
  if (printer->proxy_user)
    free(printer->proxy_user);
  if (printer->proxy_pass)
    free(printer->proxy_pass);


  ippDelete(printer->attrs);
  ippDelete(printer->dev_attrs);

  cupsArrayDelete(printer->active_jobs);
  cupsArrayDelete(printer->completed_jobs);
  cupsArrayDelete(printer->jobs);
  cupsArrayDelete(printer->subscriptions);

  _cupsRWDeinit(&printer->rwlock);

  free(printer);
}


/*
 * 'serverGetPrinterStateReasonsBits()' - Get the bits associated with "printer-state-reasons" values.
 */

server_preason_t			/* O - Bits */
serverGetPrinterStateReasonsBits(
    ipp_attribute_t *attr)		/* I - "printer-state-reasons" bits */
{
  int			i, j,		/* Looping vars */
			count;		/* Number of "printer-state-reasons" values */
  const char		*keyword;	/* "printer-state-reasons" value */
  server_preason_t	preasons = SERVER_PREASON_NONE;
					/* Bits for "printer-state-reasons" values */


  count = ippGetCount(attr);
  for (i = 0; i < count; i ++)
  {
    keyword = ippGetString(attr, i, NULL);

    for (j = 0; j < (int)(sizeof(server_preasons) / sizeof(server_preasons[0])); j ++)
    {
      if (!strcmp(keyword, server_preasons[j]))
      {
        preasons |= 1 << j;
	break;
      }
    }
  }

  return (preasons);
}


/*
 * 'serverRunPrinter()' - Run the printer service.
 */

void
serverRunPrinter(server_printer_t *printer)	/* I - Printer */
{
  int		num_fds;		/* Number of file descriptors */
  struct pollfd	polldata[3];		/* poll() data */
  int		timeout;		/* Timeout for poll() */
  server_client_t	*client;		/* New client */


 /*
  * Setup poll() data for the Bonjour service socket and IPv4/6 listeners...
  */

  polldata[0].fd     = printer->ipv4;
  polldata[0].events = POLLIN;

  polldata[1].fd     = printer->ipv6;
  polldata[1].events = POLLIN;

  num_fds = 2;

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
      if ((client = serverCreateClient(printer, printer->ipv4)) != NULL)
      {
	if (!_cupsThreadCreate((_cups_thread_func_t)serverProcessClient, client))
	{
	  perror("Unable to create client thread");
	  serverDeleteClient(client);
	}
      }
    }

    if (polldata[1].revents & POLLIN)
    {
      if ((client = serverCreateClient(printer, printer->ipv6)) != NULL)
      {
	if (!_cupsThreadCreate((_cups_thread_func_t)serverProcessClient, client))
	{
	  perror("Unable to create client thread");
	  serverDeleteClient(client);
	}
      }
    }

   /*
    * Clean out old jobs...
    */

    serverCleanJobs(printer);
  }
}


/*
 * 'compare_active_jobs()' - Compare two active jobs.
 */

static int				/* O - Result of comparison */
compare_active_jobs(server_job_t *a,	/* I - First job */
                    server_job_t *b)	/* I - Second job */
{
  int	diff;				/* Difference */


  if ((diff = b->priority - a->priority) == 0)
    diff = b->id - a->id;

  return (diff);
}


/*
 * 'compare_completed_jobs()' - Compare two completed jobs.
 */

static int				/* O - Result of comparison */
compare_completed_jobs(server_job_t *a,	/* I - First job */
                       server_job_t *b)	/* I - Second job */
{
  int	diff;				/* Difference */


  if ((diff = (int)(a->completed - b->completed)) == 0)
    diff = b->id - a->id;

  return (diff);
}


/*
 * 'compare_devices()' - Compare two devices...
 */

static int				/* O - Result of comparison */
compare_devices(server_device_t *a,	/* I - First device */
                server_device_t *b)	/* I - Second device */
{
  return (strcmp(a->uuid, b->uuid));
}


/*
 * 'compare_jobs()' - Compare two jobs.
 */

static int				/* O - Result of comparison */
compare_jobs(server_job_t *a,		/* I - First job */
             server_job_t *b)		/* I - Second job */
{
  return (b->id - a->id);
}

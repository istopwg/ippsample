/*
 * Configuration file support for sample IPP server implementation.
 *
 * Copyright © 2015-2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2015-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"
#include <cups/file.h>
#include <cups/dir.h>
#include <fnmatch.h>
#include <cups/ipp-private.h>


/*
 * Local globals...
 */

static _cups_mutex_t	printer_mutex = _CUPS_MUTEX_INITIALIZER;


/*
 * Local functions...
 */

static int		attr_cb(_ipp_file_t *f, server_pinfo_t *pinfo, const char *attr);
static int		compare_lang(server_lang_t *a, server_lang_t *b);
static int		compare_printers(server_printer_t *a, server_printer_t *b);
static server_lang_t	*copy_lang(server_lang_t *a);
#ifdef HAVE_AVAHI
static void		dnssd_client_cb(AvahiClient *c, AvahiClientState state, void *userdata);
#endif /* HAVE_AVAHI */
static int		error_cb(_ipp_file_t *f, server_pinfo_t *pinfo, const char *error);
static void		free_lang(server_lang_t *a);
static int		load_system(const char *conf);
static int		token_cb(_ipp_file_t *f, _ipp_vars_t *vars, server_pinfo_t *pinfo, const char *token);


/*
 * 'serverCleanAllJobs()' - Clean old jobs for all printers...
 */

void
serverCleanAllJobs(void)
{
  server_printer_t  *printer;             /* Current printer */


  serverLog(SERVER_LOGLEVEL_DEBUG, "Cleaning old jobs.");

  _cupsMutexLock(&printer_mutex);

  for (printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
    serverCleanJobs(printer);

  _cupsMutexUnlock(&printer_mutex);
}


/*
 * 'serverDNSSDInit()' - Initialize DNS-SD registrations.
 */

void
serverDNSSDInit(void)
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
 * 'serverFinalizeConfiguration()' - Make final configuration choices.
 */

int					/* O - 1 on success, 0 on failure */
serverFinalizeConfiguration(void)
{
  char			local[1024];	/* Local hostname */


 /*
  * Default hostname...
  */

  if (!ServerName && httpGetHostname(NULL, local, sizeof(local)))
    ServerName = strdup(local);

  if (!ServerName)
    ServerName = strdup("localhost");

#ifdef HAVE_SSL
 /*
  * Setup TLS certificate for server...
  */

  cupsSetServerCredentials(KeychainPath, ServerName, 1);
#endif /* HAVE_SSL */

 /*
  * Default directories...
  */

  if (!DataDirectory)
  {
    char	directory[1024];	/* New directory */
    const char	*tmpdir;		/* Temporary directory */

#ifdef WIN32
    if ((tmpdir = getenv("TEMP")) == NULL)
      tmpdir = "C:/TEMP";
#elif defined(__APPLE__)
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/private/tmp";
#else
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/tmp";
#endif /* WIN32 */

    snprintf(directory, sizeof(directory), "%s/ippserver.%d", tmpdir, (int)getpid());

    if (mkdir(directory, 0755) && errno != EEXIST)
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Unable to create default data directory \"%s\": %s", directory, strerror(errno));
      return (0);
    }

    serverLog(SERVER_LOGLEVEL_INFO, "Using default data directory \"%s\".", directory);

    DataDirectory = strdup(directory);
  }

  if (!SpoolDirectory)
  {
    SpoolDirectory = strdup(DataDirectory);

    serverLog(SERVER_LOGLEVEL_INFO, "Using default spool directory \"%s\".", DataDirectory);
  }

 /*
  * Initialize Bonjour...
  */

  serverDNSSDInit();

 /*
  * Apply default listeners if none are specified...
  */

  if (!Listeners)
  {
#ifdef WIN32
   /*
    * Windows is almost always used as a single user system, so use a default port
    * number of 8631.
    */

    if (!DefaultPort)
      DefaultPort = 8631;

#else
   /*
    * Use 8000 + UID mod 1000 for the default port number...
    */

    if (!DefaultPort)
      DefaultPort = 8000 + ((int)getuid() % 1000);
#endif /* WIN32 */

    serverLog(SERVER_LOGLEVEL_INFO, "Using default listeners for %s:%d.", ServerName, DefaultPort);

    if (!serverCreateListeners(strcmp(ServerName, "localhost") ? NULL : "localhost", DefaultPort))
      return (0);
  }

  return (1);
}


/*
 * 'serverFindPrinter()' - Find a printer by resource...
 */

server_printer_t *			/* O - Printer or NULL */
serverFindPrinter(const char *resource)	/* I - Resource path */
{
  server_printer_t	key,		/* Search key */
			*match = NULL;	/* Matching printer */


  _cupsMutexLock(&printer_mutex);
  if (cupsArrayCount(Printers) == 1 || !strcmp(resource, "/ipp/print"))
  {
   /*
    * Just use the first printer...
    */

    match = cupsArrayFirst(Printers);
    if (strcmp(match->resource, resource) && strcmp(resource, "/ipp/print"))
      match = NULL;
  }
  else
  {
    key.resource = (char *)resource;
    match        = (server_printer_t *)cupsArrayFind(Printers, &key);
  }
  _cupsMutexUnlock(&printer_mutex);

  return (match);
}


/*
 * 'serverLoadAttributes()' - Load printer attributes from a file.
 *
 * Syntax is based on ipptool format:
 *
 *    ATTR value-tag name value
 *    ATTR value-tag name value,value,...
 *    AUTHTYPE "scheme"
 *    COMMAND "/path/to/command"
 *    DEVICE-URI "uri"
 *    MAKE "manufacturer"
 *    MODEL "model name"
 *    PROXY-USER "username"
 *    STRINGS lang filename.strings
 *
 * AUTH schemes are "none" for no authentication or "basic" for HTTP Basic
 * authentication.
 *
 * DEVICE-URI values can be "socket", "ipp", or "ipps" URIs.
 */

int					/* O - 1 on success, 0 on failure */
serverLoadAttributes(
    const char     *filename,		/* I - File to load */
    server_pinfo_t *pinfo)		/* I - Printer information */
{
  _ipp_vars_t	vars;			/* IPP variables */


  _ippVarsInit(&vars, (_ipp_fattr_cb_t)attr_cb, (_ipp_ferror_cb_t)error_cb, (_ipp_ftoken_cb_t)token_cb);

  pinfo->attrs = _ippFileParse(&vars, filename, (void *)pinfo);

  _ippVarsDeinit(&vars);

  return (pinfo->attrs != NULL);
}


/*
 * 'serverLoadConfiguration()' - Load the server configuration file.
 */

int					/* O - 1 if successful, 0 on error */
serverLoadConfiguration(
    const char *directory)		/* I - Configuration directory */
{
  cups_dir_t	*dir;			/* Directory pointer */
  cups_dentry_t	*dent;			/* Directory entry */
  char		filename[1024],		/* Configuration file/directory */
                iconname[1024],		/* Icon file */
		resource[1024],		/* Resource path */
                *ptr;			/* Pointer into filename */
  server_printer_t *printer;		/* Printer */
  server_pinfo_t pinfo;			/* Printer information */


 /*
  * First read the system configuration file, if any...
  */

  snprintf(filename, sizeof(filename), "%s/system.conf", directory);
  if (!load_system(filename))
    return (0);

  if (!serverFinalizeConfiguration())
    return (0);

 /*
  * Then see if there are any print queues...
  */

  snprintf(filename, sizeof(filename), "%s/print", directory);
  if ((dir = cupsDirOpen(filename)) != NULL)
  {
    serverLog(SERVER_LOGLEVEL_INFO, "Loading printers from \"%s\".", filename);

    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if ((ptr = dent->filename + strlen(dent->filename) - 5) >= dent->filename && !strcmp(ptr, ".conf"))
      {
       /*
        * Load the conf file, with any associated icon image.
        */

        serverLog(SERVER_LOGLEVEL_INFO, "Loading printer from \"%s\".", dent->filename);

        snprintf(filename, sizeof(filename), "%s/print/%s", directory, dent->filename);
        *ptr = '\0';

        memset(&pinfo, 0, sizeof(pinfo));

        snprintf(iconname, sizeof(iconname), "%s/print/%s.png", directory, dent->filename);
        if (!access(iconname, R_OK))
          pinfo.icon = strdup(iconname);

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, &pinfo)) == NULL)
            continue;

	  if (!Printers)
	    Printers = cupsArrayNew((cups_array_func_t)compare_printers, NULL);

	  cupsArrayAdd(Printers, printer);
	}
      }
      else if (!strstr(dent->filename, ".png"))
        serverLog(SERVER_LOGLEVEL_INFO, "Skipping \"%s\".", dent->filename);
    }

    cupsDirClose(dir);
  }

 /*
  * Finally, see if there are any 3D print queues...
  */

  snprintf(filename, sizeof(filename), "%s/print3d", directory);
  if ((dir = cupsDirOpen(filename)) != NULL)
  {
    serverLog(SERVER_LOGLEVEL_INFO, "Loading 3D printers from \"%s\".", filename);

    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if ((ptr = dent->filename + strlen(dent->filename) - 5) >= dent->filename && !strcmp(ptr, ".conf"))
      {
       /*
        * Load the conf file, with any associated icon image.
        */

        serverLog(SERVER_LOGLEVEL_INFO, "Loading 3D printer from \"%s\".", dent->filename);

        snprintf(filename, sizeof(filename), "%s/print3d/%s", directory, dent->filename);
        *ptr = '\0';

        memset(&pinfo, 0, sizeof(pinfo));

        snprintf(iconname, sizeof(iconname), "%s/print3d/%s.png", directory, dent->filename);
        if (!access(iconname, R_OK))
          pinfo.icon = strdup(iconname);

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print3d/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, &pinfo)) == NULL)
          continue;

	  if (!Printers)
	    Printers = cupsArrayNew((cups_array_func_t)compare_printers, NULL);

	  cupsArrayAdd(Printers, printer);
	}
      }
      else if (!strstr(dent->filename, ".png"))
        serverLog(SERVER_LOGLEVEL_INFO, "Skipping \"%s\".", dent->filename);
    }

    cupsDirClose(dir);
  }

  return (1);
}


/*
 * 'attr_cb()' - Determine whether an attribute should be loaded.
 */

static int				/* O - 1 to use, 0 to ignore */
attr_cb(_ipp_file_t    *f,		/* I - IPP file */
        server_pinfo_t *pinfo,		/* I - Printer information */
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
    "job-settable-attributes-supported",
    "pages-per-minute",
    "pages-per-minute-color",
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
    "printer-finisher",
    "printer-finisher-description",
    "printer-finisher-supplies",
    "printer-finisher-supplies-description",
    "printer-get-attributes-supported",
    "printer-icons",
    "printer-id",
    "printer-input-tray",
    "printer-is-accepting-jobs",
    "printer-message-date-time",
    "printer-message-from-operator",
    "printer-message-time",
    "printer-more-info",
    "printer-output-tray",
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
    "printer-supply",
    "printer-supply-description",
    "printer-supply-info-uri",
    "printer-up-time",
    "printer-uri-supported",
    "printer-uuid",
    "printer-xri-supported",
    "queued-job-count",
    "uri-authentication-supported",
    "uri-security-supported",
    "xri-authentication-supported",
    "xri-security-supported",
    "xri-uri-scheme-supported"
  };


  (void)f;
  (void)pinfo;

  for (i = 0, result = 1; i < (int)(sizeof(ignored) / sizeof(ignored[0])); i ++)
  {
    if ((result = strcmp(attr, ignored[i])) <= 0)
      break;
  }

  return (result != 0);
}


/*
 * 'compare_lang()' - Compare two languages.
 */

static int				/* O - Result of comparison */
compare_lang(server_lang_t *a,		/* I - First localization */
             server_lang_t *b)		/* I - Second localization */
{
  return (strcmp(a->lang, b->lang));
}


/*
 * 'compare_printers()' - Compare two printers.
 */

static int				/* O - Result of comparison */
compare_printers(server_printer_t *a,	/* I - First printer */
                 server_printer_t *b)	/* I - Second printer */
{
  return (strcmp(a->resource, b->resource));
}


/*
 * 'copy_lang()' - Copy a localization.
 */

static server_lang_t *			/* O - New localization */
copy_lang(server_lang_t *a)		/* I - Localization to copy */
{
  server_lang_t	*b;			/* New localization */


  if ((b = calloc(1, sizeof(server_lang_t))) != NULL)
  {
    b->lang     = strdup(a->lang);
    b->filename = strdup(a->filename);
  }

  return (b);
}


#ifdef HAVE_AVAHI
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
        fprintf(stderr, "Ignore Avahi state %d.\n", state);
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
#endif /* HAVE_AVAHI */


/*
 * 'error_cb()' - Log an error message.
 */

static int				/* O - 1 to continue, 0 to stop */
error_cb(_ipp_file_t    *f,		/* I - IPP file data */
         server_pinfo_t *pinfo,		/* I - Printer information */
         const char     *error)		/* I - Error message */
{
  (void)f;
  (void)pinfo;

  serverLog(SERVER_LOGLEVEL_ERROR, "%s", error);

  return (1);
}


/*
 * 'free_lang()' - Free a localization.
 */

static void
free_lang(server_lang_t *a)		/* I - Localization */
{
  free(a->lang);
  free(a->filename);
  free(a);
}


/*
 * 'load_system()' - Load the system configuration file.
 */

static int				/* O - 1 on success, 0 on failure */
load_system(const char *conf)		/* I - Configuration file */
{
  cups_file_t	*fp;			/* File pointer */
  int		status = 1,		/* Return value */
		linenum = 0;		/* Current line number */
  char		line[1024],		/* Line from file */
		*value;			/* Pointer to value on line */


  if ((fp = cupsFileOpen(conf, "r")) == NULL)
    return (errno == ENOENT);

  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
    if (!value)
    {
      fprintf(stderr, "ippserver: Missing value on line %d of \"%s\".\n", linenum, conf);
      status = 0;
      break;
    }

    if (!_cups_strcasecmp(line, "DataDirectory"))
    {
      if (access(value, R_OK))
      {
        fprintf(stderr, "ippserver: Unable to access DataDirectory \"%s\": %s\n", value, strerror(errno));
        status = 0;
        break;
      }

      DataDirectory = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "DefaultPrinter"))
    {
      if (DefaultPrinter)
      {
        fprintf(stderr, "ippserver: Extra DefaultPrinter seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      DefaultPrinter = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "Encryption"))
    {
      if (!_cups_strcasecmp(value, "always"))
        Encryption = HTTP_ENCRYPTION_ALWAYS;
      else if (!_cups_strcasecmp(value, "ifrequested"))
        Encryption = HTTP_ENCRYPTION_IF_REQUESTED;
      else if (!_cups_strcasecmp(value, "never"))
        Encryption = HTTP_ENCRYPTION_NEVER;
      else if (!_cups_strcasecmp(value, "required"))
        Encryption = HTTP_ENCRYPTION_REQUIRED;
      else
      {
        fprintf(stderr, "ippserver: Bad Encryption value \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }
    }
    else if (!_cups_strcasecmp(line, "KeepFiles"))
    {
      KeepFiles = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "on");
    }
    else if (!_cups_strcasecmp(line, "Listen"))
    {
      char	*ptr;			/* Pointer into host value */
      int	port;			/* Port number */

      if ((ptr = strrchr(value, ':')) != NULL && !isdigit(ptr[1] & 255))
      {
        fprintf(stderr, "ippserver: Bad Listen value \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      if (ptr)
      {
        *ptr++ = '\0';
        port   = atoi(ptr);
      }
      else
        port = 8000 + ((int)getuid() % 1000);

      if (!serverCreateListeners(value, port))
      {
        status = 0;
        break;
      }
    }
    else if (!_cups_strcasecmp(line, "LogFile"))
    {
      if (!_cups_strcasecmp(value, "stderr"))
        LogFile = NULL;
      else
        LogFile = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "LogLevel"))
    {
      if (!_cups_strcasecmp(value, "error"))
        LogLevel = SERVER_LOGLEVEL_ERROR;
      else if (!_cups_strcasecmp(value, "info"))
        LogLevel = SERVER_LOGLEVEL_INFO;
      else if (!_cups_strcasecmp(value, "debug"))
        LogLevel = SERVER_LOGLEVEL_DEBUG;
      else
      {
        fprintf(stderr, "ippserver: Bad LogLevel value \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }
    }
    else if (!_cups_strcasecmp(line, "MaxCompletedJobs"))
    {
      if (!isdigit(*value & 255))
      {
        fprintf(stderr, "ippserver: Bad MaxCompletedJobs value \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      MaxCompletedJobs = atoi(value);
    }
    else if (!_cups_strcasecmp(line, "MaxJobs"))
    {
      if (!isdigit(*value & 255))
      {
        fprintf(stderr, "ippserver: Bad MaxJobs value \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      MaxJobs = atoi(value);
    }
    else if (!_cups_strcasecmp(line, "SpoolDirectory"))
    {
      if (access(value, R_OK))
      {
        fprintf(stderr, "ippserver: Unable to access SpoolDirectory \"%s\": %s\n", value, strerror(errno));
        status = 0;
        break;
      }

      SpoolDirectory = strdup(value);
    }
    else
    {
      fprintf(stderr, "ippserver: Unknown directive \"%s\" on line %d.\n", line, linenum);
    }
  }

  cupsFileClose(fp);

  return (status);
}


/*
 * 'token_cb()' - Process ippserver-specific config file tokens.
 */

static int				/* O - 1 to continue, 0 to stop */
token_cb(_ipp_file_t    *f,		/* I - IPP file data */
         _ipp_vars_t    *vars,		/* I - IPP variables */
         server_pinfo_t *pinfo,		/* I - Printer information */
         const char     *token)		/* I - Current token */
{
  char	temp[1024],			/* Temporary string */
	value[1024];			/* Value string */


  if (!token)
  {
   /*
    * NULL token means do the initial setup - create an empty IPP message and
    * return...
    */

    f->attrs = ippNew();

    return (1);
  }
  else if (!_cups_strcasecmp(token, "AUTHTYPE"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing AuthType value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->auth_type = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "COMMAND"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Command value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->command = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "DEVICEURI"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing DeviceURI value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->device_uri = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "OUTPUTFORMAT"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing OutputFormat value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->output_format = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "MAKE"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Make value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->make = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "MODEL"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Model value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->model = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "PROXYUSER"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing ProxyUser value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->proxy_user = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "STRINGS"))
  {
    server_lang_t	lang;			/* New localization */
    char		stringsfile[1024];	/* Strings filename */

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing STRINGS language on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing STRINGS filename on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, stringsfile, temp, sizeof(stringsfile));

    lang.lang     = value;
    lang.filename = stringsfile;

    if (!pinfo->strings)
      pinfo->strings = cupsArrayNew3((cups_array_func_t)compare_lang, NULL, NULL, 0, (cups_acopy_func_t)copy_lang, (cups_afree_func_t)free_lang);

    cupsArrayAdd(pinfo->strings, &lang);

    serverLog(SERVER_LOGLEVEL_DEBUG, "Added strings file \"%s\" for language \"%s\".", stringsfile, value);
  }
  else
  {
    serverLog(SERVER_LOGLEVEL_ERROR, "Unknown directive \"%s\" on line %d of \"%s\".", token, f->linenum, f->filename);
    return (0);
  }

  return (1);
}

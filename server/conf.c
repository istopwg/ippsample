/*
 * Configuration file support for sample IPP server implementation.
 *
 * Copyright © 2015-2019 by the IEEE-ISTO Printer Working Group
 * Copyright © 2015-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"
#include <cups/file.h>
#include <cups/dir.h>
#ifndef _WIN32
#  include <fnmatch.h>
#  include <pwd.h>
#  include <grp.h>
#endif /* !_WIN32 */
#include <cups/ipp-private.h>


/*
 * Local globals...
 */

static char		*default_printer = NULL;


/*
 * Local functions...
 */

static void		add_document_privacy(void);
static void		add_job_privacy(void);
static void		add_subscription_privacy(void);
static int		attr_cb(_ipp_file_t *f, server_pinfo_t *pinfo, const char *attr);
static int		compare_lang(server_lang_t *a, server_lang_t *b);
static int		compare_printers(server_printer_t *a, server_printer_t *b);
static server_icc_t	*copy_icc(server_icc_t *a);
static server_lang_t	*copy_lang(server_lang_t *a);
static void		create_system_attributes(void);
#ifdef HAVE_AVAHI
static void		dnssd_client_cb(AvahiClient *c, AvahiClientState state, void *userdata);
#endif /* HAVE_AVAHI */
static void		dnssd_init(void);
static int		error_cb(_ipp_file_t *f, server_pinfo_t *pinfo, const char *error);
static int		finalize_system(void);
static void		free_icc(server_icc_t *a);
static void		free_lang(server_lang_t *a);
static int		load_system(const char *conf);
static ipp_t		*parse_collection(_ipp_file_t *f, _ipp_vars_t *v, void *user_data);
static int		parse_value(_ipp_file_t *f, _ipp_vars_t *v, void *user_data, ipp_t *ipp, ipp_attribute_t **attr, int element);
static void		print_escaped_string(cups_file_t *fp, const char *s, size_t len);
static void		print_ipp_attr(cups_file_t *fp, ipp_attribute_t *attr, int indent);
static void		save_printer(server_printer_t *printer, const char *directory);
static int		token_cb(_ipp_file_t *f, _ipp_vars_t *vars, server_pinfo_t *pinfo, const char *token);


/*
 * 'serverAddPrinter()' - Add a printer object to the list of printers.
 */

void
serverAddPrinter(
    server_printer_t *printer)		/* I - Printer to add */
{
  _cupsRWLockWrite(&SystemRWLock);

  if (!Printers)
    Printers = cupsArrayNew((cups_array_func_t)compare_printers, NULL);

  cupsArrayAdd(Printers, printer);

  _cupsRWUnlock(&SystemRWLock);
}


/*
 * 'serverAddStringsFile()' - Add a strings file to a printer.
 */

void
serverAddStringsFile(
    server_printer_t  *printer,		/* I - Printer */
    const char        *language,	/* I - Language */
    server_resource_t *resource)	/* I - Strings resource file */
{
  server_lang_t	lang;			/* New localization */

  lang.lang     = (char *)language;
  lang.resource = resource;

  _cupsRWLockWrite(&printer->rwlock);

  if (!printer->pinfo.strings)
    printer->pinfo.strings = cupsArrayNew3((cups_array_func_t)compare_lang, NULL, NULL, 0, (cups_acopy_func_t)copy_lang, (cups_afree_func_t)free_lang);

  if (!cupsArrayFind(printer->pinfo.strings, &lang))
    cupsArrayAdd(printer->pinfo.strings, &lang);

  _cupsRWUnlock(&printer->rwlock);
}


/*
 * 'serverCleanAllJobs()' - Clean old jobs for all printers...
 */

void
serverCleanAllJobs(void)
{
  server_printer_t  *printer;             /* Current printer */


  serverLog(SERVER_LOGLEVEL_DEBUG, "Cleaning old jobs.");

  _cupsRWLockRead(&PrintersRWLock);

  for (printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
    serverCleanJobs(printer);

  _cupsRWUnlock(&PrintersRWLock);
}


/*
 * 'serverCreateSystem()' - Load the server configuration file and create the
 *                          System object..
 */

int					/* O - 1 if successful, 0 on error */
serverCreateSystem(
    const char *directory)		/* I - Configuration directory */
{
  cups_dir_t	*dir;			/* Directory pointer */
  cups_dentry_t	*dent;			/* Directory entry */
  char		confdir[1024],		/* Configuration directory */
  		filename[1024],		/* Configuration file */
                iconname[1024],		/* Icon file */
		resource[1024],		/* Resource path */
                *ptr;			/* Pointer into filename */
  server_printer_t *printer;		/* Printer */
  server_pinfo_t pinfo;			/* Printer information */


  SystemStartTime = SystemConfigChangeTime = time(NULL);

  if (directory)
  {
   /*
    * First read the system configuration file, if any...
    */

    snprintf(filename, sizeof(filename), "%s/system.conf", directory);
    if (!load_system(filename))
      return (0);
  }

  if (!finalize_system())
    return (0);

  if (!directory)
  {
    DefaultPrinter = NULL;
    return (1);
  }

 /*
  * Then see if there are any print queues...
  */

  if (StateDirectory)
  {
   /*
    * See if we have saved printer state information...
    */

    snprintf(confdir, sizeof(confdir), "%s/print", StateDirectory);

    if (access(confdir, 0))
      snprintf(confdir, sizeof(confdir), "%s/print", directory);
  }
  else
    snprintf(confdir, sizeof(confdir), "%s/print", directory);

  if ((dir = cupsDirOpen(confdir)) != NULL)
  {
    serverLog(SERVER_LOGLEVEL_INFO, "Loading printers from \"%s\".", filename);

    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if ((ptr = strrchr(dent->filename, '.')) == NULL)
        ptr = "";

      if (!strcmp(ptr, ".conf"))
      {
       /*
        * Load the conf file, with any associated icon image.
        */

        serverLog(SERVER_LOGLEVEL_INFO, "Loading printer from \"%s\".", dent->filename);

        snprintf(filename, sizeof(filename), "%s/%s", confdir, dent->filename);
        *ptr = '\0';

        memset(&pinfo, 0, sizeof(pinfo));

        pinfo.print_group       = SERVER_GROUP_NONE;
	pinfo.proxy_group       = SERVER_GROUP_NONE;
	pinfo.initial_accepting = 1;
	pinfo.initial_state     = IPP_PSTATE_IDLE;
	pinfo.initial_reasons   = SERVER_PREASON_NONE;
        pinfo.web_forms         = 1;

        snprintf(iconname, sizeof(iconname), "%s/%s.png", confdir, dent->filename);
        if (!access(iconname, R_OK))
        {
          pinfo.icon = strdup(iconname);
	}
	else if (StateDirectory)
	{
	  snprintf(iconname, sizeof(iconname), "%s/print/%s.png", directory, dent->filename);
	  if (!access(iconname, R_OK))
	    pinfo.icon = strdup(iconname);
	}

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, dent->filename, &pinfo, 0)) == NULL)
            continue;

          printer->state         = pinfo.initial_state;
          printer->state_reasons = pinfo.initial_reasons;
          printer->is_accepting  = pinfo.initial_accepting;

          serverAddPrinter(printer);
	}
      }
      else if (strcmp(ptr, ".png") && strcmp(ptr, ".strings"))
        serverLog(SERVER_LOGLEVEL_INFO, "Skipping \"%s\".", dent->filename);
    }

    cupsDirClose(dir);
  }

 /*
  * Finally, see if there are any 3D print queues...
  */

  if (StateDirectory)
  {
   /*
    * See if we have saved printer state information...
    */

    snprintf(confdir, sizeof(confdir), "%s/print3d", StateDirectory);

    if (access(confdir, 0))
      snprintf(confdir, sizeof(confdir), "%s/print3d", directory);
  }
  else
    snprintf(confdir, sizeof(confdir), "%s/print3d", directory);

  if ((dir = cupsDirOpen(confdir)) != NULL)
  {
    serverLog(SERVER_LOGLEVEL_INFO, "Loading 3D printers from \"%s\".", filename);

    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if ((ptr = strrchr(dent->filename, '.')) == NULL)
        ptr = "";

      if (!strcmp(ptr, ".conf"))
      {
       /*
        * Load the conf file, with any associated icon image.
        */

        serverLog(SERVER_LOGLEVEL_INFO, "Loading 3D printer from \"%s\".", dent->filename);

        snprintf(filename, sizeof(filename), "%s/%s", confdir, dent->filename);
        *ptr = '\0';

        memset(&pinfo, 0, sizeof(pinfo));

        pinfo.print_group       = SERVER_GROUP_NONE;
	pinfo.proxy_group       = SERVER_GROUP_NONE;
	pinfo.initial_accepting = 1;
	pinfo.initial_state     = IPP_PSTATE_IDLE;
	pinfo.initial_reasons   = SERVER_PREASON_NONE;
        pinfo.web_forms         = 1;

        snprintf(iconname, sizeof(iconname), "%s/%s.png", confdir, dent->filename);
        if (!access(iconname, R_OK))
        {
          pinfo.icon = strdup(iconname);
	}
	else if (StateDirectory)
	{
	  snprintf(iconname, sizeof(iconname), "%s/print3d/%s.png", directory, dent->filename);
	  if (!access(iconname, R_OK))
	    pinfo.icon = strdup(iconname);
	}

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print3d/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, dent->filename, &pinfo, 0)) == NULL)
          continue;

          printer->state         = pinfo.initial_state;
          printer->state_reasons = pinfo.initial_reasons;
          printer->is_accepting  = pinfo.initial_accepting;

          serverAddPrinter(printer);
	}
      }
      else if (strcmp(ptr, ".png") && strcmp(ptr, ".strings"))
        serverLog(SERVER_LOGLEVEL_INFO, "Skipping \"%s\".", dent->filename);
    }

    cupsDirClose(dir);
  }

  if (default_printer)
  {
    for (printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
      if (!strcmp(printer->name, default_printer))
        break;

    DefaultPrinter = printer;
  }
  else
  {
    DefaultPrinter = NULL;
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


  _cupsRWLockRead(&PrintersRWLock);
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
  _cupsRWUnlock(&PrintersRWLock);

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
  char		temp[32];		/* Temporary string */


 /*
  * Setup callbacks and variables for the printer configuration file...
  *
  * The following additional variables are supported:
  *
  * - SERVERNAME: The host name of the server.
  * - SERVERPORT: The default port of the server.
  */

  _ippVarsInit(&vars, (_ipp_fattr_cb_t)attr_cb, (_ipp_ferror_cb_t)error_cb, (_ipp_ftoken_cb_t)token_cb);
  _ippVarsSet(&vars, "SERVERNAME", ServerName);
  snprintf(temp, sizeof(temp), "%d", DefaultPort);
  _ippVarsSet(&vars, "SERVERPORT", temp);

 /*
  * Load attributes and values for the printer...
  */

  pinfo->attrs = _ippFileParse(&vars, filename, (void *)pinfo);

 /*
  * Free memory and return...
  */

  _ippVarsDeinit(&vars);

  return (pinfo->attrs != NULL);
}


/*
 * 'serverSaveSystem()' - Save the state of the system.
 */

void
serverSaveSystem(void)
{
  server_printer_t	*printer;	/* Current printer */
  char			filename[1024];	/* Output file/directory */


  if (!StateDirectory)
    return;

  serverLog(SERVER_LOGLEVEL_INFO, "Saving system state to \"%s\".", StateDirectory);

  _cupsRWLockRead(&PrintersRWLock);

  for (printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
  {
    if (!strncmp(printer->resource, "/ipp/print/", 11))
      snprintf(filename, sizeof(filename), "%s/print", StateDirectory);
    else
      snprintf(filename, sizeof(filename), "%s/print3d", StateDirectory);

    if (access(filename, 0))
      mkdir(filename, 0777);

    save_printer(printer, filename);
  }

  _cupsRWUnlock(&PrintersRWLock);
}


/*
 * 'add_document_privacy()' - Add document privacy attributes.
 */

static void
add_document_privacy(void)
{
  int		i;			/* Looping var */
  char		temp[1024],		/* Temporary copy of value */
		*start,			/* Start of value */
		*ptr;			/* Pointer into value */
  ipp_attribute_t *privattrs = NULL;	/* document-privacy-attributes */
  static const char * const description[] =
  {					/* document-description attributes */
    "compression",
    "copies-actual",
    "cover-back-actual",
    "cover-front-actual",
    "current-page-order",
    "date-time-at-completed",
    "date-time-at-creation",
    "date-time-at-processing",
    "detailed-status-messages",
    "document-access-errors",
    "document-charset",
    "document-digital-signature",
    "document-format",
    "document-format-details",
    "document-format-detected",
    "document-format-version",
    "document-format-version-detected",
    "document-message",
    "document-metadata",
    "document-name",
    "document-natural-language",
    "document-state",
    "document-state-message",
    "document-state-reasons",
    "document-uri",
    "errors-count",
    "finishings-actual",
    "finishings-col-actual",
    "force-front-side-actual",
    "imposition-template-actual",
    "impressions",
    "impressions-col",
    "impressions-completed",
    "impressions-completed-col",
    "impressions-completed-current-copy",
    "insert-sheet-actual",
    "k-octets",
    "k-octets-processed",
    "last-document",
    "materials-col-actual",
    "media-actual",
    "media-col-actual",
    "media-input-tray-check-actual",
    "media-sheets",
    "media-sheets-col",
    "media-sheets-completed",
    "media-sheets-completed-col",
    "more-info",
    "multiple-object-handling-actual",
    "number-up-actual",
    "orientation-requested-actual",
    "output-bin-actual",
    "output-device-assigned",
    "overrides-actual",
    "page-delivery-actual",
    "page-order-received-actual",
    "page-ranges-actual",
    "pages",
    "pages-col",
    "pages-completed",
    "pages-completed-col",
    "pages-completed-current-copy",
    "platform-temperature-actual",
    "presentation-direction-number-up-actual",
    "print-accuracy-actual",
    "print-base-actual",
    "print-color-mode-actual",
    "print-content-optimize-actual",
    "print-objects-actual",
    "print-quality-actual",
    "print-rendering-intent-actual",
    "print-scaling-actual",
    "print-supports-actual",
    "printer-resolution-actual",
    "printer-up-time",
    "separator-sheets-actual",
    "sheet-completed-copy-number",
    "sides-actual",
    "time-at-completed",
    "time-at-creation",
    "time-at-processing",
    "x-image-position-actual",
    "x-image-shift-actual",
    "x-side1-image-shift-actual",
    "x-side2-image-shift-actual",
    "y-image-position-actual",
    "y-image-shift-actual",
    "y-side1-image-shift-actual",
    "y-side2-image-shift-actual"
  };
  static const char * const template[] =
  {					/* document-template attributes */
    "copies",
    "cover-back",
    "cover-front",
    "feed-orientation",
    "finishings",
    "finishings-col",
    "font-name-requested",
    "font-size-requested",
    "force-front-side",
    "imposition-template",
    "insert-sheet",
    "materials-col",
    "media",
    "media-col",
    "media-input-tray-check",
    "multiple-document-handling",
    "multiple-object-handling",
    "number-up",
    "orientation-requested",
    "overrides",
    "page-delivery",
    "page-order-received",
    "page-ranges",
    "pages-per-subset",
    "pdl-init-file",
    "platform-temperature",
    "presentation-direction-number-up",
    "print-accuracy",
    "print-base",
    "print-color-mode",
    "print-content-optimize",
    "print-objects",
    "print-quality",
    "print-rendering-intent",
    "print-scaling",
    "print-supports",
    "printer-resolution",
    "separator-sheets",
    "sheet-collate",
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


  if (!strcmp(DocumentPrivacyAttributes, "none"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "document-privacy-attributes", NULL, "none");
  }
  else if (!strcmp(DocumentPrivacyAttributes, "all"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "document-privacy-attributes", NULL, "all");

    DocumentPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
    for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
      cupsArrayAdd(DocumentPrivacyArray, (void *)description[i]);
    for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
      cupsArrayAdd(DocumentPrivacyArray, (void *)template[i]);
  }
  else
  {
    DocumentPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    strlcpy(temp, DocumentPrivacyAttributes, sizeof(temp));

    ptr = temp;
    while (*ptr)
    {
      start = ptr;
      while (*ptr && *ptr != ',')
	ptr ++;
      if (*ptr == ',')
	*ptr++ = '\0';

      if (!strcmp(start, "all") || !strcmp(start, "none"))
	continue;

      if (!privattrs)
	privattrs = ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "document-privacy-attributes", NULL, start);
      else
	ippSetString(PrivacyAttributes, &privattrs, ippGetCount(privattrs), start);

      if (!strcmp(start, "default"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(DocumentPrivacyArray, (void *)description[i]);
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(DocumentPrivacyArray, (void *)template[i]);
      }
      else if (!strcmp(start, "document-description"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(DocumentPrivacyArray, (void *)description[i]);
      }
      else if (!strcmp(start, "document-template"))
      {
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(DocumentPrivacyArray, (void *)template[i]);
      }
      else
      {
	cupsArrayAdd(DocumentPrivacyArray, (void *)start);
      }
    }
  }

  ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "document-privacy-scope", NULL, DocumentPrivacyScope);
}


/*
 * 'add_job_privacy()' - Add job privacy attributes.
 */

static void
add_job_privacy(void)
{
  int		i;			/* Looping var */
  char		temp[1024],		/* Temporary copy of value */
		*start,			/* Start of value */
		*ptr;			/* Pointer into value */
  ipp_attribute_t *privattrs = NULL;	/* job-privacy-attributes */
  static const char * const description[] =
  {					/* job-description attributes */
    "compression-supplied",
    "copies-actual",
    "cover-back-actual",
    "cover-front-actual",
    "current-page-order",
    "date-time-at-completed",
    "date-time-at-creation",
    "date-time-at-processing",
    "destination-statuses",
    "document-charset-supplied",
    "document-digital-signature-supplied",
    "document-format-details-supplied",
    "document-format-supplied",
    "document-message-supplied",
    "document-metadata",
    "document-name-supplied",
    "document-natural-language-supplied",
    "document-overrides-actual",
    "errors-count",
    "finishings-actual",
    "finishings-col-actual",
    "force-front-side-actual",
    "imposition-template-actual",
    "impressions-completed-current-copy",
    "insert-sheet-actual",
    "job-account-id-actual",
    "job-accounting-sheets-actual",
    "job-accounting-user-id-actual",
    "job-attribute-fidelity",
    "job-collation-type",
    "job-collation-type-actual",
    "job-copies-actual",
    "job-cover-back-actual",
    "job-cover-front-actual",
    "job-detailed-status-message",
    "job-document-access-errors",
    "job-error-sheet-actual",
    "job-finishings-actual",
    "job-finishings-col-actual",
    "job-hold-until-actual",
    "job-impressions",
    "job-impressions-col",
    "job-impressions-completed",
    "job-impressions-completed-col",
    "job-k-octets",
    "job-k-octets-processed",
    "job-mandatory-attributes",
    "job-media-sheets",
    "job-media-sheets-col",
    "job-media-sheets-completed",
    "job-media-sheets-completed-col",
    "job-message-from-operator",
    "job-more-info",
    "job-name",
    "job-originating-user-name",
    "job-originating-user-uri",
    "job-pages",
    "job-pages-col",
    "job-pages-completed",
    "job-pages-completed-col",
    "job-pages-completed-current-copy",
    "job-priority-actual",
    "job-save-printer-make-and-model",
    "job-sheet-message-actual",
    "job-sheets-actual",
    "job-sheets-col-actual",
    "job-state",
    "job-state-message",
    "job-state-reasons",
    "materials-col-actual",
    "media-actual",
    "media-col-actual",
    "media-check-input-tray-actual",
    "multiple-document-handling-actual",
    "multiple-object-handling-actual",
    "number-of-documents",
    "number-of-intervening-jobs",
    "number-up-actual",
    "orientation-requested-actual",
    "original-requesting-user-name",
    "output-bin-actual",
    "output-device-assigned",
    "overrides-actual",
    "page-delivery-actual",
    "page-order-received-actual",
    "page-ranges-actual",
    "platform-temperature-actual",
    "presentation-direction-number-up-actual",
    "print-accuracy-actual",
    "print-base-actual",
    "print-color-mode-actual",
    "print-content-optimize-actual",
    "print-objects-actual",
    "print-quality-actual",
    "print-rendering-intent-actual",
    "print-scaling-actual",
    "print-supports-actual",
    "printer-resolution-actual",
    "separator-sheets-actual",
    "sheet-collate-actual",
    "sheet-completed-copy-number",
    "sheet-completed-document-number",
    "sides-actual",
    "time-at-completed",
    "time-at-creation",
    "time-at-processing",
    "warnings-count",
    "x-image-position-actual",
    "x-image-shift-actual",
    "x-side1-image-shift-actual",
    "x-side2-image-shift-actual",
    "y-image-position-actual",
    "y-image-shift-actual",
    "y-side1-image-shift-actual",
    "y-side2-image-shift-actual"
  };
  static const char * const template[] =
  {					/* job-template attributes */
    "confirmation-sheet-print",
    "copies",
    "cover-back",
    "cover-front",
    "cover-sheet-info",
    "destination-uris",
    "feed-orientation",
    "finishings",
    "finishings-col",
    "font-name-requested",
    "font-size-requested",
    "force-front-side",
    "imposition-template",
    "insert-sheet",
    "job-account-id",
    "job-accounting-sheets"
    "job-accounting-user-id",
    "job-copies",
    "job-cover-back",
    "job-cover-front",
    "job-delay-output-until",
    "job-delay-output-until-time",
    "job-error-action",
    "job-error-sheet",
    "job-finishings",
    "job-finishings-col",
    "job-hold-until",
    "job-hold-until-time",
    "job-message-to-operator",
    "job-phone-number",
    "job-priority",
    "job-recipient-name",
    "job-save-disposition",
    "job-sheets",
    "job-sheets-col",
    "materials-col",
    "media",
    "media-col",
    "media-input-tray-check",
    "multiple-document-handling",
    "multiple-object-handling",
    "number-of-retries",
    "number-up",
    "orientation-requested",
    "output-bin",
    "output-device",
    "overrides",
    "page-delivery",
    "page-order-received",
    "page-ranges",
    "pages-per-subset",
    "pdl-init-file",
    "platform-temperature",
    "presentation-direction-number-up",
    "print-accuracy",
    "print-base",
    "print-color-mode",
    "print-content-optimize",
    "print-objects",
    "print-quality",
    "print-rendering-intent",
    "print-scaling",
    "print-supports",
    "printer-resolution",
    "proof-print",
    "retry-interval",
    "retry-timeout",
    "separator-sheets",
    "sheet-collate",
    "sides",
    "x-image-position",
    "x-image-shift",
    "x-side1-image-shift",
    "x-side2-image-shift",
    "y-image-position",
    "y-image-shift",
    "y-side1-image-shift",
    "y-side2-image-shift",
  };


  if (!strcmp(JobPrivacyAttributes, "none"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-privacy-attributes", NULL, "none");
  }
  else if (!strcmp(JobPrivacyAttributes, "all"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-privacy-attributes", NULL, "all");

    JobPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
    for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
      cupsArrayAdd(JobPrivacyArray, (void *)description[i]);
    for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
      cupsArrayAdd(JobPrivacyArray, (void *)template[i]);
  }
  else
  {
    JobPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    strlcpy(temp, JobPrivacyAttributes, sizeof(temp));

    ptr = temp;
    while (*ptr)
    {
      start = ptr;
      while (*ptr && *ptr != ',')
	ptr ++;
      if (*ptr == ',')
	*ptr++ = '\0';

      if (!strcmp(start, "all") || !strcmp(start, "none"))
	continue;

      if (!privattrs)
	privattrs = ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "job-privacy-attributes", NULL, start);
      else
	ippSetString(PrivacyAttributes, &privattrs, ippGetCount(privattrs), start);

      if (!strcmp(start, "default"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(JobPrivacyArray, (void *)description[i]);
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(JobPrivacyArray, (void *)template[i]);
      }
      else if (!strcmp(start, "job-description"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(JobPrivacyArray, (void *)description[i]);
      }
      else if (!strcmp(start, "job-template"))
      {
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(JobPrivacyArray, (void *)template[i]);
      }
      else
      {
	cupsArrayAdd(JobPrivacyArray, (void *)start);
      }
    }
  }

  ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-privacy-scope", NULL, JobPrivacyScope);
}


/*
 * 'add_subscription_privacy()' - Add subscription privacy attributes.
 */

static void
add_subscription_privacy(void)
{
  int		i;			/* Looping var */
  char		temp[1024],		/* Temporary copy of value */
		*start,			/* Start of value */
		*ptr;			/* Pointer into value */
  ipp_attribute_t *privattrs = NULL;	/* job-privacy-attributes */
  static const char * const description[] =
  {					/* subscription-description attributes */
    "notify-lease-expiration-time",
    "notify-sequence-number",
    "notify-subscriber-user-name"
  };
  static const char * const template[] =
  {					/* subscription-template attributes */
    "notify-attributes",
    "notify-charset",
    "notify-events",
    "notify-lease-duration",
    "notify-natural-language",
    "notify-pull-method",
    "notify-recipient-uri",
    "notify-time-interval",
    "notify-user-data"
  };


  if (!strcmp(SubscriptionPrivacyAttributes, "none"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "subscription-privacy-attributes", NULL, "none");
  }
  else if (!strcmp(SubscriptionPrivacyAttributes, "all"))
  {
    ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "subscription-privacy-attributes", NULL, "all");

    SubscriptionPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
    for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
      cupsArrayAdd(SubscriptionPrivacyArray, (void *)description[i]);
    for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
      cupsArrayAdd(SubscriptionPrivacyArray, (void *)template[i]);
  }
  else
  {
    SubscriptionPrivacyArray = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

    strlcpy(temp, SubscriptionPrivacyAttributes, sizeof(temp));

    ptr = temp;
    while (*ptr)
    {
      start = ptr;
      while (*ptr && *ptr != ',')
	ptr ++;
      if (*ptr == ',')
	*ptr++ = '\0';

      if (!strcmp(start, "all") || !strcmp(start, "none"))
	continue;

      if (!privattrs)
	privattrs = ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "subscription-privacy-attributes", NULL, start);
      else
	ippSetString(PrivacyAttributes, &privattrs, ippGetCount(privattrs), start);

      if (!strcmp(start, "default"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(SubscriptionPrivacyArray, (void *)description[i]);
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(SubscriptionPrivacyArray, (void *)template[i]);
      }
      else if (!strcmp(start, "subscription-description"))
      {
	for (i = 0; i < (int)(sizeof(description) / sizeof(description[0])); i ++)
	  cupsArrayAdd(SubscriptionPrivacyArray, (void *)description[i]);
      }
      else if (!strcmp(start, "subscription-template"))
      {
	for (i = 0; i < (int)(sizeof(template) / sizeof(template[0])); i ++)
	  cupsArrayAdd(SubscriptionPrivacyArray, (void *)template[i]);
      }
      else
      {
	cupsArrayAdd(SubscriptionPrivacyArray, (void *)start);
      }
    }
  }

  ippAddString(PrivacyAttributes, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "subscription-privacy-scope", NULL, SubscriptionPrivacyScope);
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
    "generated-natural-language-supported",
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
 * 'copy_icc()' - Copy an ICC profile.
 */

static server_icc_t *			/* O - New ICC profile */
copy_icc(server_icc_t *a)		/* I - ICC profile to copy */
{
  server_icc_t	*b;			/* New ICC profile */


  if ((b = calloc(1, sizeof(server_icc_t))) != NULL)
  {
    b->attrs    = a->attrs;
    b->resource = a->resource;
  }

  return (b);
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
    b->resource = a->resource;
  }

  return (b);
}


/*
 * 'create_system_attributes()' - Create the core system object attributes.
 */

static void
create_system_attributes(void)
{
  int			i;		/* Looping var */
  ipp_t			*col;		/* Collection value */
  const char		*setting;	/* System setting value */
  char			vcard[1024];	/* VCARD value */
  server_listener_t	*lis;		/* Current listener */
  cups_array_t		*uris;		/* Array of URIs */
  char			uri[1024];	/* URI */
  int			num_values = 0;	/* Number of values */
  ipp_t			*values[32];	/* Collection values */
#ifndef _WIN32
  int			alloc_groups,	/* Allocated groups */
			num_groups;	/* Number of groups */
  char			**groups;	/* Group names */
  struct group		*grp;		/* Current group */
#endif /* !_WIN32 */
  char			uuid[128];	/* system-uuid */
  static const char * const charset_supported[] =
  {					/* Values for charset-supported */
    "us-ascii",
    "utf-8"
  };
  static const char * const document_format_supported[] =
  {					/* Values for document-format-supported */
    "application/pdf",
    "application/postscript",
    "application/vnd.hp-pcl",
    "application/vnd.pwg-safe-gcode",
    "image/jpeg",
    "image/png",
    "image/pwg-raster",
    "image/urf",
    "model/3mf",
    "model/3mf+slice",
    "text/plain"
  };
  static const char * const ipp_features_supported[] =
  {					/* Values for ipp-features-supported */
    "document-object",
    "infrastructure-printer",
    "ipp-3d",
    "ipp-everywhere",
    "page-overrides",
    "system-service"
  };
  static const char * const ipp_versions_supported[] =
  {					/* Values for ipp-versions-supported */
    "2.0",
    "2.1",
    "2.2"
  };
  static const char * const notify_attributes_supported[] =
  {					/* Values for notify-attributes-supported */
    "printer-state-change-time",
    "notify-lease-expiration-time",
    "notify-subscriber-user-name"
  };
  static const int operations_supported[] =
  {					/* Values for operations-supported */
    IPP_OP_GET_PRINTER_ATTRIBUTES,
    IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES,
    IPP_OP_GET_SUBSCRIPTIONS,
    IPP_OP_RENEW_SUBSCRIPTION,
    IPP_OP_CANCEL_SUBSCRIPTION,
    IPP_OP_GET_NOTIFICATIONS,
    IPP_OP_ALLOCATE_PRINTER_RESOURCES,
    IPP_OP_CREATE_PRINTER,
    IPP_OP_DEALLOCATE_PRINTER_RESOURCES,
    IPP_OP_DELETE_PRINTER,
    IPP_OP_GET_PRINTERS,
    IPP_OP_SHUTDOWN_ONE_PRINTER,
    IPP_OP_STARTUP_ONE_PRINTER,
    IPP_OP_CANCEL_RESOURCE,
    IPP_OP_CREATE_RESOURCE,
    IPP_OP_INSTALL_RESOURCE,
    IPP_OP_SEND_RESOURCE_DATA,
    IPP_OP_SET_RESOURCE_ATTRIBUTES,
    IPP_OP_CREATE_RESOURCE_SUBSCRIPTIONS,
    IPP_OP_CREATE_SYSTEM_SUBSCRIPTIONS,
    IPP_OP_DISABLE_ALL_PRINTERS,
    IPP_OP_ENABLE_ALL_PRINTERS,
    IPP_OP_GET_SYSTEM_ATTRIBUTES,
    IPP_OP_GET_SYSTEM_SUPPORTED_VALUES,
    IPP_OP_PAUSE_ALL_PRINTERS,
    IPP_OP_PAUSE_ALL_PRINTERS_AFTER_CURRENT_JOB,
    IPP_OP_REGISTER_OUTPUT_DEVICE,
    IPP_OP_RESTART_SYSTEM,
    IPP_OP_RESUME_ALL_PRINTERS,
    IPP_OP_SET_SYSTEM_ATTRIBUTES,
    IPP_OP_SHUTDOWN_ALL_PRINTERS,
    IPP_OP_STARTUP_ALL_PRINTERS
  };
  static const char * const printer_creation_attributes_supported[] =
  {					/* Values for printer-creation-attributes-supported */
    "chamber-humidity-default",
    "chamber-humidity-supported",
    "chamber-temperature-default",
    "chamber-temperature-su[pported",
    "coating-sides-supported",
    "coating-type-supported",
    "color-supported",
    "copies-default",
    "copies-supported",
    "cover-back-default",
    "cover-back-supported",
    "cover-front-default",
    "cover-front-supported",
    "covering-name-supported",
    "document-creation-attributes-supported",
    "document-format-default",
    "document-format-supported",
    "finishing-template-supported",
    "finishings-default",
    "finishings-ready",
    "finishings-supported",
    "finishings-col-database",
    "finishings-col-default",
    "finishings-col-ready",
    "finishings-col-supported",
    "folding-direction-supported",
    "folding-offset-supported",
    "folding-reference-edge-supported",
    "imposition-template-default",
    "imposition-template-supported",
    "insert-sheet-default",
    "inseet-sheet-supported",
    "job-account-id-default",
    "job-account-id-supported",
    "job-account-type-default",
    "job-account-type-supported",
    "job-accounting-sheets-default",
    "job-accounting-sheets-supported",
    "job-accounting-user-id-default",
    "job-accounting-user-id-supported",
    "job-authorization-uri-supported",
    "job-constraints-supported",
    "job-creation-attributes-supported",
    "job-delay-output-until-default",
    "job-error-action-default",
    "job-error-action-supported",
    "job-error-sheet-default",
    "job-error-sheet-supported",
    "job-hold-until-default",
    "job-message-to-operator-default",
    "job-pages-per-set-supported",
    "job-password-encryption-supported",
    "job-password-length-supported",
    "job-password-repertoire-configured",
    "job-password-repertoire-supported",
    "job-password-supported",
    "job-phone-number-default",
    "job-phone-number-supported",
    "job-presets-supported",
    "job-priority-default",
    "job-recipient-name-default",
    "job-recipient-name-supported",
    "job-resolvers-supported",
    "job-retain-until-default",
    "job-sheet-message-default",
    "job-sheet-message-supported",
    "job-sheets-col-default",
    "job-sheets-col-supported",
    "job-sheets-default",
    "job-sheets-supported",
    "job-triggers-supported",
    "laminating-sides-supported",
    "laminating-type-supported",
    "material-amount-units-supported",
    "material-diameter-supported",
    "material-nozzle-diameter-supported",
    "material-purpose-supported",
    "material-rate-supported",
    "material-rate-units-supported",
    "material-shell-thickness-supported",
    "material-temperature-supported",
    "material-type-supported",
    "materials-col-database",
    "materials-col-default",
    "materials-col-ready",
    "materials-col-supported",
    "max-materials-col-supported",
    "max-stitching-locations-supported",
    "media-bottom-margin-supported",
    "media-col-database",
    "media-col-default",
    "media-col-ready",
    "media-color-supported",
    "media-default",
    "media-key-supported",
    "media-ready",
    "media-supported",
    "media-left-margin-supported",
    "media-right-margin-supported",
    "media-size-supported",
    "media-source-supported",
    "media-top-margin-supported",
    "media-type-supported",
    "multiple-document-handling-default",
    "multiple-document-jobs-supported",
    "multiple-object-handling-default",
    "multiple-operation-time-out-action",
    "natural-languauge-configured",
    "notify-events-default",
    "number-up-default",
    "number-up-supported",
    "orientation-requested-default",
    "orientation-requested-supported",
    "output-bin-default",
    "output-bin-supported",
    "overrides-supported",
    "page-delivery-default",
    "page-delivery-supported",
    "page-ranges-supported",
    "pages-per-minute",
    "pages-per-minute-color",
    "pdl-override-supported",
    "platform-shape",
    "platform-temperature-default",
    "platform-temperature-supported",
    "presentation-direction-number-up-default",
    "presentation-direction-number-up-supported",
    "print-accuracy-default",
    "print-accuracy-supported",
    "print-base-default",
    "print-base-supported",
    "print-color-mode-default",
    "print-color-mode-supported",
    "print-content-optimize-default",
    "print-content-optimize-supported",
    "print-objects-default",
    "print-quality-default",
    "print-rendering-intent-default",
    "print-rendering-intent-supported",
    "print-scaling-default",
    "print-scaling-supported",
    "print-supports-default",
    "print-supports-supported",
    "printer-charge-info",
    "printer-charge-info-uri",
    "printer-contact-col",
    "printer-device-id",
    "printer-dns-sd-name",
    "printer-geo-location",
    "printer-icc-profiles",
    "printer-info",
    "printer-kind",
    "printer-location",
    "printer-make-and-model",
    "printer-mandatory-job-attributes",
    "printer-name",
    "printer-organization",
    "printer-organizational-unit",
    "printer-resolution-default",
    "printer-resolution-supported",
    "printer-volume-supported",
    "proof-print-default",
    "proof-print-suppported",
    "punching-hole-diameter-configured",
    "punching-locations-supported",
    "punching-offset-supported",
    "punching-reference-edge-supported",
    "pwg-raster-document-resolution-supported",
    "pwg-raster-document-sheet-back",
    "pwg-raster-document-type-supported",
    "pwg-safe-gcode-supported",
    "separator-sheets-default",
    "separator-sheets-supported",
    "sides-default",
    "sides-supported",
    "smi2699-auth-print-group",
    "smi2699-auth-proxy-group",
    "smi2699-device-command",
    "smi2699-device-format",
    "smi2699-device-name",
    "smi2699-device-uri",
    "smi2699-max-output-device",
    "stitching-angle-supported",
    "stitching-locations-supported",
    "stitching-method-supported",
    "stitching-offset-supported",
    "stitching-reference-edge-supported",
    "trimming-offset-supported",
    "trimming-reference-edge-supported",
    "trimming-type-supported",
    "trimming-when-supported",
    "urf-supported",
    "x-image-position-default",
    "x-image-position-supported",
    "x-image-shift-default",
    "x-image-shift-supported",
    "x-side1-image-shift-default",
    "x-side1-image-shift-supported",
    "x-side2-image-shift-default",
    "x-side2-image-shift-supported",
    "y-image-position-default",
    "y-image-position-supported",
    "y-image-shift-default",
    "y-image-shift-supported",
    "y-side1-image-shift-default",
    "y-side1-image-shift-supported",
    "y-side2-image-shift-default",
    "y-side2-image-shift-supported"
  };
  static const char * const resource_format_supported[] =
  {					/* Values for resource-format-supported */
    "application/ipp",
    "application/vnd.iccprofile",
    "image/png",
    "text/strings"
  };
  static const char * const resource_settable_attributes_supported[] =
  {					/* Values for resource-settable-attributes-supported */
    "resource-name"
  };
  static const char * const resource_type_supported[] =
  {					/* Values for resource-type-supported */
    "static-icc-profile",
    "static-image",
    "static-strings",
    "template-document",
    "template-job",
    "template-printer"
  };
  static const char * const smi2699_device_command_supported[] =
  {					/* Values for smi2699-device-command-supported */
    /* TODO: Scan BinDir for commands? Or make this configurable? */
    "ippdoclint",
    "ipptransform",
    "ipptransform3d"
  };
  static const char * const smi2699_device_format_supported[] =
  {					/* Values for smi2699-device-format-supported */
    "application/pdf",
    "application/postscript",
    "application/vnd.hp-pcl",
    "application/vnd.pwg-safe-gcode",
    "image/pwg-raster",
    "image/urf",
    "model/3mf",
    "model/3mf+slice",
    "text/plain"
  };
  static const char * const smi2699_device_uri_schemes_supported[] =
  {					/* Values for smi2699-device-uri-schemes-supported */
    "ipp",
    "ipps",
    "socket",
    "usbserial"
  };
  static const char * const system_mandatory_printer_attributes[] =
  {					/* Values for system-mandatory-printer-attributes */
    "printer-name"
  };
  static const char * const system_settable_attributes_supported[] =
  {					/* Values for system-settable-attributes-supported */
    "system-default-printer-id",
    "system-geo-location",
    "system-info",
    "system-location",
    "system-make-and-model",
    "system-name",
    "system-owner-col"
  };


  SystemAttributes = ippNew();

  /* charset-configured */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");

  /* charset-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-supported", (int)(sizeof(charset_supported) / sizeof(charset_supported[0])), NULL, charset_supported);

  /* document-format-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "document-format-supported", (int)(sizeof(document_format_supported) / sizeof(document_format_supported[0])), NULL, document_format_supported);

  /* generated-natural-language-supported */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "generated-natural-language-supported", NULL, "en");

  /* ipp-features-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", (int)(sizeof(ipp_features_supported) / sizeof(ipp_features_supported[0])), NULL, ipp_features_supported);

  /* ipp-versions-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", (int)(sizeof(ipp_versions_supported) / sizeof(ipp_versions_supported[0])), NULL, ipp_versions_supported);

  /* ippget-event-life */
  ippAddInteger(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "ippget-event-life", SERVER_IPPGET_EVENT_LIFE);

  /* natural-language-configured */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "natural-language-configured", NULL, "en");

  /* notify-attributes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-attributes-supported", sizeof(notify_attributes_supported) / sizeof(notify_attributes_supported[0]), NULL, notify_attributes_supported);

  /* notify-events-default */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-default", NULL, "job-completed");

  /* notify-events-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-supported", sizeof(server_events) / sizeof(server_events[0]), NULL, server_events);

  /* notify-lease-duration-default */
  ippAddInteger(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "notify-lease-duration-default", SERVER_NOTIFY_LEASE_DURATION_DEFAULT);

  /* notify-lease-duration-supported */
  ippAddRange(SystemAttributes, IPP_TAG_SYSTEM, "notify-lease-duration-supported", 0, SERVER_NOTIFY_LEASE_DURATION_MAX);

  /* notify-max-events-supported */
  ippAddInteger(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "notify-max-events-supported", (int)(sizeof(server_events) / sizeof(server_events[0])));

  /* notify-pull-method-supported */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-pull-method-supported", NULL, "ippget");

  /* operations-supported */
  ippAddIntegers(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_ENUM, "operations-supported", sizeof(operations_supported) / sizeof(operations_supported[0]), operations_supported);

  /* printer-creation-attributes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-creation-attributes-supported", sizeof(printer_creation_attributes_supported) / sizeof(printer_creation_attributes_supported[0]), NULL, printer_creation_attributes_supported);

  /* resource-format-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "resource-format-supported", (int)(sizeof(resource_format_supported) / sizeof(resource_format_supported[0])), NULL, resource_format_supported);

  /* resource-settable-attributes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "resource-settable-attributes-supported", (int)(sizeof(resource_settable_attributes_supported) / sizeof(resource_settable_attributes_supported[0])), NULL, resource_settable_attributes_supported);

  /* resource-type-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "resource-type-supported", (int)(sizeof(resource_type_supported) / sizeof(resource_type_supported[0])), NULL, resource_type_supported);

  /* smi2699-auth-group-supported */
#ifndef _WIN32
  alloc_groups = 10;
  num_groups   = 0;
  groups       = calloc(10, sizeof(char *));

  if ((setting = cupsGetOption("AuthGroups", SystemNumSettings, SystemSettings)) != NULL)
  {
    char	*tempgroups = strdup(setting),
					/* Temporary string to hold group names */
		*tempptr = tempgroups,	/* Pointer into group names */
		*tempgroup;		/* Current group name */

    while ((tempgroup = strsep(&tempptr, " \t")) != NULL)
    {
      if (num_groups >= alloc_groups)
      {
	alloc_groups += 10;
	groups       = (char **)realloc(groups, (size_t)alloc_groups * sizeof(char *));
      }

      groups[num_groups ++] = strdup(tempgroup);
    }

    free(tempgroups);
  }
  else if (getuid())
  {
   /*
    * Default is the current user's groups (if not root).
    */

    struct passwd	*pw;		/* User account information */
    int			ngids;		/* Number of groups for user */
#  ifdef __APPLE__
    int			gids[2048];	/* Group list */
#  else
    gid_t		gids[2048];	/* Group list */
#  endif /* __APPLE__ */

    if ((pw = getpwuid(getuid())) != NULL)
    {
      ngids = (int)(sizeof(gids) / sizeof(gids[0]));

#  ifdef __APPLE__
      if (!getgrouplist(pw->pw_name, (int)pw->pw_gid, gids, &ngids))
#  else
      if (!getgrouplist(pw->pw_name, pw->pw_gid, gids, &ngids))
#  endif /* __APPLE__ */
      {
        for (i = 0; i < ngids; i ++)
        {
          if ((grp = getgrgid((gid_t)gids[i])) != NULL)
          {
	    if (grp->gr_name[0] == '_' && strncmp(grp->gr_name, "_lp", 3))
	      continue;				/* Skip system groups */

	    if (num_groups >= alloc_groups)
	    {
	      alloc_groups += 10;
	      groups       = (char **)realloc(groups, (size_t)alloc_groups * sizeof(char *));
	    }

	    groups[num_groups ++] = strdup(grp->gr_name);
          }
        }
      }
    }
  }

  if (num_groups == 0)
  {
   /*
    * If all else fails, use default list of groups...
    */

    static const char * const defgroups[] =
    {
      "adm",
      "admin",
      "daemon",
      "operator",
      "staff",
      "wheel"
    };

    for (i = 0; i < (int)(sizeof(defgroups) / sizeof(defgroups[0])); i ++)
    {
      if (getgrnam(defgroups[i]))
        groups[num_groups ++] = strdup(defgroups[i]);
    }
  }

  if (num_groups > 0)
  {
    ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_NAME, "smi2699-auth-group-supported", num_groups, NULL, (const char **)groups);

    for (i = 0; i < num_groups; i ++)
      free(groups[i]);
    free(groups);
  }
#endif /* !_WIN32 */

  /* smi2699-device-command-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_NAME), "smi2699-device-command-supported", (int)(sizeof(smi2699_device_command_supported) / sizeof(smi2699_device_command_supported[0])), NULL, smi2699_device_command_supported);

  /* smi2699-device-format-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "smi2699-device-format-supported", (int)(sizeof(smi2699_device_format_supported) / sizeof(smi2699_device_format_supported[0])), NULL, smi2699_device_format_supported);

  /* smi2699-device-uri-schemes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_URISCHEME), "smi2699-device-uri-schemes-supported", (int)(sizeof(smi2699_device_uri_schemes_supported) / sizeof(smi2699_device_uri_schemes_supported[0])), NULL, smi2699_device_uri_schemes_supported);

  /* system-device-id, TODO: maybe remove this, it has no purpose */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_TEXT), "system-device-id", NULL, "MANU:None;MODEL:None;");

  /* system-geo-location */
  setting = cupsGetOption("GeoLocation", SystemNumSettings, SystemSettings);
  if (setting)
    ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_URI, "system-geo-location", NULL, setting);
  else
    ippAddOutOfBand(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_UNKNOWN, "system-geo-location");

  /* system-info */
  setting = cupsGetOption("Info", SystemNumSettings, SystemSettings);
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_TEXT), "system-info", NULL, setting ? setting : "ippserver system service");

  /* system-location */
  setting = cupsGetOption("Location", SystemNumSettings, SystemSettings);
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_TEXT), "system-location", NULL, setting ? setting : "nowhere");

  /* system-mandatory-printer-attributes */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "system-mandatory-printer-attributes", sizeof(system_mandatory_printer_attributes) / sizeof(system_mandatory_printer_attributes[0]), NULL, system_mandatory_printer_attributes);

  /* system-make-and-model */
  setting = cupsGetOption("MakeAndModel", SystemNumSettings, SystemSettings);
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_TEXT), "system-make-and-model", NULL, setting ? setting : "ippserver prototype");

  /* system-name */
  setting = cupsGetOption("Name", SystemNumSettings, SystemSettings);
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_NAME), "system-name", NULL, setting ? setting : "ippserver");

  /* system-owner-col */
  col = ippNew();

  setting = cupsGetOption("OwnerEmail", SystemNumSettings, SystemSettings);
  httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "mailto", NULL, NULL, 0, setting ? setting : "unknown@example.com");
  ippAddString(col, IPP_TAG_ZERO, IPP_TAG_URI, "owner-uri", NULL, uri);

  setting = cupsGetOption("OwnerName", SystemNumSettings, SystemSettings);
  ippAddString(col, IPP_TAG_ZERO, IPP_TAG_NAME, "owner-name", NULL, setting ? setting : cupsUser());

  serverMakeVCARD(NULL, cupsGetOption("OwnerName", SystemNumSettings, SystemSettings), cupsGetOption("OwnerLocation", SystemNumSettings, SystemSettings), cupsGetOption("OwnerEmail", SystemNumSettings, SystemSettings), cupsGetOption("OwnerPhone", SystemNumSettings, SystemSettings), vcard, sizeof(vcard));
  ippAddString(col, IPP_TAG_ZERO, IPP_TAG_TEXT, "owner-vcard", NULL, vcard);

  ippAddCollection(SystemAttributes, IPP_TAG_SYSTEM, "system-owner-col", col);
  ippDelete(col);

  /* system-settable-attributes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "system-settable-attributes-supported", (int)(sizeof(system_settable_attributes_supported) / sizeof(system_settable_attributes_supported[0])), NULL, system_settable_attributes_supported);

#if 0
  /* TODO: Support system-strings-languages-supported */
  if (SystemStrings)
  {
    server_lang_t *lang;

    for (attr = NULL, lang = (server_lang_t *)cupsArrayFirst(SystemStrings); lang; lang = (server_lang_t *)cupsArrayNext(SystemStrings))
    {
      if (attr)
        ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), lang->lang);
      else
        attr = ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_LANGUAGE, "system-strings-languages-supported", NULL, lang->lang);
    }
  }
#endif /* 0 */

  /* system-uuid */
  if ((setting = cupsGetOption("UUID", SystemNumSettings, SystemSettings)) == NULL)
  {
    lis = cupsArrayFirst(Listeners);
    httpAssembleUUID(lis->host, lis->port, "", 0, uuid, sizeof(uuid));
    setting = uuid;
  }
  else if (strncmp(setting, "urn:uuid:", 9))
  {
    snprintf(uuid, sizeof(uuid), "urn:uuid:%s", setting);
    setting = uuid;
  }

  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_URI, "system-uuid", NULL, setting);

  /* system-xri-supported */
  uris = cupsArrayNew3((cups_array_func_t)strcmp, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);
  for (lis = cupsArrayFirst(Listeners); lis && num_values < (int)(sizeof(values) / sizeof(values[0])); lis = cupsArrayNext(Listeners))
  {
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), SERVER_IPP_SCHEME, NULL, lis->host, lis->port, "/ipp/system");

    if (!DefaultSystemURI)
      DefaultSystemURI = strdup(uri);

    if (!cupsArrayFind(uris, uri))
    {
      cupsArrayAdd(uris, uri);

      col = ippNew();

      ippAddString(col, IPP_TAG_ZERO, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-authentication", NULL, Authentication ? "basic"  : "none");

#ifdef HAVE_SSL
      if (Encryption != HTTP_ENCRYPTION_NEVER)
        ippAddString(col, IPP_TAG_ZERO, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-security", NULL, "tls");
      else
#endif /* HAVE_SSL */
        ippAddString(col, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "xri-security", NULL, "none");

      ippAddString(col, IPP_TAG_ZERO, IPP_TAG_URI, "xri-uri", NULL, uri);

      values[num_values ++] = col;
    }
  }

  if (num_values > 0)
  {
    ippAddCollections(SystemAttributes, IPP_TAG_SYSTEM, "system-xri-supported", num_values, (const ipp_t **)values);

    for (i = 0; i < num_values; i ++)
      ippDelete(values[i]);
  }

  cupsArrayDelete(uris);

  /* xri-authentication-supported */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-authentication-supported", NULL, Authentication ? "basic" : "none");

  /* xri-security-supported */
#ifdef HAVE_SSL
  if (Encryption != HTTP_ENCRYPTION_NEVER)
    ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-security-supported", NULL, "tls");
  else
#endif /* HAVE_SSL */
    ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-security-supported", NULL, "none");

  /* xri-uri-scheme-supported */
#ifdef HAVE_SSL
  if (Encryption != HTTP_ENCRYPTION_NEVER)
    ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_URISCHEME), "xri-uri-scheme-supported", NULL, "ipps");
  else
#endif /* HAVE_SSL */
    ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_URISCHEME), "xri-uri-scheme-supported", NULL, "ipp");
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
 * 'dnssd_init()' - Initialize DNS-SD registrations.
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
 * 'finalize_system()' - Finalize values for the System object.
 */

static int				/* O - 1 on success, 0 on failure */
finalize_system(void)
{
  char		local[1024];		/* Local hostname */


 /*
  * Default BinDir...
  */

  if (!BinDir)
  {
    const char	*env;			/* Environment variable */
    char	temp[1024];		/* Temporary string */

    if ((env = getenv("CUPS_SERVERBIN")) != NULL)
    {
     /*
      * Look for the commands in the indicated directory...
      */

      snprintf(temp, sizeof(temp), "%s/command", env);
      BinDir = strdup(temp);
    }
    else if ((env = getenv("SNAP")) != NULL)
    {
     /*
      * Look for the commands in the snap directory...
      */

      snprintf(temp, sizeof(temp), "%s/lib/cups/command", env);
      BinDir = strdup(temp);
    }
    else
    {
     /*
      * Use the compiled-in defaults...
      */

      BinDir = strdup(CUPS_SERVERBIN "/command");
    }
  }

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

#ifdef _WIN32
    if ((tmpdir = getenv("TEMP")) == NULL)
      tmpdir = "C:/TEMP";
#elif defined(__APPLE__)
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/private/tmp";
#else
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/tmp";
#endif /* _WIN32 */

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
  * Authentication/authorization support...
  */

  if (Authentication)
  {
#ifndef _WIN32
    if (AuthAdminGroup == SERVER_GROUP_NONE)
      AuthAdminGroup = getgid();
    if (AuthOperatorGroup == SERVER_GROUP_NONE)
      AuthOperatorGroup = getgid();
    if (AuthProxyGroup == SERVER_GROUP_NONE)
      AuthProxyGroup = getgid();
#endif /* !_WIN32 */

    if (!AuthName)
      AuthName = strdup("Printing");
#ifdef DEFAULT_PAM_SERVICE
    if (!AuthService && !AuthTestPassword)
      AuthService = strdup(DEFAULT_PAM_SERVICE);
#endif /* DEFAULT_PAM_SERVICE */
    if (!AuthType)
      AuthType = strdup("Basic");

    if (!DocumentPrivacyScope)
      DocumentPrivacyScope = strdup(SERVER_SCOPE_DEFAULT);
    if (!DocumentPrivacyAttributes)
      DocumentPrivacyAttributes = strdup("default");

    if (!JobPrivacyScope)
      JobPrivacyScope = strdup(SERVER_SCOPE_DEFAULT);
    if (!JobPrivacyAttributes)
      JobPrivacyAttributes = strdup("default");

    if (!SubscriptionPrivacyScope)
      SubscriptionPrivacyScope = strdup(SERVER_SCOPE_DEFAULT);
    if (!SubscriptionPrivacyAttributes)
      SubscriptionPrivacyAttributes = strdup("default");
  }
  else
  {
    if (!DocumentPrivacyScope)
      DocumentPrivacyScope = strdup(SERVER_SCOPE_ALL);
    if (!DocumentPrivacyAttributes)
      DocumentPrivacyAttributes = strdup("none");

    if (!JobPrivacyScope)
      JobPrivacyScope = strdup(SERVER_SCOPE_ALL);
    if (!JobPrivacyAttributes)
      JobPrivacyAttributes = strdup("none");

    if (!SubscriptionPrivacyScope)
      SubscriptionPrivacyScope = strdup(SERVER_SCOPE_ALL);
    if (!SubscriptionPrivacyAttributes)
      SubscriptionPrivacyAttributes = strdup("none");
  }

  PrivacyAttributes = ippNew();

  add_document_privacy();
  add_job_privacy();
  add_subscription_privacy();

 /*
  * Initialize DNS-SD...
  */

  if (DNSSDEnabled)
    dnssd_init();

 /*
  * Apply default listeners if none are specified...
  */

  if (!Listeners)
  {
    int	port = DefaultPort;		/* Current port */

#ifdef _WIN32
   /*
    * Windows is almost always used as a single user system, so use a default port
    * number of 8631.
    */

    if (!port)
      port = 8631;

#else
   /*
    * Use 8000 + UID mod 1000 for the default port number...
    */

    if (!port)
      port = 8000 + ((int)getuid() % 1000);
#endif /* _WIN32 */

    while (!serverCreateListeners(strcmp(ServerName, "localhost") ? NULL : "localhost", port))
    {
      if (DefaultPort)
        return (0);

      port ++;
    }

    DefaultPort = port;

    serverLog(SERVER_LOGLEVEL_INFO, "Using default listeners for %s:%d.", ServerName, DefaultPort);
  }

  create_system_attributes();

  return (1);
}


/*
 * 'free_icc()' - Free a profile.
 */

static void
free_icc(server_icc_t *a)		/* I - Profile */
{
  ippDelete(a->attrs);
  free(a);
}


/*
 * 'free_lang()' - Free a localization.
 */

static void
free_lang(server_lang_t *a)		/* I - Localization */
{
  free(a->lang);
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
		*value,			/* Pointer to value on line */
		temp[1024];		/* Temporary string */
  const char	*setting;		/* Current setting */
#ifndef _WIN32
  struct group	*group;			/* Group information */
#endif /* !_WIN32 */
  int		i;			/* Looping var */
  static const char * const settings[] =/* List of directives */
  {
    "Authentication",
    "AuthAdminGroup",
    "AuthGroups",
    "AuthName",
    "AuthOperatorGroup",
    "AuthProxyGroup",
    "AuthService",
    "AuthTestPassword",
    "AuthType",
    "BinDir",
    "DataDir",
    "DefaultPrinter",
    "DocumentPrivacyAttributes",
    "DocumentPrivacyScope",
    "Encryption",
    "FileDirectory",
    "GeoLocation",
    "Info",
    "JobPrivacyAttributes",
    "JobPrivacyScope",
    "KeepFiles",
    "Listen",
    "Location",
    "LogFile",
    "LogLevel",
    "MakeAndModel",
    "MaxCompletedJobs",
    "MaxJobs",
    "Name",
    "OwnerEmail",
    "OwnerLocation",
    "OwnerName",
    "OwnerPhone",
    "SpoolDir",
    "StateDir",
    "SubscriptionPrivacyAttributes",
    "SubscriptionPrivacyScope",
    "UUID"
  };


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

    for (i = 0; i < (int)(sizeof(settings) / sizeof(settings[0])); i ++)
      if (!_cups_strcasecmp(line, settings[i]))
        break;

    if (i >= (int)(sizeof(settings) / sizeof(settings[0])))
    {
      fprintf(stderr, "ippserver: Unknown \"%s\" directive on line %d.\n", line, linenum);
      continue;
    }

    if ((setting = cupsGetOption(line, SystemNumSettings, SystemSettings)) != NULL)
    {
     /*
      * Already have this setting, check whether this is OK...
      */

      if (!_cups_strcasecmp(line, "FileDirectory") || !_cups_strcasecmp(line, "Listen"))
      {
       /*
        * Listen allows multiple values, others do not...
        */

	snprintf(temp, sizeof(temp), "%s %s", setting, value);
	SystemNumSettings = cupsAddOption(line, temp, SystemNumSettings, &SystemSettings);
      }
      else
      {
	fprintf(stderr, "ippserver: Duplicate \"%s\" directive on line %d.\n", line, linenum);
	continue;
      }
    }
    else
    {
     /*
      * First time we've seen this setting...
      */

      SystemNumSettings = cupsAddOption(line, value, SystemNumSettings, &SystemSettings);
    }

    if (!_cups_strcasecmp(line, "Authentication"))
    {
      if (!_cups_strcasecmp(value, "on") || !_cups_strcasecmp(value, "yes"))
      {
        Authentication = 1;
      }
      else if (!_cups_strcasecmp(value, "off") || !_cups_strcasecmp(value, "no"))
      {
        Authentication = 0;
      }
      else
      {
        fprintf(stderr, "ippserver: Unknown Authentication \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }
    }
#ifndef _WIN32
    else if (!_cups_strcasecmp(line, "AuthAdminGroup"))
    {
      if ((group = getgrnam(value)) == NULL)
      {
        fprintf(stderr, "ippserver: Unable to find AuthAdminGroup \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      AuthAdminGroup = group->gr_gid;
    }
#endif /* !_WIN32 */
    else if (!_cups_strcasecmp(line, "AuthName"))
    {
      AuthName = strdup(value);
    }
#ifndef _WIN32
    else if (!_cups_strcasecmp(line, "AuthOperatorGroup"))
    {
      if ((group = getgrnam(value)) == NULL)
      {
        fprintf(stderr, "ippserver: Unable to find AuthOperatorGroup \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      AuthOperatorGroup = group->gr_gid;
    }
    else if (!_cups_strcasecmp(line, "AuthProxyGroup"))
    {
      if ((group = getgrnam(value)) == NULL)
      {
        fprintf(stderr, "ippserver: Unable to find AuthProxyGroup \"%s\" on line %d of \"%s\".\n", value, linenum, conf);
        status = 0;
        break;
      }

      AuthProxyGroup = group->gr_gid;
    }
#endif /* !_WIN32 */
    else if (!_cups_strcasecmp(line, "AuthService"))
    {
      AuthService = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "AuthTestPassword"))
    {
      AuthTestPassword = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "AuthType"))
    {
      AuthType = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "BinDir"))
    {
      if (access(value, X_OK))
      {
        fprintf(stderr, "ippserver: Unable to access BinDir \"%s\": %s\n", value, strerror(errno));
        status = 0;
        break;
      }

      BinDir = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "DataDir"))
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
      if (default_printer)
      {
        fprintf(stderr, "ippserver: Extra DefaultPrinter seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      default_printer = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "DocumentPrivacyAttributes"))
    {
      if (DocumentPrivacyAttributes)
      {
        fprintf(stderr, "ippserver: Extra DocumentPrivacyAttributes seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      DocumentPrivacyAttributes = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "DocumentPrivacyScope"))
    {
      if (DocumentPrivacyScope)
      {
        fprintf(stderr, "ippserver: Extra DocumentPrivacyScope seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      DocumentPrivacyScope = strdup(value);
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
    else if (!_cups_strcasecmp(line, "FileDirectory"))
    {
      char		*dir,		/* Directory value */
			dirabs[256];	/* Absolute directory path */
      struct stat	dirinfo;	/* Directory information */

      while (*value)
      {
        while (isspace(*value & 255))
          value ++;

        if (*value == '\'' || *value == '\"')
        {
          char	quote = *value++;	/* Quote to look for */

          dir = value;
          while (*value && *value != quote)
            value ++;

          if (*value == quote)
          {
            *value++ = '\0';
	  }
	  else
	  {
	    fprintf(stderr, "ippserver: Missing closing quote for FileDirectory on line %d of \"%s\".\n", linenum, conf);
	    status = 0;
	    break;
	  }
	}
	else
	{
          dir = value;
          while (*value && !isspace(*value & 255))
            value ++;

          if (*value)
            *value++ = '\0';
	}

        if (!FileDirectories)
          FileDirectories = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)strdup, (cups_afree_func_t)free);

#ifndef _WIN32 /* TODO: Update this for Windows */
        if (dir[0] != '/')
          dir = realpath(dir, dirabs);
#endif /* !_WIN32 */

        if (!dir || access(dir, X_OK) || stat(dir, &dirinfo) || !S_ISDIR(dirinfo.st_mode))
        {
	  fprintf(stderr, "ippserver: Bad FileDirectory on line %d of \"%s\".\n", linenum, conf);
	  status = 0;
	  break;
        }

        cupsArrayAdd(FileDirectories, dir);
      }
    }
    else if (!_cups_strcasecmp(line, "JobPrivacyAttributes"))
    {
      if (JobPrivacyAttributes)
      {
        fprintf(stderr, "ippserver: Extra JobPrivacyAttributes seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      JobPrivacyAttributes = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "JobPrivacyScope"))
    {
      if (JobPrivacyScope)
      {
        fprintf(stderr, "ippserver: Extra JobPrivacyScope seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      JobPrivacyScope = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "KeepFiles"))
    {
      KeepFiles = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcasecmp(value, "on");
    }
    else if (!_cups_strcasecmp(line, "Listen"))
    {
      char	*host,			/* Host value */
		*ptr;			/* Pointer into host value */
      int	port;			/* Port number */

#ifdef _WIN32
      char *curvalue = value;		/* Current value */

      while ((host = strtok(curvalue, " \t")) != NULL)
#else
      while ((host = strsep(&value, " \t")) != NULL)
#endif /* _WIN32 */
      {
#ifdef _WIN32
	curvalue = NULL;
#endif /* _WIN32 */

	if ((ptr = strrchr(host, ':')) != NULL && !isdigit(ptr[1] & 255))
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
	else if (DefaultPort)
	{
	  port = DefaultPort;
	}
	else
	{
#ifdef _WIN32
          port = 8631;
#else
	  port = 8000 + ((int)getuid() % 1000);
#endif /* _WIN32 */
	}

        if (!DefaultPort)
          DefaultPort = port;

	if (!serverCreateListeners(host, port))
	{
	  status = 0;
	  break;
	}
      }

      if (!status)
        break;
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
    else if (!_cups_strcasecmp(line, "SpoolDir"))
    {
      if (access(value, R_OK))
      {
        fprintf(stderr, "ippserver: Unable to access SpoolDirectory \"%s\": %s\n", value, strerror(errno));
        status = 0;
        break;
      }

      SpoolDirectory = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "StateDir"))
    {
      if (access(value, R_OK) && mkdir(value, 0700))
      {
        fprintf(stderr, "ippserver: Unable to access StateDirectory \"%s\": %s\n", value, strerror(errno));
        status = 0;
        break;
      }

      StateDirectory = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "SubscriptionPrivacyAttributes"))
    {
      if (SubscriptionPrivacyAttributes)
      {
        fprintf(stderr, "ippserver: Extra SubscriptionPrivacyAttributes seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      SubscriptionPrivacyAttributes = strdup(value);
    }
    else if (!_cups_strcasecmp(line, "SubscriptionPrivacyScope"))
    {
      if (SubscriptionPrivacyScope)
      {
        fprintf(stderr, "ippserver: Extra SubscriptionPrivacyScope seen on line %d of \"%s\".\n", linenum, conf);
        status = 0;
        break;
      }

      SubscriptionPrivacyScope = strdup(value);
    }
  }

  cupsFileClose(fp);

  return (status);
}


/*
 * 'iso_date()' - Return an ISO 8601 date/time string for the given IPP dateTime
 *                value.
 */

static char *				/* O - ISO 8601 date/time string */
iso_date(const ipp_uchar_t *date)	/* I - IPP (RFC 1903) date/time value */
{
  time_t	utctime;		/* UTC time since 1970 */
  struct tm	*utcdate;		/* UTC date/time */
  static char	buffer[255];		/* String buffer */


  utctime = ippDateToTime(date);
  utcdate = gmtime(&utctime);

  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
	   utcdate->tm_year + 1900, utcdate->tm_mon + 1, utcdate->tm_mday,
	   utcdate->tm_hour, utcdate->tm_min, utcdate->tm_sec);

  return (buffer);
}


/*
 * 'parse_collection()' - Parse an IPP collection value.
 */

static ipp_t *				/* O - Collection value or @code NULL@ on error */
parse_collection(
    _ipp_file_t      *f,		/* I - IPP data file */
    _ipp_vars_t      *v,		/* I - IPP variables */
    void             *user_data)	/* I - User data pointer */
{
  ipp_t		*col = ippNew();	/* Collection value */
  ipp_attribute_t *attr = NULL;		/* Current member attribute */
  char		token[1024];		/* Token string */


 /*
  * Parse the collection value...
  */

  while (_ippFileReadToken(f, token, sizeof(token)))
  {
    if (!_cups_strcasecmp(token, "}"))
    {
     /*
      * End of collection value...
      */

      break;
    }
    else if (!_cups_strcasecmp(token, "MEMBER"))
    {
     /*
      * Member attribute definition...
      */

      char	syntax[128],		/* Attribute syntax (value tag) */
		name[128];		/* Attribute name */
      ipp_tag_t	value_tag;		/* Value tag */

      attr = NULL;

      if (!_ippFileReadToken(f, syntax, sizeof(syntax)))
      {
        fprintf(stderr, "ippserver: Missing MEMBER syntax on line %d of \"%s\".\n", f->linenum, f->filename);
	ippDelete(col);
	col = NULL;
	break;
      }
      else if ((value_tag = ippTagValue(syntax)) < IPP_TAG_UNSUPPORTED_VALUE)
      {
        fprintf(stderr, "ippserver: Bad MEMBER syntax \"%s\" on line %d of \"%s\".\n", syntax, f->linenum, f->filename);
	ippDelete(col);
	col = NULL;
	break;
      }

      if (!_ippFileReadToken(f, name, sizeof(name)) || !name[0])
      {
        fprintf(stderr, "ippserver: Missing MEMBER name on line %d of \"%s\".\n", f->linenum, f->filename);
	ippDelete(col);
	col = NULL;
	break;
      }

      if (value_tag < IPP_TAG_INTEGER)
      {
       /*
	* Add out-of-band attribute - no value string needed...
	*/

        ippAddOutOfBand(col, IPP_TAG_ZERO, value_tag, name);
      }
      else
      {
       /*
        * Add attribute with one or more values...
        */

        attr = ippAddString(col, IPP_TAG_ZERO, value_tag, name, NULL, NULL);

        if (!parse_value(f, v, user_data, col, &attr, 0))
        {
	  ippDelete(col);
	  col = NULL;
          break;
	}
      }

    }
    else if (attr && !_cups_strcasecmp(token, ","))
    {
     /*
      * Additional value...
      */

      if (!parse_value(f, v, user_data, col, &attr, ippGetCount(attr)))
      {
	ippDelete(col);
	col = NULL;
	break;
      }
    }
    else
    {
     /*
      * Something else...
      */

      fprintf(stderr, "ippserver: Unknown directive \"%s\" on line %d of \"%s\".\n", token, f->linenum, f->filename);
      ippDelete(col);
      col  = NULL;
      attr = NULL;
      break;
    }
  }

  return (col);
}


/*
 * 'parse_value()' - Parse an IPP value.
 */

static int				/* O  - 1 on success or 0 on error */
parse_value(_ipp_file_t      *f,	/* I  - IPP data file */
            _ipp_vars_t      *v,	/* I  - IPP variables */
            void             *user_data,/* I  - User data pointer */
            ipp_t            *ipp,	/* I  - IPP message */
            ipp_attribute_t  **attr,	/* IO - IPP attribute */
            int              element)	/* I  - Element number */
{
  char		value[2049],		/* Value string */
		*valueptr,		/* Pointer into value string */
		temp[2049],		/* Temporary string */
		*tempptr;		/* Pointer into temporary string */
  size_t	valuelen;		/* Length of value */


  if (!_ippFileReadToken(f, temp, sizeof(temp)))
  {
    fprintf(stderr, "ippserver: Missing value on line %d of \"%s\".\n", f->linenum, f->filename);
    return (0);
  }

  _ippVarsExpand(v, value, temp, sizeof(value));

  switch (ippGetValueTag(*attr))
  {
    case IPP_TAG_BOOLEAN :
        return (ippSetBoolean(ipp, attr, element, !_cups_strcasecmp(value, "true")));
        break;

    case IPP_TAG_ENUM :
    case IPP_TAG_INTEGER :
        return (ippSetInteger(ipp, attr, element, (int)strtol(value, NULL, 0)));
        break;

    case IPP_TAG_DATE :
        {
          int	year,			/* Year */
		month,			/* Month */
		day,			/* Day of month */
		hour,			/* Hour */
		minute,			/* Minute */
		second,			/* Second */
		utc_offset = 0;		/* Timezone offset from UTC */
          ipp_uchar_t date[11];		/* dateTime value */

          if (*value == 'P')
          {
           /*
            * Time period...
            */

            time_t	curtime;	/* Current time in seconds */
            int		period = 0,	/* Current period value */
			saw_T = 0;	/* Saw time separator */

            curtime = time(NULL);

            for (valueptr = value + 1; *valueptr; valueptr ++)
            {
              if (isdigit(*valueptr & 255))
              {
                period = (int)strtol(valueptr, &valueptr, 10);

                if (!valueptr || period < 0)
                {
		  fprintf(stderr, "ippserver: Bad dateTime value \"%s\" on line %d of \"%s\".\n", value, f->linenum, f->filename);
		  return (0);
		}
              }

              if (*valueptr == 'Y')
              {
                curtime += 365 * 86400 * period;
                period  = 0;
              }
              else if (*valueptr == 'M')
              {
                if (saw_T)
                  curtime += 60 * period;
                else
                  curtime += 30 * 86400 * period;

                period = 0;
              }
              else if (*valueptr == 'D')
              {
                curtime += 86400 * period;
                period  = 0;
              }
              else if (*valueptr == 'H')
              {
                curtime += 3600 * period;
                period  = 0;
              }
              else if (*valueptr == 'S')
              {
                curtime += period;
                period = 0;
              }
              else if (*valueptr == 'T')
              {
                saw_T  = 1;
                period = 0;
              }
              else
	      {
		fprintf(stderr, "ippserver: Bad dateTime value \"%s\" on line %d of \"%s\".\n", value, f->linenum, f->filename);
		return (0);
	      }
	    }

	    return (ippSetDate(ipp, attr, element, ippTimeToDate(curtime)));
          }
          else if (sscanf(value, "%d-%d-%dT%d:%d:%d%d", &year, &month, &day, &hour, &minute, &second, &utc_offset) < 6)
          {
           /*
            * Date/time value did not parse...
            */

	    fprintf(stderr, "ippserver: Bad dateTime value \"%s\" on line %d of \"%s\".\n", value, f->linenum, f->filename);
	    return (0);
          }

          date[0] = (ipp_uchar_t)(year >> 8);
          date[1] = (ipp_uchar_t)(year & 255);
          date[2] = (ipp_uchar_t)month;
          date[3] = (ipp_uchar_t)day;
          date[4] = (ipp_uchar_t)hour;
          date[5] = (ipp_uchar_t)minute;
          date[6] = (ipp_uchar_t)second;
          date[7] = 0;
          if (utc_offset < 0)
          {
            utc_offset = -utc_offset;
            date[8]    = (ipp_uchar_t)'-';
	  }
	  else
	  {
            date[8] = (ipp_uchar_t)'+';
	  }

          date[9]  = (ipp_uchar_t)(utc_offset / 100);
          date[10] = (ipp_uchar_t)(utc_offset % 100);

          return (ippSetDate(ipp, attr, element, date));
        }
        break;

    case IPP_TAG_RESOLUTION :
	{
	  int	xres,		/* X resolution */
		yres;		/* Y resolution */
	  char	*ptr;		/* Pointer into value */

	  xres = yres = (int)strtol(value, (char **)&ptr, 10);
	  if (ptr > value && xres > 0)
	  {
	    if (*ptr == 'x')
	      yres = (int)strtol(ptr + 1, (char **)&ptr, 10);
	  }

	  if (ptr <= value || xres <= 0 || yres <= 0 || !ptr || (_cups_strcasecmp(ptr, "dpi") && _cups_strcasecmp(ptr, "dpc") && _cups_strcasecmp(ptr, "dpcm") && _cups_strcasecmp(ptr, "other")))
	  {
	    fprintf(stderr, "ippserver: Bad resolution value \"%s\" on line %d of \"%s\".\n", value, f->linenum, f->filename);
	    return (0);
	  }

	  if (!_cups_strcasecmp(ptr, "dpi"))
	    return (ippSetResolution(ipp, attr, element, IPP_RES_PER_INCH, xres, yres));
	  else if (!_cups_strcasecmp(ptr, "dpc") || !_cups_strcasecmp(ptr, "dpcm"))
	    return (ippSetResolution(ipp, attr, element, IPP_RES_PER_CM, xres, yres));
	  else
	    return (ippSetResolution(ipp, attr, element, (ipp_res_t)0, xres, yres));
	}
	break;

    case IPP_TAG_RANGE :
	{
	  int	lower,			/* Lower value */
		upper;			/* Upper value */

          if (sscanf(value, "%d-%d", &lower, &upper) != 2)
          {
	    fprintf(stderr, "ippserver: Bad rangeOfInteger value \"%s\" on line %d of \"%s\".\n", value, f->linenum, f->filename);
	    return (0);
	  }

	  return (ippSetRange(ipp, attr, element, lower, upper));
	}
	break;

    case IPP_TAG_STRING :
        valuelen = strlen(value);

        if (value[0] == '<' && value[strlen(value) - 1] == '>')
        {
          if (valuelen & 1)
          {
	    fprintf(stderr, "ippserver: Bad octetString value on line %d of \"%s\".\n", f->linenum, f->filename);
	    return (0);
          }

          valueptr = value + 1;
          tempptr  = temp;

          while (*valueptr && *valueptr != '>')
          {
	    if (!isxdigit(valueptr[0] & 255) || !isxdigit(valueptr[1] & 255))
	    {
	      fprintf(stderr, "ippserver: Bad octetString value on line %d of \"%s\".\n", f->linenum, f->filename);
	      return (0);
	    }

            if (valueptr[0] >= '0' && valueptr[0] <= '9')
              *tempptr = (char)((valueptr[0] - '0') << 4);
	    else
              *tempptr = (char)((tolower(valueptr[0]) - 'a' + 10) << 4);

            if (valueptr[1] >= '0' && valueptr[1] <= '9')
              *tempptr |= (valueptr[1] - '0');
	    else
              *tempptr |= (tolower(valueptr[1]) - 'a' + 10);

            tempptr ++;
          }

          return (ippSetOctetString(ipp, attr, element, temp, (int)(tempptr - temp)));
        }
        else
          return (ippSetOctetString(ipp, attr, element, value, (int)valuelen));
        break;

    case IPP_TAG_TEXTLANG :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_TEXT :
    case IPP_TAG_NAME :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
        return (ippSetString(ipp, attr, element, value));
        break;

    case IPP_TAG_BEGIN_COLLECTION :
        {
          int	status;			/* Add status */
          ipp_t *col;			/* Collection value */

          if (strcmp(value, "{"))
          {
	    fprintf(stderr, "ippserver: Bad collection value on line %d of \"%s\".\n", f->linenum, f->filename);
	    return (0);
          }

          if ((col = parse_collection(f, v, user_data)) == NULL)
            return (0);

	  status = ippSetCollection(ipp, attr, element, col);
	  ippDelete(col);

	  return (status);
	}
	break;

    default :
        fprintf(stderr, "ippserver: Unsupported value on line %d of \"%s\".\n", f->linenum, f->filename);
        return (0);
  }

  return (1);
}


/*
 * 'print_escaped_string()' - Print an escaped string value.
 */

static void
print_escaped_string(
    cups_file_t *fp,			/* I - File to write to */
    const char  *s,			/* I - String to print */
    size_t      len)			/* I - Length of string */
{
  cupsFilePutChar(fp, '\"');
  while (len > 0)
  {
    if (*s == '\"')
      cupsFilePutChar(fp, '\\');
    cupsFilePutChar(fp, *s);

    s ++;
    len --;
  }
  cupsFilePutChar(fp, '\"');
}


/*
 * 'print_ipp_attr()' - Print an IPP attribute definition.
 */

static void
print_ipp_attr(
    cups_file_t     *fp,		/* I - File to write to */
    ipp_attribute_t *attr,		/* I - Attribute to print */
    int             indent)		/* I - Indentation level */
{
  int			i,		/* Looping var */
			count = ippGetCount(attr);
					/* Number of values */
  ipp_attribute_t	*colattr;	/* Collection attribute */


  if (indent == 0)
    cupsFilePrintf(fp, "ATTR %s %s", ippTagString(ippGetValueTag(attr)), ippGetName(attr));
  else
    cupsFilePrintf(fp, "%*sMEMBER %s %s", indent, "", ippTagString(ippGetValueTag(attr)), ippGetName(attr));

  switch (ippGetValueTag(attr))
  {
    case IPP_TAG_INTEGER :
    case IPP_TAG_ENUM :
	for (i = 0; i < count; i ++)
	  cupsFilePrintf(fp, "%s%d", i ? "," : " ", ippGetInteger(attr, i));
	break;

    case IPP_TAG_BOOLEAN :
	cupsFilePuts(fp, ippGetBoolean(attr, 0) ? " true" : " false");

	for (i = 1; i < count; i ++)
	  cupsFilePuts(fp, ippGetBoolean(attr, 1) ? ",true" : ",false");
	break;

    case IPP_TAG_RANGE :
	for (i = 0; i < count; i ++)
	{
	  int upper, lower = ippGetRange(attr, i, &upper);

	  cupsFilePrintf(fp, "%s%d-%d", i ? "," : " ", lower, upper);
	}
	break;

    case IPP_TAG_RESOLUTION :
	for (i = 0; i < count; i ++)
	{
	  ipp_res_t units;
	  int yres, xres = ippGetResolution(attr, i, &yres, &units);

	  cupsFilePrintf(fp, "%s%dx%d%s", i ? "," : " ", xres, yres, units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	}
	break;

    case IPP_TAG_DATE :
	for (i = 0; i < count; i ++)
	  cupsFilePrintf(fp, "%s%s", i ? "," : " ", iso_date(ippGetDate(attr, i)));
	break;

    case IPP_TAG_STRING :
	for (i = 0; i < count; i ++)
	{
	  int len;
	  const char *s = (const char *)ippGetOctetString(attr, i, &len);

	  cupsFilePuts(fp, i ? "," : " ");
	  print_escaped_string(fp, s, (size_t)len);
	}
	break;

    case IPP_TAG_TEXT :
    case IPP_TAG_TEXTLANG :
    case IPP_TAG_NAME :
    case IPP_TAG_NAMELANG :
    case IPP_TAG_KEYWORD :
    case IPP_TAG_URI :
    case IPP_TAG_URISCHEME :
    case IPP_TAG_CHARSET :
    case IPP_TAG_LANGUAGE :
    case IPP_TAG_MIMETYPE :
	for (i = 0; i < count; i ++)
	{
	  const char *s = ippGetString(attr, i, NULL);

	  cupsFilePuts(fp, i ? "," : " ");
	  print_escaped_string(fp, s, strlen(s));
	}
	break;

    case IPP_TAG_BEGIN_COLLECTION :
	for (i = 0; i < count; i ++)
	{
	  ipp_t *col = ippGetCollection(attr, i);

	  cupsFilePuts(fp, i ? ",{\n" : " {\n");
	  for (colattr = ippFirstAttribute(col); colattr; colattr = ippNextAttribute(col))
	    print_ipp_attr(fp, colattr, indent + 4);
	  cupsFilePrintf(fp, "%*s}", indent, "");
	}
	break;

    default :
        /* Out-of-band value */
	break;
  }

  cupsFilePuts(fp, "\n");
}


/*
 * 'save_printer()' - Save printer configuration information to disk.
 */

static void
save_printer(
    server_printer_t *printer,		/* I - Printer */
    const char       *directory)	/* I - Directory for conf files */
{
  char		filename[1024];		/* Filename */
  cups_file_t	*fp;			/* File pointer */
  ipp_attribute_t *attr;		/* Current attribute */
  const char	*aname;			/* Attribute name */
#ifndef _WIN32
  struct group	*grp;			/* Group information */
#endif /* !_WIN32 */
  server_icc_t	*icc;			/* Current ICC profile */
  server_lang_t	*lang;			/* Current language */
  server_device_t *device;		/* Current device */


  _cupsRWLockRead(&printer->rwlock);

  snprintf(filename, sizeof(filename), "%s/%s.conf", directory, printer->name);
  if ((fp = cupsFileOpen(filename, "w")) != NULL)
  {
    cupsFilePrintf(fp, "# Written by ippserver on %s\n", httpGetDateString(time(NULL)));

#ifndef _WIN32
    if (printer->pinfo.print_group != SERVER_GROUP_NONE && (grp = getgrgid(printer->pinfo.print_group)) != NULL)
      cupsFilePutConf(fp, "AuthPrintGroup", grp->gr_name);

    if (printer->pinfo.proxy_group != SERVER_GROUP_NONE && (grp = getgrgid(printer->pinfo.proxy_group)) != NULL)
      cupsFilePutConf(fp, "AuthProxyGroup", grp->gr_name);
#endif /* !_WIN32 */

    if (printer->pinfo.command)
      cupsFilePutConf(fp, "Command", printer->pinfo.command);

    if (printer->pinfo.device_uri)
      cupsFilePutConf(fp, "DeviceURI", printer->pinfo.device_uri);

    cupsFilePrintf(fp, "InitialState %d %d %u", printer->is_accepting, (int)printer->state, printer->state_reasons);

    if (printer->pinfo.output_format)
      cupsFilePutConf(fp, "OutputFormat", printer->pinfo.output_format);

    for (icc = (server_icc_t *)cupsArrayFirst(printer->pinfo.profiles); icc; icc = (server_icc_t *)cupsArrayNext(printer->pinfo.profiles))
    {
      const char *name = ippGetString(ippFindAttribute(icc->attrs, "profile-name", IPP_TAG_NAME), 0, NULL);
					/* Name string */

      cupsFilePuts(fp, "Profile ");
      print_escaped_string(fp, name, strlen(name));
      cupsFilePuts(fp, " ");
      print_escaped_string(fp, icc->resource->filename, strlen(icc->resource->filename));
      cupsFilePuts(fp, " {\n");

      for (attr = ippFirstAttribute(icc->attrs); attr; attr = ippNextAttribute(icc->attrs))
      {
        name = ippGetName(attr);

        if (strcmp(name, "profile-name") && strcmp(name, "profile-uri"))
          print_ipp_attr(fp, attr, 4);
      }
      cupsFilePuts(fp, "}\n");
    }

    for (lang = (server_lang_t *)cupsArrayFirst(printer->pinfo.strings); lang; lang = (server_lang_t *)cupsArrayNext(printer->pinfo.strings))
      cupsFilePrintf(fp, "Strings %s %s\n", lang->lang, lang->resource->filename);

    if (printer->pinfo.max_devices)
      cupsFilePrintf(fp, "MaxOutputDevices %d\n", printer->pinfo.max_devices);
    for (device = (server_device_t *)cupsArrayFirst(printer->pinfo.devices); device; device = (server_device_t *)cupsArrayNext(printer->pinfo.devices))
      cupsFilePutConf(fp, "OutputDevice", device->uuid);

    cupsFilePutConf(fp, "WebForms", printer->pinfo.web_forms ? "Yes" : "No");

    for (attr = ippFirstAttribute(printer->pinfo.attrs); attr; attr = ippNextAttribute(printer->pinfo.attrs))
    {
      if (ippGetGroupTag(attr) != IPP_TAG_PRINTER || (aname = ippGetName(attr)) == NULL || !attr_cb(NULL, NULL, aname))
        continue;

      print_ipp_attr(fp, attr, 0);
    }

    cupsFileClose(fp);
  }

  _cupsRWUnlock(&printer->rwlock);
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

    f->attrs     = ippNew();
    f->group_tag = IPP_TAG_PRINTER;

    return (1);
  }
#ifndef _WIN32
  else if (!_cups_strcasecmp(token, "AuthPrintGroup"))
  {
    struct group	*group;		/* Group information */

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing AuthPrintGroup value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    if ((group = getgrnam(value)) == NULL)
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Unknown AuthPrintGroup \"%s\" on line %d of \"%s\".", value, f->linenum, f->filename);
      return (0);
    }

    pinfo->print_group = group->gr_gid;
  }
  else if (!_cups_strcasecmp(token, "AuthProxyGroup"))
  {
    struct group	*group;		/* Group information */

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing AuthProxyGroup value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    if ((group = getgrnam(value)) == NULL)
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Unknown AuthProxyGroup \"%s\" on line %d of \"%s\".", value, f->linenum, f->filename);
      return (0);
    }

    pinfo->proxy_group = group->gr_gid;
  }
#endif /* !_WIN32 */
  else if (!_cups_strcasecmp(token, "Command"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Command value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->command = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "DeviceURI"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing DeviceURI value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->device_uri = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "InitialState"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing InitialState value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    pinfo->initial_accepting = (char)atoi(temp);

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing InitialState value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    pinfo->initial_state = (ipp_pstate_t)atoi(temp);

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing InitialState value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    pinfo->initial_reasons = (server_preason_t)strtol(temp, NULL, 10);
  }
  else if (!_cups_strcasecmp(token, "Make"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Make value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->make = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "MaxOutputDevices"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing MaxOutputDevices value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    pinfo->max_devices = atoi(temp);
  }
  else if (!_cups_strcasecmp(token, "Model"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Model value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->model = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "OutputDevice"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing OutputDevice UUID value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    serverCreateDevicePinfo(pinfo, temp);
  }
  else if (!_cups_strcasecmp(token, "OutputFormat"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing OutputFormat value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    pinfo->output_format = strdup(value);
  }
  else if (!_cups_strcasecmp(token, "Profile"))
  {
    server_icc_t	icc;		/* ICC profile data */
    char		filename[1024];	/* ICC file */

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Profile name on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Profile filename on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, filename, temp, sizeof(filename));

    if (!_ippFileReadToken(f, temp, sizeof(temp)) || strcmp(temp, "{"))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Profile collection on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    if ((icc.attrs = parse_collection(f, vars, pinfo)) == NULL)
      return (0);

    if ((icc.resource = serverFindResourceByFilename(filename)) == NULL)
      icc.resource = serverCreateResource(NULL, filename, "application/icc", value, value, "static-icc-profile", NULL);

    ippAddString(icc.attrs, IPP_TAG_PRINTER, IPP_TAG_NAME, "profile-name", NULL, value);
    httpAssembleURI(HTTP_URI_CODING_ALL, temp, sizeof(temp),
#ifdef HAVE_SSL
                    Encryption != HTTP_ENCRYPTION_NEVER ? SERVER_HTTPS_SCHEME : SERVER_HTTP_SCHEME,
#else
                    SERVER_HTTP_SCHEME,
#endif /* HAVE_SSL */
                    NULL, ServerName, DefaultPort, icc.resource->resource);
    ippAddString(icc.attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "profile-uri", NULL, temp);

    if (!pinfo->profiles)
      pinfo->profiles = cupsArrayNew3(NULL, NULL, NULL, 0, (cups_acopy_func_t)copy_icc, (cups_afree_func_t)free_icc);

    cupsArrayAdd(pinfo->profiles, &icc);

    serverLog(SERVER_LOGLEVEL_DEBUG, "Added ICC profile \"%s\".", filename);
  }
  else if (!_cups_strcasecmp(token, "Strings"))
  {
    server_lang_t lang;			/* New localization */
    char	stringsfile[1024];	/* Strings filename */

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Strings language on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, value, temp, sizeof(value));

    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing Strings filename on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    _ippVarsExpand(vars, stringsfile, temp, sizeof(stringsfile));

    lang.lang = value;
    if ((lang.resource = serverFindResourceByFilename(stringsfile)) == NULL)
      lang.resource = serverCreateResource(NULL, stringsfile, "text/strings", value, value, "static-strings", value);

    if (!pinfo->strings)
      pinfo->strings = cupsArrayNew3((cups_array_func_t)compare_lang, NULL, NULL, 0, (cups_acopy_func_t)copy_lang, (cups_afree_func_t)free_lang);

    cupsArrayAdd(pinfo->strings, &lang);

    serverLog(SERVER_LOGLEVEL_DEBUG, "Added strings file \"%s\" for language \"%s\".", stringsfile, value);
  }
  else if (!_cups_strcasecmp(token, "WebForms"))
  {
    if (!_ippFileReadToken(f, temp, sizeof(temp)))
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Missing WebForms value on line %d of \"%s\".", f->linenum, f->filename);
      return (0);
    }

    pinfo->web_forms = !_cups_strcasecmp(temp, "yes") || !_cups_strcasecmp(temp, "on") || !_cups_strcasecmp(temp, "true");
  }
  else
  {
    serverLog(SERVER_LOGLEVEL_ERROR, "Unknown directive \"%s\" on line %d of \"%s\".", token, f->linenum, f->filename);
    return (0);
  }

  return (1);
}

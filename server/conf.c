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
#ifndef _WIN32
#  include <fnmatch.h>
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
static server_lang_t	*copy_lang(server_lang_t *a);
static void		create_system_attributes(void);
#ifdef HAVE_AVAHI
static void		dnssd_client_cb(AvahiClient *c, AvahiClientState state, void *userdata);
#endif /* HAVE_AVAHI */
static void		dnssd_init(void);
static int		error_cb(_ipp_file_t *f, server_pinfo_t *pinfo, const char *error);
static int		finalize_system(void);
static void		free_lang(server_lang_t *a);
static int		load_system(const char *conf);
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
  char		filename[1024],		/* Configuration file/directory */
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
        pinfo.print_group = SERVER_GROUP_NONE;
	pinfo.proxy_group = SERVER_GROUP_NONE;

        snprintf(iconname, sizeof(iconname), "%s/print/%s.png", directory, dent->filename);
        if (!access(iconname, R_OK))
          pinfo.icon = strdup(iconname);

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, &pinfo, 0)) == NULL)
            continue;

          printer->state        = IPP_PSTATE_IDLE;
          printer->is_accepting = 1;

          serverAddPrinter(printer);
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
        pinfo.print_group = SERVER_GROUP_NONE;
	pinfo.proxy_group = SERVER_GROUP_NONE;

        snprintf(iconname, sizeof(iconname), "%s/print3d/%s.png", directory, dent->filename);
        if (!access(iconname, R_OK))
          pinfo.icon = strdup(iconname);

        if (serverLoadAttributes(filename, &pinfo))
	{
          snprintf(resource, sizeof(resource), "/ipp/print3d/%s", dent->filename);

	  if ((printer = serverCreatePrinter(resource, dent->filename, &pinfo, 0)) == NULL)
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
    "job-settable-attributes-supported",
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
  static const char * const device_command_supported[] =
  {					/* Values for device-command-supported */
    /* TODO: Scan BinDir for commands? Or make this configurable? */
    "ippdoclint",
    "ipptransform",
    "ipptransform3d"
  };
  static const char * const device_format_supported[] =
  {					/* Values for device-format-supported */
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
  static const char * const device_uri_schemes_supported[] =
  {					/* Values for device-uri-schemes-supported */
    "ipp",
    "ipps",
    "socket",
    "usbserial"
  };
  static const char * const printer_creation_attributes_supported[] =
  {					/* Values for printer-creation-attributes-supported */
    "auth-print-group",
    "auth-proxy-group",
    "color-supported",
    "device-command",
    "device-format",
    "device-name",
    "device-uri",
    "document-format-default",
    "document-format-supported",
    "multiple-document-jobs-supported",
    "natural-language-configured",
    "pages-per-minute",
    "pages-per-minute-color",
    "pdl-override-supported",
    "printer-device-id",
    "printer-geo-location",
    "printer-info",
    "printer-location",
    "printer-make-and-model",
    "printer-name",
    "pwg-raster-document-resolution-supported",
    "pwg-raster-document-sheet-back",
    "pwg-raster-document-type-supported",
    "urf-supported"
  };
  static const char * const resource_format_supported[] =
  {					/* Values for resource-format-supported */
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
    "static-strings"
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

  /* auth-group-supported */
#ifndef _WIN32
  alloc_groups = num_groups = 0;
  groups       = NULL;

  setgrent();
  while ((grp = getgrent()) != NULL)
  {
    if (grp->gr_name[0] == '_')
      continue;				/* Skip system groups */

    if (num_groups >= alloc_groups)
    {
      alloc_groups += 10;
      groups       = (char **)realloc(groups, (size_t)alloc_groups * sizeof(char *));
    }

    groups[num_groups ++] = strdup(grp->gr_name);
  }
  endgrent();

  if (num_groups > 0)
  {
    ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_TAG_NAME, "auth-group-supported", num_groups, NULL, (const char **)groups);

    for (i = 0; i < num_groups; i ++)
      free(groups[i]);
    free(groups);
  }
#endif /* !_WIN32 */

  /* charset-configured */
  ippAddString(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");

  /* charset-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-supported", (int)(sizeof(charset_supported) / sizeof(charset_supported[0])), NULL, charset_supported);

  /* device-command-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_NAME), "device-command-supported", (int)(sizeof(device_command_supported) / sizeof(device_command_supported[0])), NULL, device_command_supported);

  /* device-format-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "device-format-supported", (int)(sizeof(device_format_supported) / sizeof(device_format_supported[0])), NULL, device_format_supported);

  /* device-uri-schemes-supported */
  ippAddStrings(SystemAttributes, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_URISCHEME), "device-uri-schemes-supported", (int)(sizeof(device_uri_schemes_supported) / sizeof(device_uri_schemes_supported[0])), NULL, device_uri_schemes_supported);

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
  char	local[1024];			/* Local hostname */


 /*
  * Default BinDir...
  */

  if (!BinDir)
    BinDir = strdup(CUPS_SERVERBIN);

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
  * Initialize Bonjour...
  */

  dnssd_init();

 /*
  * Apply default listeners if none are specified...
  */

  if (!Listeners)
  {
#ifdef _WIN32
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
#endif /* _WIN32 */

    serverLog(SERVER_LOGLEVEL_INFO, "Using default listeners for %s:%d.", ServerName, DefaultPort);

    if (!serverCreateListeners(strcmp(ServerName, "localhost") ? NULL : "localhost", DefaultPort))
      return (0);
  }

  create_system_attributes();

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
    "AuthName",
    "AuthOperatorGroup",
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
	else
	{
#ifdef _WIN32
          port = 8631;
#else
	  port = 8000 + ((int)getuid() % 1000);
#endif /* _WIN32 */
	}

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
  else if (!_cups_strcasecmp(token, "Strings"))
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

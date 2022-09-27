/*
 * Printer object code for sample IPP server implementation.
 *
 * Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * Local functions...
 */

static int		compare_active_jobs(server_job_t *a, server_job_t *b);
static int		compare_completed_jobs(server_job_t *a, server_job_t *b);
static int		compare_jobs(server_job_t *a, server_job_t *b);
static ipp_t		*create_media_col(const char *media, const char *source, const char *type, int width, int length, int margins);
static ipp_t		*create_media_size(int width, int length);
static void		dnssd_callback(cups_dnssd_service_t *service, server_printer_t *printer, cups_dnssd_flags_t flags);


/*
 * 'serverAllocatePrinterResource()' - Allocate a resource for a printer.
 */

void
serverAllocatePrinterResource(
    server_printer_t  *printer,		/* I - Printer */
    server_resource_t *resource)	/* I - Resource to allocate */
{
  int	i;				/* Looping var */


 /*
  * See if we need to add the resource...
  */

  if (printer->num_resources >= SERVER_RESOURCES_MAX)
    return;

  for (i = 0; i < printer->num_resources; i ++)
  {
    if (printer->resources[i] == resource->id)
      return;
  }

 /*
  * Add the resource to the list...
  */

  cupsRWLockWrite(&printer->rwlock);
  cupsRWLockWrite(&resource->rwlock);

  resource->use ++;

  printer->resources[printer->num_resources ++] = resource->id;

 /*
  * And then update any printer attributes/values based on the type of
  * resource...
  */

  if (!strcmp(resource->type, "static-image") && !strcmp(resource->format, "image/png"))
  {
   /*
    * Printer icon...
    */

    printer->icon_resource = resource;
  }
  else if (!strcmp(resource->type, "static-strings"))
  {
   /*
    * Localization file...
    */

    const char *language = ippGetString(ippFindAttribute(resource->attrs, "resource-natural-language", IPP_TAG_LANGUAGE), 0, NULL);
					/* Language for string */

    if (language)
      serverAddStringsFileNoLock(printer, language, resource);
  }

  cupsRWUnlock(&resource->rwlock);
  cupsRWUnlock(&printer->rwlock);
}


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


  if (printer->state == IPP_PSTATE_STOPPED)
    creasons |= SERVER_PREASON_PAUSED;
  else
    creasons &= (server_preason_t)~SERVER_PREASON_PAUSED;

  if (creasons == SERVER_PREASON_NONE)
  {
    ippAddString(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-state-reasons", NULL, "none");
  }
  else
  {
    size_t		i,		/* Looping var */
			num_reasons = 0;/* Number of reasons */
    server_preason_t	reason;		/* Current reason */
    const char		*reasons[32];	/* Reason strings */

    for (i = 0, reason = 1; i < (sizeof(server_preasons) / sizeof(server_preasons[0])); i ++, reason <<= 1)
    {
      if (creasons & reason)
	reasons[num_reasons ++] = server_preasons[i];
    }

    ippAddStrings(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-state-reasons", num_reasons, NULL, reasons);
  }
}


/*
 * 'serverCreatePrinter()' - Create, register, and listen for connections to a
 *                           printer object.
 */

server_printer_t *			/* O - Printer */
serverCreatePrinter(
    const char     *resource,		/* I - Resource path for URIs */
    const char     *name,		/* I - printer-name */
    const char     *info,		/* I - printer-info */
    server_pinfo_t *pinfo,		/* I - Printer information */
    int            dupe_pinfo)		/* I - Duplicate printer info strings? */
{
  size_t		i;		/* Looping var */
  server_printer_t	*printer;	/* Printer */
  cups_array_t		*existing;	/* Existing attributes cache */
  char			title[256];	/* Title for attributes */
  int			is_print3d;	/* 3D printer? */
  char			device_id[1024],/* printer-device-id */
			make_model[128],/* printer-make-and-model */
			uuid[128],	/* printer-uuid */
			spooldir[1024];	/* Per-printer spool directory */
  size_t		num_formats = 0;/* Number of document-format-supported values */
  char			*defformat = NULL,
					/* document-format-default value */
			*formats[100],	/* document-format-supported values */
			*ptr;		/* Pointer into string */
  const char		*prefix;	/* Prefix string */
  size_t		num_sup_attrs;	/* Number of supported attributes */
  const char		*sup_attrs[200];/* Supported attributes */
  char			xxx_supported[256];
					/* Name of -supported attribute */
  ipp_attribute_t	*format_sup = NULL,
					/* document-format-supported */
			*media_col_database,
					/* media-col-database value */
			*media_size_supported;
					/* media-size-supported value */
  ipp_t			*media_col;	/* media-col-default value */
  int			k_supported;	/* Maximum file size supported */
#ifdef HAVE_STATVFS
  struct statvfs	spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#elif defined(HAVE_STATFS)
  struct statfs		spoolinfo;	/* FS info for spool directory */
  double		spoolsize;	/* FS size */
#endif /* HAVE_STATVFS */
  ipp_attribute_t	*attr;		/* Attribute */
  static const char * const versions[] =/* ipp-versions-supported values */
  {
    "1.0",
    "1.1",
    "2.0",
    "2.1",
    "2.2"
  };
  static const char * const features[] =/* ipp-features-supported values */
  {
    "document-object",
    "ipp-everywhere",
    "page-overrides",
    "production",
    "system-service",
    "infrastructure-printer"
  };
  static const char * const features3d[] =/* ipp-features-supported values */
  {
    "document-object",
    "ipp-3d",
    "system-service",
    "infrastructure-printer"
  };
  static const char * const notify_events_supported[] =
  {					/* notify-events-supported values */
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
    "none",
    "printer-config-changed",
    "printer-created",
    "printer-deleted",
    "printer-finishings-changed",
    "printer-media-changed",
    "printer-queue-order-changed",
    "printer-restarted",
    "printer-shutdown",
    "printer-state-changed",
    "printer-stopped",
    "resource-canceled",
    "resource-config-changed",
    "resource-created",
    "resource-installed",
    "resource-changed",
    "system-config-changed",
    "system-state-changed",
    "system-stopped"
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
    IPP_OP_HOLD_JOB,
    IPP_OP_RELEASE_JOB,
    IPP_OP_PAUSE_PRINTER,
    IPP_OP_RESUME_PRINTER,
    IPP_OP_SET_PRINTER_ATTRIBUTES,
    IPP_OP_SET_JOB_ATTRIBUTES,
    IPP_OP_GET_PRINTER_SUPPORTED_VALUES,
    IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS,
    IPP_OP_CREATE_JOB_SUBSCRIPTIONS,
    IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES,
    IPP_OP_GET_SUBSCRIPTIONS,
    IPP_OP_RENEW_SUBSCRIPTION,
    IPP_OP_CANCEL_SUBSCRIPTION,
    IPP_OP_GET_NOTIFICATIONS,
    IPP_OP_ENABLE_PRINTER,
    IPP_OP_DISABLE_PRINTER,
    IPP_OP_PAUSE_PRINTER_AFTER_CURRENT_JOB,
    IPP_OP_HOLD_NEW_JOBS,
    IPP_OP_RELEASE_HELD_NEW_JOBS,
    IPP_OP_RESTART_PRINTER,
    IPP_OP_SHUTDOWN_PRINTER,
    IPP_OP_STARTUP_PRINTER,
    IPP_OP_CANCEL_CURRENT_JOB,
    IPP_OP_CANCEL_DOCUMENT,
    IPP_OP_GET_DOCUMENT_ATTRIBUTES,
    IPP_OP_GET_DOCUMENTS,
    IPP_OP_SET_DOCUMENT_ATTRIBUTES,
    IPP_OP_CANCEL_JOBS,
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
  static const int	ops3d[] =	/* operations-supported values */
  {
    IPP_OP_VALIDATE_JOB,
    IPP_OP_CREATE_JOB,
    IPP_OP_SEND_DOCUMENT,
    IPP_OP_SEND_URI,
    IPP_OP_CANCEL_JOB,
    IPP_OP_GET_JOB_ATTRIBUTES,
    IPP_OP_GET_JOBS,
    IPP_OP_GET_PRINTER_ATTRIBUTES,
    IPP_OP_HOLD_JOB,
    IPP_OP_RELEASE_JOB,
    IPP_OP_PAUSE_PRINTER,
    IPP_OP_RESUME_PRINTER,
    IPP_OP_SET_PRINTER_ATTRIBUTES,
    IPP_OP_SET_JOB_ATTRIBUTES,
    IPP_OP_GET_PRINTER_SUPPORTED_VALUES,
    IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS,
    IPP_OP_CREATE_JOB_SUBSCRIPTIONS,
    IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES,
    IPP_OP_GET_SUBSCRIPTIONS,
    IPP_OP_RENEW_SUBSCRIPTION,
    IPP_OP_CANCEL_SUBSCRIPTION,
    IPP_OP_GET_NOTIFICATIONS,
    IPP_OP_ENABLE_PRINTER,
    IPP_OP_DISABLE_PRINTER,
    IPP_OP_PAUSE_PRINTER_AFTER_CURRENT_JOB,
    IPP_OP_HOLD_NEW_JOBS,
    IPP_OP_RELEASE_HELD_NEW_JOBS,
    IPP_OP_RESTART_PRINTER,
    IPP_OP_SHUTDOWN_PRINTER,
    IPP_OP_STARTUP_PRINTER,
    IPP_OP_CANCEL_CURRENT_JOB,
    IPP_OP_CANCEL_DOCUMENT,
    IPP_OP_GET_DOCUMENT_ATTRIBUTES,
    IPP_OP_GET_DOCUMENTS,
    IPP_OP_SET_DOCUMENT_ATTRIBUTES,
    IPP_OP_CANCEL_JOBS,
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
  static const char * const doc_settable_attributes_supported[] =
  {					/* document-settable-attributes-supported */
    "document-metadata",
    "document-name"
  };
  static const char * const doc_creation[] =
  {					/* document-creation-attributes-supported values */
    "chamber-humidity",
    "chamber-temperature",
    "copies",
    "document-access",
    "document-charset",
    "document-format",
    "document-message",
    "document-natural-language",
    "document-password",
    "finishings",
    "finishings-col",
    "image-orientation",
    "imposition-template",
    "materials-col",
    "media",
    "media-col",
    "multiple-document-handling",
    "multiple-object-handling",
    "number-up",
    "orientation-requested",
    "output-bin",
    "output-device",
    "overrides",
    "page-delivery",
    "page-ranges",
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
  static const char * const identify_actions[] =
  {
    "display",
    "sound"
  };
  static const int image_orientation_supported[] =
  {
    IPP_ORIENT_PORTRAIT,
    IPP_ORIENT_LANDSCAPE,
    IPP_ORIENT_REVERSE_LANDSCAPE,
    IPP_ORIENT_REVERSE_PORTRAIT
  };
  static const char * const imposition_template_supported[] =
  {					// imposition-template-supported values
    "booklet",
    "none"
  };
  static const char * const job_creation[] =
  {					/* job-creation-attributes-supported values */
    "chamber-humidity",
    "chamber-temperature",
    "copies",
    "cover-back",
    "cover-front",
    "document-password",
    "finishings",
    "finishings-col",
    "image-orientation",
    "imposition-template",
    "insert-sheet",
    "job-account-id",
    "job-account-type",
    "job-accounting-sheets",
    "job-accounting-user-id",
    "job-authorization-uri",
    "job-delay-output-until",
    "job-delay-output-until-time",
    "job-error-action",
    "job-error-sheet",
    "job-hold-until",
    "job-hold-until-time",
    "job-mandatory-attributes",
    "job-message-to-operator",
    "job-pages-per-set",
    "job-password",
    "job-password-encryption",
    "job-phone-number",
    "job-recipient-name",
    "job-resource-ids",
    "job-retain-until",
    "job-retain-until-time",
    "job-sheet-message",
    "job-sheets",
    "job-sheets-col",
    "materials-col",
    "media",
    "media-col",
    "multiple-document-handling",
    "multiple-object-handling",
    "number-up",
    "orientation-requested",
    "output-bin",
    "output-device",
    "overrides",
    "page-delivery",
    "page-ranges",
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
  static const char * const job_error_sheet_supported[] =
  {					// job-error-sheet-supported values
    "job-error-sheet-type",
    "job-error-sheet-when",
    "media",
    "media-col"
  };
  static const char * const job_error_sheet_type_supported[] =
  {					// job-error-sheet-type-supported values
    "none",
    "standard"
  };
  static const char * const job_error_sheet_when_supported[] =
  {					// job-error-sheet-when-supported values
    "always",
    "on-error"
  };
  static const char * const job_hold_until_supported[] =
  {					/* job-hold-until-supported values */
    "no-hold",
    "indefinite",
    "day-time",
    "evening",
    "night",
    "second-shift",
    "third-shift",
    "weekend"
  };
  static const char * const job_settable_attributes_supported[] =
  {					/* job-settable-attributes-supported */
    "document-metadata",
    "document-name",
    "job-hold-until",
    "job-hold-until-time",
    "job-name",
    "job-priority"
  };
  static const char * const job_sheets_supported[] =
  {
    "none",
    "standard"
  };
  static const char * const job_sheets_col_supported[] =
  {
    "job-sheets",
    "media",
    "media-col"
  };
  static const int media_col_sizes[][2] =
  {					/* Default media-col sizes */
    { 21590, 27940 },			/* Letter */
    { 21590, 35560 },			/* Legal */
    { 21000, 29700 }			/* A4 */
  };
  static const char * const media_col_supported[] =
  {					/* media-col-supported values */
    "media-bottom-margin",
    "media-color",
    "media-info",
    "media-key",
    "media-left-margin",
    "media-right-margin",
    "media-size",
    "media-size-name",
    "media-source",
    "media-top-margin",
    "media-type"
  };
  static const char * const media_supported[] =
  {					/* Default media-supported values */
    "na_letter_8.5x11in",		/* Letter */
    "na_legal_8.5x14in",		/* Legal */
    "iso_a4_210x297mm"			/* A4 */
  };
  static const int media_xxx_margin_supported[] =
  {					/* Default media-xxx-margin-supported values */
    635
  };
  static const char * const multiple_document_handling[] =
  {					/* multiple-document-handling-supported values */
    "separate-documents-uncollated-copies",
    "separate-documents-collated-copies"
  };
  static const char * const notify_attributes[] =
  {					/* notify-attributes-supported */
    "printer-state-change-time",
    "notify-lease-expiration-time",
    "notify-subscriber-user-name"
  };
  static const int number_up_supported[] =
  {					// "number-up-supported" values
    1,
    2,
    4,
    6,
    9,
    12,
    16
  };
  static const int orientation_requested_supported[] =
  {
    IPP_ORIENT_PORTRAIT,
    IPP_ORIENT_LANDSCAPE,
    IPP_ORIENT_REVERSE_LANDSCAPE,
    IPP_ORIENT_REVERSE_PORTRAIT,
    IPP_ORIENT_NONE
  };
  static const char * const overrides[] =
  {					/* overrides-supported */
    "document-numbers",
    "media",
    "media-col",
    "orientation-requested",
    "pages"
  };
  static const char * const print_color_mode_supported[] =
  {					/* print-color-mode-supported values */
    "auto",
    "color",
    "monochrome"
  };
  static const int	print_quality_supported[] =
  {					/* print-quality-supported values */
    IPP_QUALITY_DRAFT,
    IPP_QUALITY_NORMAL,
    IPP_QUALITY_HIGH
  };
  static const char * const print_scaling_supported[] =
  {					/* print-scaling-supported values */
    "auto",
    "auto-fit",
    "fill",
    "fit",
    "none"
  };
  static const char * const printer_settable_attributes_supported[] =
  {					/* printer-settable-attributes-supported */
    "job-constraints-supported",
    "job-presets-suppored",
    "job-resolvers-supported",
    "job-triggers-supported",
    "printer-contact-col",
    "printer-dns-sd-name",
    "printer-device-id",
    "printer-geo-location",
    "printer-icc-profiles",
    "printer-info",
    "printer-kind",
    "printer-location",
    "printer-make-and-model",
    "printer-mandatory-job-attributes",
    "printer-name",
    "printer-organization",
    "printer-organizational-unit"
  };
  static const char * const printer_supply[] =
  {					/* printer-supply values */
    "index=1;class=receptacleThatIsFilled;type=wasteToner;unit=percent;"
        "maxcapacity=100;level=67;colorantname=unknown;",
    "index=2;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=100;colorantname=black;",
    "index=3;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=25;colorantname=cyan;",
    "index=4;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=50;colorantname=magenta;",
    "index=5;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=75;colorantname=yellow;"
  };
  static const char * const printer_supply_desc[] =
  {					/* printer-supply-description values */
    "Toner Waste",
    "Black Toner",
    "Cyan Toner",
    "Magenta Toner",
    "Yellow Toner"
  };
  static const int	pwg_raster_document_resolution_supported[] =
  {
    150,
    300
  };
  static const char * const pwg_raster_document_type_supported[] =
  {
    "black_1",
    "cmyk_8",
    "sgray_8",
    "srgb_8",
    "srgb_16"
  };
  static const char * const reference_uri_schemes_supported[] =
  {					/* reference-uri-schemes-supported */
    "ftp",
    "http",
    "https",
    "file"
  };
  static const char * const separator_sheets_supported[] =
  {					// separator-sheets-supported values
    "media",
    "media-col",
    "separator-sheets-type"
  };
  static const char * const separator_sheets_type_supported[] =
  {					// separator-sheets-type-supported values
    "none",
    "both-sheets",
    "end-sheet",
    "slip-sheets",
    "start-sheet"
  };
  static const char * const sides_supported[] =
  {					/* sides-supported values */
    "one-sided",
    "two-sided-long-edge",
    "two-sided-short-edge"
  };
  static const char * const urf_supported[] =
  {					/* urf-supported values */
    "CP1",
    "IS1-5-7",
    "MT1-2-3-4-5-6-8-9-10-11-12-13",
    "RS300",
    "SRGB24",
    "V1.4",
    "W8",
    "DM1"
  };
  static const char * const uri_authentication_none[2] =
  {					/* uri-authentication-supported values for none */
    "none",
    "none"
  };
  static const char * const uri_authentication_basic[2] =
  {					/* uri-authentication-supported values for basic */
    "basic",
    "basic"
  };
  static const char * const uri_security_supported[2] =
  {					/* uri-security-supported values */
    "none",
    "tls"
  };
  static const char * const which_jobs[] =
  {					/* which-jobs-supported values */
    "aborted",
    "all",
    "canceled",
    "completed",
    "not-completed",
    "pending",
    "pending-held",
    "processing",
    "processing-stopped"
  };
  static const char * const which_jobs_proxy[] =
  {					/* which-jobs-supported values */
    "aborted",
    "all",
    "canceled",
    "completed",
    "fetchable",
    "not-completed",
    "pending",
    "pending-held",
    "processing",
    "processing-stopped"
  };
  static const char * const x_image_position_supported[] =
  {					// x-image-position-supported values
    "center",
    "left",
    "none",
    "right"
  };
  static const char * const xri_scheme_supported[] =
  {					// xri-scheme-supported values
    "ipp",
    "ipps"
  };
  static const char * const y_image_position_supported[] =
  {					// y-image-position-supported values
    "bottom",
    "center",
    "none",
    "top"
  };


  serverLog(SERVER_LOGLEVEL_DEBUG, "serverCreatePrinter(resource=\"%s\", name=\"%s\", pinfo=%p)", resource, name, (void *)pinfo);

  is_print3d = !strncmp(resource, "/ipp/print3d/", 13);

 /*
  * Allocate memory for the printer...
  */

  if ((printer = calloc(1, sizeof(server_printer_t))) == NULL)
  {
    perror("ippserver: Unable to allocate memory for printer");
    return (NULL);
  }

  if ((attr = ippFindAttribute(pinfo->attrs, "printer-id", IPP_TAG_INTEGER)) != NULL)
  {
    printer->id = ippGetInteger(attr, 0);
  }
  else
  {
    printer->id = NextPrinterId ++;

    ippAddInteger(pinfo->attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-id", printer->id);
  }

  printer->type           = is_print3d ? SERVER_TYPE_PRINT3D : SERVER_TYPE_PRINT;
  printer->resource       = strdup(resource);
  printer->resourcelen    = strlen(resource);
  printer->name           = strdup(name);
  printer->dns_sd_name    = strdup(name);
  printer->dns_sd_serial  = 1;
  printer->start_time     = time(NULL);
  printer->config_time    = printer->start_time;
  printer->state          = IPP_PSTATE_STOPPED;
  printer->state_reasons  = SERVER_PREASON_PAUSED;
  printer->state_time     = printer->start_time;
  printer->jobs           = cupsArrayNew((cups_array_cb_t)compare_jobs, NULL, NULL, 0, NULL, (cups_afree_cb_t)serverDeleteJob);
  printer->active_jobs    = cupsArrayNew((cups_array_cb_t)compare_active_jobs, NULL, NULL, 0, NULL, NULL);
  printer->completed_jobs = cupsArrayNew((cups_array_cb_t)compare_completed_jobs, NULL, NULL, 0, NULL, NULL);
  printer->next_job_id    = 1;
  printer->pinfo          = *pinfo;

  if (dupe_pinfo)
  {
    printer->pinfo.icon             = pinfo->icon ? strdup(pinfo->icon) : NULL;
    printer->pinfo.location         = pinfo->location ? strdup(pinfo->location) : NULL;
    printer->pinfo.make             = pinfo->make ? strdup(pinfo->make) : NULL;
    printer->pinfo.model            = pinfo->model ? strdup(pinfo->model) : NULL;
    printer->pinfo.document_formats = pinfo->document_formats ? strdup(pinfo->document_formats) : NULL;
    printer->pinfo.command          = pinfo->command ? strdup(pinfo->command) : NULL;
    printer->pinfo.device_uri       = pinfo->device_uri ? strdup(pinfo->device_uri) : NULL;
    printer->pinfo.output_format    = pinfo->output_format ? strdup(pinfo->output_format) : NULL;
    printer->pinfo.devices          = cupsArrayDup(pinfo->devices);
    printer->pinfo.profiles         = cupsArrayDup(pinfo->profiles);
    printer->pinfo.strings          = cupsArrayDup(pinfo->strings);
  }

  if (printer->pinfo.ppm == 0)
  {
    printer->pinfo.ppm = ippGetInteger(ippFindAttribute(printer->pinfo.attrs, "pages-per-minute", IPP_TAG_INTEGER), 0);
    serverLog(SERVER_LOGLEVEL_DEBUG, "Using ppm=%d", printer->pinfo.ppm);
  }

  if (printer->pinfo.ppm_color == 0)
  {
    printer->pinfo.ppm_color = ippGetInteger(ippFindAttribute(printer->pinfo.attrs, "pages-per-minute-color", IPP_TAG_INTEGER), 0);
    serverLog(SERVER_LOGLEVEL_DEBUG, "Using ppm_color=%d", printer->pinfo.ppm_color);
  }

  if ((attr = ippFindAttribute(printer->pinfo.attrs, "sides-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    printer->pinfo.duplex = (char)ippContainsString(attr, "two-sided-long-edge");
    serverLog(SERVER_LOGLEVEL_DEBUG, "Using duplex=%d", printer->pinfo.duplex);
  }

  cupsRWInit(&(printer->rwlock));

 /*
  * Prepare values for the printer attributes...
  */

  if (printer->pinfo.icon)
  {
    printer->icon_resource = serverCreateResource(NULL, printer->pinfo.icon, "image/png", "icon", "Printer Icon", "static-image", NULL);
    serverAllocatePrinterResource(printer, printer->icon_resource);
  }

  if (printer->pinfo.document_formats)
  {
    num_formats = 1;
    formats[0]  = strdup(printer->pinfo.document_formats);
    defformat   = formats[0];
    for (ptr = strchr(formats[0], ','); ptr; ptr = strchr(ptr, ','))
    {
      *ptr++ = '\0';
      formats[num_formats++] = ptr;

      if (!strcasecmp(ptr, "application/octet-stream"))
	defformat = ptr;
    }
  }

 /*
  * Create the printer's spool directory...
  */

  snprintf(spooldir, sizeof(spooldir), "%s/%s", SpoolDirectory, printer->name);
  if (mkdir(spooldir, 0755) && errno != EEXIST)
    serverLog(SERVER_LOGLEVEL_ERROR, "Unable to create spool directory \"%s\": %s", spooldir, strerror(errno));

 /*
  * Get the maximum spool size based on the size of the filesystem used for
  * the spool directory.  If the host OS doesn't support the statfs call
  * or the filesystem is larger than 2TiB, always report INT_MAX.
  */

#ifdef HAVE_STATVFS
  if (statvfs(spooldir, &spoolinfo))
    k_supported = INT_MAX;
  else if ((spoolsize = (double)spoolinfo.f_frsize *
                        spoolinfo.f_blocks / 1024) > INT_MAX)
    k_supported = INT_MAX;
  else
    k_supported = (int)spoolsize;

#elif defined(HAVE_STATFS)
  if (statfs(spooldir, &spoolinfo))
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

  if (!printer->pinfo.attrs)
    printer->pinfo.attrs = ippNew();

  existing = cupsArrayNew((cups_array_cb_t)strcmp, NULL, NULL, 0, NULL, NULL);
  for (attr = ippGetFirstAttribute(printer->pinfo.attrs); attr; attr = ippGetNextAttribute(printer->pinfo.attrs))
  {
    const char *attrname = ippGetName(attr);/* Attribute name */

    if (attrname)
      cupsArrayAdd(existing, (void *)attrname);
  }

  /* charset-configured */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-configured", NULL, "utf-8");

  /* charset-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_CHARSET), "charset-supported", sizeof(charsets) / sizeof(charsets[0]), NULL, charsets);

  /* color-supported */
  if (!cupsArrayFind(existing, (void *)"color-supported"))
    ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "color-supported", printer->pinfo.ppm_color > 0);

  /* compression-supported */
  if (!cupsArrayFind(existing, (void *)"compression-supported"))
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "compression-supported", (int)(sizeof(compressions) / sizeof(compressions[0])), NULL, compressions);

  /* copies-default */
  if (!cupsArrayFind(existing, (void *)"copies-default"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "copies-default", 1);

  if (printer->pinfo.proxy_group == SERVER_GROUP_NONE)
  {
    /* copies-supported */
    if (!cupsArrayFind(existing, (void *)"copies-supported"))
      ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "copies-supported", 1, is_print3d ? 1 : 999);
  }

  /* document-creation-attributes-supported */
  if (!cupsArrayFind(existing, (void *)"document-creation-attributes-supported"))
  {
   /*
    * Get the list of attributes that can be used when creating a document...
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

    for (i = 0; i < (int)(sizeof(doc_creation) / sizeof(doc_creation[0])) && num_sup_attrs < (int)(sizeof(sup_attrs) / sizeof(sup_attrs[0])); i ++)
    {
      snprintf(xxx_supported, sizeof(xxx_supported), "%s-supported", doc_creation[i]);
      if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
	sup_attrs[num_sup_attrs ++] = doc_creation[i];
    }

    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "document-creation-attributes-supported", num_sup_attrs, NULL, sup_attrs);
  }

  /* document-format-default */
  if (!cupsArrayFind(existing, (void *)"document-format-default"))
  {
    if (defformat)
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-default", NULL, defformat);
    else
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "document-format-default", NULL, "application/octet-stream");
  }

  /* document-format-supported */
  if ((format_sup = ippFindAttribute(printer->pinfo.attrs, "document-format-supported", IPP_TAG_ZERO)) == NULL && num_formats > 0)
    format_sup = ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE, "document-format-supported", num_formats, NULL, (const char * const *)formats);

  /* document-password-supported */
  if (!cupsArrayFind(existing, (void *)"document-password-supported"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "document-password-supported", 127);

  /* document-settable-attributes-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "document-settable-attributes-supported", (int)(sizeof(doc_settable_attributes_supported) / sizeof(doc_settable_attributes_supported[0])), NULL, doc_settable_attributes_supported);

  /* finishings-default */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"finishings-default"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-default", IPP_FINISHINGS_NONE);

  /* finishings-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"finishings-supported"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "finishings-supported", IPP_FINISHINGS_NONE);

  /* generated-natural-language-supported */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "generated-natural-language-supported", NULL, "en");

  /* identify-actions-default */
  if (!cupsArrayFind(existing, (void *)"identify-actions-default"))
    ippAddString (printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-default", NULL, "sound");

  /* identify-actions-supported */
  if (!cupsArrayFind(existing, (void *)"identify-actions-supported"))
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "identify-actions-supported", sizeof(identify_actions) / sizeof(identify_actions[0]), NULL, identify_actions);

  if (!is_print3d)
  {
    /* image-orientation-default */
    if (!cupsArrayFind(existing, (void *)"image-orientation-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "image-orientation-default", IPP_ORIENT_PORTRAIT);

    /* image-orientation-supported */
    if (!cupsArrayFind(existing, (void *)"image-orientation-supported"))
      ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "image-orientation-supported", sizeof(image_orientation_supported) / sizeof(image_orientation_supported[0]), image_orientation_supported);

    /* imposition-template-default */
    if (!cupsArrayFind(existing, (void *)"imposition-template-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "imposition-template-default", NULL, "none");

    /* imposition-template-supported */
    if (!cupsArrayFind(existing, (void *)"imposition-template-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "imposition-template-supported", sizeof(imposition_template_supported) / sizeof(imposition_template_supported[0]), NULL, imposition_template_supported);
  }

  /* ipp-features-supported */
  if (!cupsArrayFind(existing, (void *)"ipp-features-supported"))
  {
    if (is_print3d)
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(features3d) / sizeof(features3d[0]) - (printer->pinfo.proxy_group == SERVER_GROUP_NONE), NULL, features3d);
    else
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-features-supported", sizeof(features) / sizeof(features[0]) - (printer->pinfo.proxy_group == SERVER_GROUP_NONE), NULL, features);
  }

  /* ipp-versions-supported */
  if (!cupsArrayFind(existing, (void *)"ipp-versions-supported"))
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "ipp-versions-supported", sizeof(versions) / sizeof(versions[0]), NULL, versions);

  /* ippget-event-life */
  ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "ippget-event-life", SERVER_IPPGET_EVENT_LIFE);

  /* job-account-id-default */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-account-id-default"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-account-id-default", NULL, "");

  /* job-account-id-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-account-id-supported"))
    ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-account-id-supported", 1);

  /* job-accounting-user-id-default */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-accounting-user-id-default"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_NAME), "job-accounting-user-id-default", NULL, "");

  /* job-accounting-user-id-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-accounting-user-id-supported"))
    ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-accounting-user-id-supported", 1);

  /* job-creation-attributes-supported */
  if (!cupsArrayFind(existing, (void *)"job-creation-attributes-supported"))
  {
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
      if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
	sup_attrs[num_sup_attrs ++] = job_creation[i];
    }

    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-creation-attributes-supported", num_sup_attrs, NULL, sup_attrs);
  }

  if (!is_print3d)
  {
    /* job-error-sheet-default */
    if (!cupsArrayFind(existing, (void *)"job-error-sheet-default"))
      ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "job-error-sheet-default");

    /* job-error-sheet-supported */
    if (!cupsArrayFind(existing, (void *)"job-error-sheet-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-error-sheet-supported", sizeof(job_error_sheet_supported) / sizeof(job_error_sheet_supported[0]), NULL, job_error_sheet_supported);

    /* job-error-sheet-type-supported */
    if (!cupsArrayFind(existing, (void *)"job-error-sheet-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-error-sheet-type-supported", sizeof(job_error_sheet_type_supported) / sizeof(job_error_sheet_type_supported[0]), NULL, job_error_sheet_type_supported);

    /* job-error-sheet-when-supported */
    if (!cupsArrayFind(existing, (void *)"job-error-sheet-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-error-sheet-when-supported", sizeof(job_error_sheet_when_supported) / sizeof(job_error_sheet_when_supported[0]), NULL, job_error_sheet_when_supported);
  }

  /* job-hold-until-default */
  if (!cupsArrayFind(existing, (void *)"job-hold-until-default"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-hold-until-default", NULL, "no-hold");

  /* job-hold-until-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-hold-until-supported", sizeof(job_hold_until_supported) / sizeof(job_hold_until_supported[0]), NULL, job_hold_until_supported);

  /* job-hold-until-time-supported */
  ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-hold-until-time-supported", 0, INT_MAX);

  /* job-ids-supported */
  ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-ids-supported", 1);

  /* job-k-octets-supported */
  ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-k-octets-supported", 0,
	      k_supported);

  if (!is_print3d)
  {
    /* job-message-to-operator-default */
    if (!cupsArrayFind(existing, (void *)"job-message-to-operator-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "job-message-to-operator-default", NULL, "");

    /* job-message-to-operator-supported */
    if (!cupsArrayFind(existing, (void *)"job-message-to-operator-supported"))
      ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-message-to-operator-supported", true);
  }

  /* job-password-encryption-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-password-encryption-supported"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "job-password-encryption-supported", NULL, "none");

  /* job-password-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"job-password-supported"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-password-supported", 4);

  /* job-priority-default */
  if (!cupsArrayFind(existing, (void *)"job-priority-default"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-default", 50);

  /* job-priority-supported */
  if (!cupsArrayFind(existing, (void *)"job-priority-supported"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "job-priority-supported", 100);

  /* job-settable-attributes-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-settable-attributes-supported", (int)(sizeof(job_settable_attributes_supported) / sizeof(job_settable_attributes_supported[0])), NULL, job_settable_attributes_supported);

  if (!is_print3d)
  {
    /* job-sheet-message-default */
    if (!cupsArrayFind(existing, (void *)"job-sheet-message-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "job-sheet-message-default", NULL, "");

    /* job-sheet-message-supported */
    if (!cupsArrayFind(existing, (void *)"job-sheet-message-supported"))
      ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "job-sheet-message-supported", true);

    /* job-sheets-col-supported */
    if (!cupsArrayFind(existing, (void *)"job-sheets-col-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-sheets-col-supported", sizeof(job_sheets_col_supported) / sizeof(job_sheets_col_supported[0]), NULL, job_sheets_col_supported);

    /* job-sheets-default */
    if (!cupsArrayFind(existing, (void *)"job-sheets-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-sheets-default", NULL, "none");

    /* job-sheets-supported */
    if (!cupsArrayFind(existing, (void *)"job-sheets-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-sheets-supported", sizeof(job_sheets_supported) / sizeof(job_sheets_supported[0]), NULL, job_sheets_supported);

    if (printer->pinfo.proxy_group == SERVER_GROUP_NONE)
    {
      /* media-bottom-margin-supported */
      if (!cupsArrayFind(existing, (void *)"media-bottom-margin-supported"))
	ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-bottom-margin-supported", (int)(sizeof(media_xxx_margin_supported) / sizeof(media_xxx_margin_supported[0])), media_xxx_margin_supported);

      /* media-col-database */
      if (!cupsArrayFind(existing, (void *)"media-col-database"))
      {
	media_col_database = ippAddCollections(printer->pinfo.attrs, IPP_TAG_PRINTER, "media-col-database", (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])), NULL);
	for (i = 0; i < (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])); i ++)
	{
	  media_col = create_media_col(media_supported[i], "main", "auto", media_col_sizes[i][0], media_col_sizes[i][1], media_xxx_margin_supported[0]);

	  ippSetCollection(printer->pinfo.attrs, &media_col_database, i, media_col);

	  ippDelete(media_col);
	}
      }

      /* media-col-default */
      if (!cupsArrayFind(existing, (void *)"media-col-default"))
      {
	media_col = create_media_col(media_supported[0], "main", "auto", media_col_sizes[0][0], media_col_sizes[0][1], media_xxx_margin_supported[0]);

	ippAddCollection(printer->pinfo.attrs, IPP_TAG_PRINTER, "media-col-default", media_col);
	ippDelete(media_col);
      }

      /* media-col-ready */
      if (!cupsArrayFind(existing, (void *)"media-col-ready"))
      {
	media_col = create_media_col(media_supported[0], "main", "auto", media_col_sizes[0][0], media_col_sizes[0][1], media_xxx_margin_supported[0]);

	ippAddCollection(printer->pinfo.attrs, IPP_TAG_PRINTER, "media-col-ready", media_col);
	ippDelete(media_col);
      }

      /* media-default */
      if (!cupsArrayFind(existing, (void *)"media-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-default", NULL, media_supported[0]);

      /* media-left-margin-supported */
      if (!cupsArrayFind(existing, (void *)"media-left-margin-supported"))
	ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-left-margin-supported", (int)(sizeof(media_xxx_margin_supported) / sizeof(media_xxx_margin_supported[0])), media_xxx_margin_supported);

      /* media-ready */
      if (!cupsArrayFind(existing, (void *)"media-ready"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-ready", NULL, media_supported[0]);

      /* media-right-margin-supported */
      if (!cupsArrayFind(existing, (void *)"media-right-margin-supported"))
	ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-right-margin-supported", (int)(sizeof(media_xxx_margin_supported) / sizeof(media_xxx_margin_supported[0])), media_xxx_margin_supported);

      /* media-supported */
      if (!cupsArrayFind(existing, (void *)"media-supported"))
	ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-supported", (int)(sizeof(media_supported) / sizeof(media_supported[0])), NULL, media_supported);

      /* media-size-supported */
      if (!cupsArrayFind(existing, (void *)"media-size-supported"))
      {
	media_size_supported = ippAddCollections(printer->pinfo.attrs, IPP_TAG_PRINTER, "media-size-supported", (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0])), NULL);

	for (i = 0;
	     i < (int)(sizeof(media_col_sizes) / sizeof(media_col_sizes[0]));
	     i ++)
	{
	  ipp_t *size = create_media_size(media_col_sizes[i][0], media_col_sizes[i][1]);

	  ippSetCollection(printer->pinfo.attrs, &media_size_supported, i, size);
	  ippDelete(size);
	}
      }

      /* media-source-supported */
      if (!cupsArrayFind(existing, (void *)"media-source-supported"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-source-supported", NULL, "main");

      /* media-top-margin-supported */
      if (!cupsArrayFind(existing, (void *)"media-top-margin-supported"))
	ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "media-top-margin-supported", (int)(sizeof(media_xxx_margin_supported) / sizeof(media_xxx_margin_supported[0])), media_xxx_margin_supported);

      /* media-type-supported */
      if (!cupsArrayFind(existing, (void *)"media-type-supported"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-type-supported", NULL, "auto");

      /* media-col-supported */
      if (!cupsArrayFind(existing, (void *)"media-col-supported"))
	ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "media-col-supported", (int)(sizeof(media_col_supported) / sizeof(media_col_supported[0])), NULL, media_col_supported);
    }

    /* multiple-document-handling-supported */
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-document-handling-supported", sizeof(multiple_document_handling) / sizeof(multiple_document_handling[0]), NULL, multiple_document_handling);
  }

  /* multiple-document-jobs-supported */
  ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "multiple-document-jobs-supported", 0);

  /* multiple-operation-time-out */
  ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "multiple-operation-time-out", 60);

  /* multiple-operation-time-out-action */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "multiple-operation-time-out-action", NULL, "abort-job");

  /* natural-language-configured */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_LANGUAGE), "natural-language-configured", NULL, "en");

  /* notify-attributes-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-attributes-supported", sizeof(notify_attributes) / sizeof(notify_attributes[0]), NULL, notify_attributes);

  /* notify-events-default */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-default", NULL, "job-completed");

  /* notify-events-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events-supported", sizeof(notify_events_supported) / sizeof(notify_events_supported[0]), NULL, notify_events_supported);

  /* notify-lease-duration-default */
  ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "notify-lease-duration-default", SERVER_NOTIFY_LEASE_DURATION_DEFAULT);

  /* notify-lease-duration-supported */
  ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "notify-lease-duration-supported", 0, SERVER_NOTIFY_LEASE_DURATION_MAX);

  /* notify-max-events-supported */
  ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "notify-max-events-supported", (int)(sizeof(server_events) / sizeof(server_events[0])));

  /* notify-pull-method-supported */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-pull-method-supported", NULL, "ippget");

  /* number-up-default */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"number-up-default"))
    ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "number-up-default", 1);

  /* number-up-supported */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"number-up-supported"))
    ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "number-up-supported", sizeof(number_up_supported) / sizeof(number_up_supported[0]), number_up_supported);

  /* operations-supported */
  if (is_print3d)
    ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "operations-supported", sizeof(ops3d) / sizeof(ops3d[0]), ops3d);
  else
    ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "operations-supported", sizeof(ops) / sizeof(ops[0]), ops);

  if (!is_print3d)
  {
    /* orientation-requested-default */
    if (!cupsArrayFind(existing, (void *)"orientation-requested-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-default", IPP_ORIENT_NONE);

    /* orientation-requested-supported */
    if (!cupsArrayFind(existing, (void *)"orientation-requested-supported"))
      ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "orientation-requested-supported", sizeof(orientation_requested_supported) / sizeof(orientation_requested_supported[0]), orientation_requested_supported);

    /* output-bin-default */
    if (!cupsArrayFind(existing, (void *)"output-bin-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "output-bin-default", NULL, "face-down");

    /* output-bin-supported */
    if (!cupsArrayFind(existing, (void *)"output-bin-supported"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "output-bin-supported", NULL, "face-down");

    /* overrides-supported */
    if (!cupsArrayFind(existing, (void *)"overrides-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "overrides-supported", sizeof(overrides) / sizeof(overrides[0]), NULL, overrides);

    /* page-ranges-supported */
    if (!cupsArrayFind(existing, (void *)"page-ranges-supported"))
      ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "page-ranges-supported", 1);

    /* pages-per-minute */
    if (!cupsArrayFind(existing, (void *)"pages-per-minute"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "pages-per-minute", printer->pinfo.ppm);

    /* pages-per-minute-color */
    if (printer->pinfo.ppm_color > 0 && !cupsArrayFind(existing, (void *)"pages-per-minute-color"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "pages-per-minute-color", printer->pinfo.ppm_color);

    /* pdl-override-supported */
    if (!cupsArrayFind(existing, (void *)"pdl-override-supported"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "pdl-override-supported", NULL, "attempted");

    /* preferred-attributes-supported */
    ippAddBoolean(printer->pinfo.attrs, IPP_TAG_PRINTER, "preferred-attributes-supported", 0);

    if (printer->pinfo.proxy_group == SERVER_GROUP_NONE)
    {
      /* print-color-mode-default */
      if (!cupsArrayFind(existing, (void *)"print-color-mode-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-default", NULL, "auto");

      /* print-color-mode-supported */
      if (!cupsArrayFind(existing, (void *)"print-color-mode-supported"))
	ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-color-mode-supported", (int)(sizeof(print_color_mode_supported) / sizeof(print_color_mode_supported[0])), NULL, print_color_mode_supported);

      /* print-content-optimize-default */
      if (!cupsArrayFind(existing, (void *)"print-content-optimize-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-default", NULL, "auto");

      /* print-content-optimize-supported */
      if (!cupsArrayFind(existing, (void *)"print-content-optimize-supported"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-content-optimize-supported", NULL, "auto");

      /* print-rendering-intent-default */
      if (!cupsArrayFind(existing, (void *)"print-rendering-intent-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-rendering-intent-default", NULL, "auto");

      /* print-rendering-intent-supported */
      if (!cupsArrayFind(existing, (void *)"print-rendering-intent-supported"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-rendering-intent-supported", NULL, "auto");

      /* print-scaling-default */
      if (!cupsArrayFind(existing, (void *)"print-scaling-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-scaling-default", NULL, "auto");

      /* print-scaling-supported */
      if (!cupsArrayFind(existing, (void *)"print-scaling-supported"))
	ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "print-scaling-supported", (int)(sizeof(print_scaling_supported) / sizeof(print_scaling_supported[0])), NULL, print_scaling_supported);
    }
  }

  if (printer->pinfo.proxy_group == SERVER_GROUP_NONE)
  {
    /* print-quality-default */
    if (!cupsArrayFind(existing, (void *)"print-quality-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-default", IPP_QUALITY_NORMAL);

    /* print-quality-supported */
    if (!cupsArrayFind(existing, (void *)"print-quality-supported"))
      ippAddIntegers(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_ENUM, "print-quality-supported", (int)(sizeof(print_quality_supported) / sizeof(print_quality_supported[0])), print_quality_supported);
  }

  /* printer-device-id */
  if (!is_print3d && !cupsArrayFind(existing, (void *)"printer-device-id"))
  {
    size_t count = ippGetCount(format_sup);/* Number of supported formats */

    snprintf(device_id, sizeof(device_id), "MFG:%s;MDL:%s;", printer->pinfo.make, printer->pinfo.model);
    ptr    = device_id + strlen(device_id);
    prefix = "CMD:";
    for (i = 0; i < count; i ++)
    {
      const char *format = ippGetString(format_sup, i, NULL);
					/* Current format */

      if (!strcasecmp(format, "application/pdf"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sPDF", prefix);
      else if (!strcasecmp(format, "application/postscript"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sPS", prefix);
      else if (!strcasecmp(format, "application/vnd.hp-PCL"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sPCL", prefix);
      else if (!strcasecmp(format, "application/vnd.hp-PCLXL"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sPCLXL", prefix);
      else if (!strcasecmp(format, "image/jpeg"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sJPEG", prefix);
      else if (!strcasecmp(format, "image/png"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sPNG", prefix);
      else if (!strcasecmp(format, "image/urf"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%sURF", prefix);
      else if (strcasecmp(format, "application/octet-stream"))
	snprintf(ptr, sizeof(device_id) - (size_t)(ptr - device_id), "%s%s", prefix, format);

      ptr += strlen(ptr);
      prefix = ",";
    }
    if (ptr < (device_id + sizeof(device_id) - 1))
    {
      *ptr++ = ';';
      *ptr = '\0';
    }

    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-device-id", NULL, device_id);
  }

  /* printer-get-attributes-supported */
  ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-get-attributes-supported", NULL, "document-format");

  /* printer-geo-location */
  if (!cupsArrayFind(existing, (void *)"printer-geo-location"))
    ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_UNKNOWN, "printer-geo-location");

  /* printer-icc-profiles */
  if (!cupsArrayFind(existing, (void *)"printer-icc-profiles") && printer->pinfo.profiles)
  {
    server_icc_t	*icc;		/* ICC profile */

    for (attr = NULL, icc = (server_icc_t *)cupsArrayGetFirst(printer->pinfo.profiles); icc; icc = (server_icc_t *)cupsArrayGetNext(printer->pinfo.profiles))
    {
      serverAllocatePrinterResource(printer, icc->resource);

      if (!attr)
        attr = ippAddCollection(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-icc-profiles", icc->attrs);
      else
        ippSetCollection(printer->pinfo.attrs, &attr, ippGetCount(attr), icc->attrs);
    }
  }

  /* printer-info */
  if (!cupsArrayFind(existing, (void *)"printer-info"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", NULL, info);

  if (!is_print3d)
  {
    /* printer-input-tray */
    if (!cupsArrayFind(existing, (void *)"printer-input-tray"))
    {
      const char *tray = "type=sheetFeedAutoRemovableTray;mediafeed=0;mediaxfeed=0;maxcapacity=250;level=100;status=0;name=main;";

      ippAddOctetString(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-input-tray", tray, strlen(tray));
    }
  }

  /* printer-kind */
  if (!cupsArrayFind(existing, (void *)"printer-kind"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-kind", NULL, "document");

  /* printer-location */
  if (!cupsArrayFind(existing, (void *)"printer-location"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", NULL, printer->pinfo.location ? printer->pinfo.location : "Unknown");

  /* printer-make-and-model */
  if (!cupsArrayFind(existing, (void *)"printer-make-and-model"))
  {
    if (printer->pinfo.make && printer->pinfo.model)
      snprintf(make_model, sizeof(make_model), "%s %s", printer->pinfo.make, printer->pinfo.model);
    else
      cupsCopyString(make_model, "Unknown", sizeof(make_model));

    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", NULL, make_model);
  }

  /* printer-mandatory-job-attributes */
  if (printer->pinfo.pin && !cupsArrayFind(existing, (void *)"printer-mandatory-job-attributes"))
  {
    static const char * const names[] =	/* Attributes needed for PIN printing */
    {
      "job-account-id",
      "job-accounting-user-id",
      "job-password"
    };

    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "printer-mandatory-job-attributes", (int)(sizeof(names) / sizeof(names[0])), NULL, names);
  }

  /* printer-name */
  if (!cupsArrayFind(existing, (void *)"printer-name"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", NULL, name);

  /* printer-organization */
  if (!cupsArrayFind(existing, (void *)"printer-organization"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-organization", NULL, "IEEE-ISTO Printer Working Group");

  /* printer-organizational-unit */
  if (!cupsArrayFind(existing, (void *)"printer-organizational-unit"))
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-organizational-unit", NULL, "IPP Workgroup");

  if (!is_print3d && printer->pinfo.proxy_group == SERVER_GROUP_NONE)
  {
    /* printer-resolution-default */
    if (!cupsArrayFind(existing, (void *)"printer-resolution-default"))
      ippAddResolution(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-resolution-default", IPP_RES_PER_INCH, 600, 600);

    /* printer-resolution-supported */
    if (!cupsArrayFind(existing, (void *)"printer-resolutions-supported"))
      ippAddResolution(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-resolution-supported", IPP_RES_PER_INCH, 600, 600);
  }

  /* printer-settable-attributes-supported */
  attr = ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "printer-settable-attributes-supported", (int)(sizeof(printer_settable_attributes_supported) / sizeof(printer_settable_attributes_supported[0])), NULL, printer_settable_attributes_supported);

  for (i = 0; i < (int)(sizeof(job_creation) / sizeof(job_creation[0])); i ++)
  {
    snprintf(xxx_supported, sizeof(xxx_supported), "%s-default", job_creation[i]);
    if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
      ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), xxx_supported);

    if (!strcmp(job_creation[i], "job-account-id") || !strcmp(job_creation[i], "job-accounting-user-id") || !strcmp(job_creation[i], "job-delay-output-until") || !strcmp(job_creation[i], "job-delay-output-until-time") || !strcmp(job_creation[i], "job-hold-until") || !strcmp(job_creation[i], "job-hold-until-time") || !strcmp(job_creation[i], "job-retain-until") || !strcmp(job_creation[i], "job-retain-until-time"))
      continue;

    snprintf(xxx_supported, sizeof(xxx_supported), "%s-supported", job_creation[i]);
    if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
      ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), xxx_supported);

    snprintf(xxx_supported, sizeof(xxx_supported), "%s-ready", job_creation[i]);
    if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
      ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), xxx_supported);

    snprintf(xxx_supported, sizeof(xxx_supported), "%s-database", job_creation[i]);
    if (ippFindAttribute(printer->pinfo.attrs, xxx_supported, IPP_TAG_ZERO))
      ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), xxx_supported);
  }

  /* printer-strings-languages-supported */
  if (!cupsArrayFind(existing, (void *)"printer-strings-languages-supported") && printer->pinfo.strings)
  {
    server_lang_t *lang;

    for (attr = NULL, lang = (server_lang_t *)cupsArrayGetFirst(printer->pinfo.strings); lang; lang = (server_lang_t *)cupsArrayGetNext(printer->pinfo.strings))
    {
      if (attr)
        ippSetString(printer->pinfo.attrs, &attr, ippGetCount(attr), lang->lang);
      else
        attr = ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_LANGUAGE, "printer-strings-languages-supported", NULL, lang->lang);

      serverAllocatePrinterResource(printer, lang->resource);
    }
  }

  if (!is_print3d)
  {
    /* printer-supply */
    if (!cupsArrayFind(existing, (void *)"printer-supply"))
    {
      int count = printer->pinfo.ppm_color > 0 ? 5 : 2;
					/* Number of values */

      attr = ippAddOctetString(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-supply", printer_supply[0], strlen(printer_supply[0]));
      for (i = 1; i < count; i ++)
        ippSetOctetString(printer->pinfo.attrs, &attr, i, printer_supply[i], strlen(printer_supply[i]));
    }

    /* printer-supply-description */
    if (!cupsArrayFind(existing, (void *)"printer-supply-description"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-supply-description", printer->pinfo.ppm_color > 0 ? 5 : 2, NULL, printer_supply_desc);
  }

  /* printer-uuid */
  if (!cupsArrayFind(existing, (void *)"printer-uuid"))
  {
    httpAssembleUUID(ServerName, DefaultPort, name, 0, uuid, sizeof(uuid));
    ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uuid", NULL, uuid);
  }

  /* pwg-raster-document-xxx-supported */
  for (i = 0; i < num_formats; i ++)
    if (!strcasecmp(formats[i], "image/pwg-raster"))
      break;

  if (i < num_formats)
  {
    if (!cupsArrayFind(existing, (void *)"pwg-raster-document-resolution-supported"))
      ippAddResolutions(printer->pinfo.attrs, IPP_TAG_PRINTER, "pwg-raster-document-resolution-supported", (int)(sizeof(pwg_raster_document_resolution_supported) / sizeof(pwg_raster_document_resolution_supported[0])), IPP_RES_PER_INCH, pwg_raster_document_resolution_supported, pwg_raster_document_resolution_supported);
    if (!cupsArrayFind(existing, (void *)"pwg-raster-document-sheet-back"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", NULL, "normal");
    if (!cupsArrayFind(existing, (void *)"pwg-raster-document-type-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "pwg-raster-document-type-supported", (int)(sizeof(pwg_raster_document_type_supported) / sizeof(pwg_raster_document_type_supported[0])), NULL, pwg_raster_document_type_supported);
  }

  /* reference-uri-scheme-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_URISCHEME), "reference-uri-schemes-supported", (int)(sizeof(reference_uri_schemes_supported) / sizeof(reference_uri_schemes_supported[0])) - !FileDirectories, NULL, reference_uri_schemes_supported);

  if (!is_print3d)
  {
    /* separator-sheets-default */
    if (!cupsArrayFind(existing, (void *)"separator-sheets-default"))
      ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "separator-sheets-default");

    /* separator-sheets-supported */
    if (!cupsArrayFind(existing, (void *)"separator-sheets-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "separator-sheets-supported", sizeof(separator_sheets_supported) / sizeof(separator_sheets_supported[0]), NULL, separator_sheets_supported);

    /* separator-sheets-type-supported */
    if (!cupsArrayFind(existing, (void *)"separator-sheets-type-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "separator-sheets-type-supported", sizeof(separator_sheets_type_supported) / sizeof(separator_sheets_type_supported[0]), NULL, separator_sheets_type_supported);

    if (printer->pinfo.proxy_group == SERVER_GROUP_NONE)
    {
      /* sides-default */
      if (!cupsArrayFind(existing, (void *)"sides-default"))
	ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-default", NULL, "one-sided");

      /* sides-supported */
      if (!cupsArrayFind(existing, (void *)"sides-supported"))
	ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "sides-supported", printer->pinfo.duplex ? 3 : 1, NULL, sides_supported);
    }
  }

  /* urf-supported */
  for (i = 0; i < num_formats; i ++)
    if (!strcasecmp(formats[i], "image/urf"))
      break;

  if (i < num_formats && !cupsArrayFind(existing, "urf-supported"))
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "urf-supported", (int)(sizeof(urf_supported) / sizeof(urf_supported[0])) - !printer->pinfo.duplex, NULL, urf_supported);

  /* uri-authentication-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-authentication-supported", Encryption == HTTP_ENCRYPTION_NEVER ? 1 : 2, NULL, Authentication ? uri_authentication_basic : uri_authentication_none);

  /* uri-security-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "uri-security-supported", Encryption == HTTP_ENCRYPTION_NEVER ? 1 : 2, NULL, uri_security_supported);

  /* which-jobs-supported */
  if (printer->pinfo.proxy_group != SERVER_GROUP_NONE || printer->pinfo.max_devices > 0)
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "which-jobs-supported", sizeof(which_jobs_proxy) / sizeof(which_jobs_proxy[0]), NULL, which_jobs_proxy);
  else
    ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "which-jobs-supported", sizeof(which_jobs) / sizeof(which_jobs[0]), NULL, which_jobs);

  if (!is_print3d)
  {
    /* x-image-position-default */
    if (!cupsArrayFind(existing, (void *)"x-image-position-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "x-image-position-default", NULL, "none");

    /* x-image-position-supported */
    if (!cupsArrayFind(existing, (void *)"x-image-position-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "x-image-position-supported", sizeof(x_image_position_supported) / sizeof(x_image_position_supported[0]), NULL, x_image_position_supported);

    /* x-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"x-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-image-shift-default", 0);

    /* x-image-shift-supported */
    if (!cupsArrayFind(existing, (void *)"x-image-shift-supported"))
      ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "x-image-shift-supported", INT_MIN, INT_MAX);

    /* x-side1-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"x-side1-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-side1-image-shift-default", 0);

    /* x-side2-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"x-side2-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-side2-image-shift-default", 0);

    /* y-image-position-default */
    if (!cupsArrayFind(existing, (void *)"y-image-position-default"))
      ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "y-image-position-default", NULL, "none");

    /* y-image-position-supported */
    if (!cupsArrayFind(existing, (void *)"y-image-position-supported"))
      ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "y-image-position-supported", sizeof(y_image_position_supported) / sizeof(y_image_position_supported[0]), NULL, y_image_position_supported);

    /* y-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"y-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-image-shift-default", 0);

    /* y-image-shift-supported */
    if (!cupsArrayFind(existing, (void *)"y-image-shift-supported"))
      ippAddRange(printer->pinfo.attrs, IPP_TAG_PRINTER, "y-image-shift-supported", INT_MIN, INT_MAX);

    /* y-side1-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"y-side1-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-side1-image-shift-default", 0);

    /* y-side2-image-shift-default */
    if (!cupsArrayFind(existing, (void *)"y-side2-image-shift-default"))
      ippAddInteger(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-side2-image-shift-default", 0);
  }

  /* xri-authentication-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-authentication-supported", Encryption == HTTP_ENCRYPTION_NEVER ? 1 : 2, NULL, Authentication ? uri_authentication_basic : uri_authentication_none);

  /* xri-security-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-security-supported", Encryption == HTTP_ENCRYPTION_NEVER ? 1 : 2, NULL, uri_security_supported);

  /* xri-uri-scheme-supported */
  ippAddStrings(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_KEYWORD), "xri-scheme-supported", Encryption == HTTP_ENCRYPTION_NEVER ? 1 : 2, NULL, xri_scheme_supported);

  if (num_formats > 0)
    free(formats[0]);

  cupsArrayDelete(existing);

  snprintf(title, sizeof(title), "[Printer %s]", printer->name);
  serverLogAttributes(NULL, title, printer->pinfo.attrs, 0);

 /*
  * Register the printer with Bonjour...
  */

  if (!serverRegisterPrinter(printer))
    goto bad_printer;

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
 * 'serverDeallocatePrinterResource()' - Deallocate a resource for a printer.
 */

void
serverDeallocatePrinterResource(
    server_printer_t  *printer,		/* I - Printer */
    server_resource_t *resource)	/* I - Resource to allocate */
{
  size_t	i;			/* Looping var */


 /*
  * See if we need to remove the resource...
  */

  for (i = 0; i < printer->num_resources; i ++)
  {
    if (printer->resources[i] == resource->id)
      break;
  }

  if (i >= printer->num_resources)
    return;

 /*
  * Remove the resource from the list...
  */

  printer->num_resources --;
  if (i < printer->num_resources)
    memmove(printer->resources + i, printer->resources + i + 1, (printer->num_resources - i) * sizeof(int));

  cupsRWLockWrite(&resource->rwlock);

  resource->use --;

 /*
  * And then update any printer attributes/values based on the type of
  * resource...
  */

  if (!strcmp(resource->type, "static-image") && !strcmp(resource->format, "image/png"))
  {
   /*
    * Printer icon...
    */

    if (printer->pinfo.icon)
    {
      free(printer->pinfo.icon);
      printer->pinfo.icon = NULL;
    }
  }
  else if (!strcmp(resource->type, "static-strings"))
  {
   /*
    * Localization file...
    */

    server_lang_t	key,		/* Language key */
			*match;		/* Matching language record */

    if ((key.lang = (char *)ippGetString(ippFindAttribute(resource->attrs, "resource-natural-language", IPP_TAG_LANGUAGE), 0, NULL)) != NULL && (match = cupsArrayFind(printer->pinfo.strings, &key)) != NULL)
      cupsArrayRemove(printer->pinfo.strings, match);
  }

  cupsRWUnlock(&resource->rwlock);
}


/*
 * 'serverDeletePrinter()' - Unregister, close listen sockets, and free all
 *                           memory used by a printer object.
 */

void
serverDeletePrinter(
    server_printer_t *printer)		/* I - Printer */
{
  int			i;		/* Looping var */
  server_device_t	*device;	/* Current device */

  cupsRWLockWrite(&printer->rwlock);

  serverUnregisterPrinter(printer);

  for (i = 0; i < printer->num_resources; i ++)
  {
    server_resource_t *resource = serverFindResourceById(printer->resources[i]);

    if (resource)
    {
      cupsRWLockWrite(&resource->rwlock);
      resource->use --;
      cupsRWUnlock(&resource->rwlock);
    }
  }

  free(printer->resource);
  free(printer->dns_sd_name);
  free(printer->name);
  free(printer->pinfo.icon);
  free(printer->pinfo.command);
  free(printer->pinfo.device_uri);

  cupsArrayDelete(printer->pinfo.profiles);
  cupsArrayDelete(printer->pinfo.strings);

  for (device = (server_device_t *)cupsArrayGetFirst(printer->pinfo.devices); device; device = (server_device_t *)cupsArrayGetNext(printer->pinfo.devices))
  {
    cupsArrayRemove(printer->pinfo.devices, device);
    serverDeleteDevice(device);
  }

  ippDelete(printer->pinfo.attrs);
  ippDelete(printer->dev_attrs);

  cupsArrayDelete(printer->active_jobs);
  cupsArrayDelete(printer->completed_jobs);
  cupsArrayDelete(printer->jobs);

  free(printer->identify_message);

  cupsRWDestroy(&printer->rwlock);

  free(printer);
}


/*
 * 'serverDisablePrinter()' - Stop accepting new jobs for a printer.
 */

void
serverDisablePrinter(
    server_printer_t *printer)		/* I - Printer */
{
  cupsRWLockWrite(&printer->rwlock);

  printer->is_accepting = 0;

  serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "No longer accepting jobs.");

  cupsRWUnlock(&printer->rwlock);
}


/*
 * 'serverEnablePrinter()' - Start accepting new jobs for a printer.
 */

void
serverEnablePrinter(
    server_printer_t *printer)		/* I - Printer */
{
  cupsRWLockWrite(&printer->rwlock);

  printer->is_accepting = 1;

  serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Now accepting jobs.");

  cupsRWUnlock(&printer->rwlock);
}


/*
 * 'serverGetPrinterStateReasonsBits()' - Get the bits associated with "printer-state-reasons" values.
 */

server_preason_t			/* O - Bits */
serverGetPrinterStateReasonsBits(
    ipp_attribute_t *attr)		/* I - "printer-state-reasons" bits */
{
  size_t		i, j,		/* Looping vars */
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
        preasons |= (server_preason_t)(1 << j);
	break;
      }
    }
  }

  return (preasons);
}


/*
 * 'serverPausePrinter()' - Stop processing jobs for a printer.
 */

void
serverPausePrinter(
    server_printer_t *printer,		/* I - Printer */
    int              immediately)	/* I - Pause immediately? */
{
  cupsRWLockWrite(&printer->rwlock);

  if (printer->state != IPP_PSTATE_STOPPED)
  {
    if (printer->state == IPP_PSTATE_IDLE)
    {
      printer->state         = IPP_PSTATE_STOPPED;
      printer->state_reasons |= SERVER_PREASON_PAUSED;

      serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED |  SERVER_EVENT_PRINTER_STOPPED, "Printer stopped.");
    }
    else if (printer->state == IPP_PSTATE_PROCESSING)
    {
      if (immediately)
	serverStopJob(printer->processing_job);

      printer->state_reasons |= SERVER_PREASON_MOVING_TO_PAUSED;

      serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Stopping printer.");
    }
  }

  cupsRWUnlock(&printer->rwlock);
}


/*
 * 'serverRegisterPrinter()' - Register a printer object via DNS-SD.
 */

int					/* O - 1 on success, 0 on error */
serverRegisterPrinter(
    server_printer_t *printer)		/* I - Printer */
{
  int			is_print3d;	/* 3D printer? */
  size_t		num_txt;	/* Number of IPP TXT key/value pairs */
  cups_option_t		*txt;		/* IPP TXT key/value pairs */
  size_t		i,		/* Looping var */
			count;		/* Number of values */
  ipp_attribute_t	*color_supported,
			*document_format_supported,
			*printer_geo_location,
			*printer_kind,
			*printer_location,
			*printer_make_and_model,
			*printer_more_info,
			*printer_uuid,
			*sides_supported,
			*urf_supported;	/* Printer attributes */
  const char		*value;		/* Value string */
  char			dns_sd_name[256],/* DNS-SD name */
			formats[252],	/* List of supported formats */
			kind[251],	/* List of printer-kind values */
			urf[252],	/* List of supported URF values */
			*ptr;		/* Pointer into string */
  char			regtype[256];	/* DNS-SD service type */


  // Check whether DNS-SD is enabled...
  if (!DNSSDEnabled)
    return (1);

  // Collect values...
  cupsRWLockWrite(&printer->rwlock);

  is_print3d                = !strncmp(printer->resource, "/ipp/print3d/", 13);
  color_supported           = ippFindAttribute(printer->pinfo.attrs, "color-supported", IPP_TAG_BOOLEAN);
  document_format_supported = ippFindAttribute(printer->pinfo.attrs, "document-format-supported", IPP_TAG_MIMETYPE);
  printer_geo_location      = ippFindAttribute(printer->pinfo.attrs, "printer-geo-location", IPP_TAG_URI);
  printer_kind              = ippFindAttribute(printer->pinfo.attrs, "printer-kind", IPP_TAG_KEYWORD);
  printer_location          = ippFindAttribute(printer->pinfo.attrs, "printer-location", IPP_TAG_TEXT);
  printer_make_and_model    = ippFindAttribute(printer->pinfo.attrs, "printer-make-and-model", IPP_TAG_TEXT);
  printer_more_info         = ippFindAttribute(printer->pinfo.attrs, "printer-more-info", IPP_TAG_URI);
  printer_uuid              = ippFindAttribute(printer->pinfo.attrs, "printer-uuid", IPP_TAG_URI);
  sides_supported           = ippFindAttribute(printer->pinfo.attrs, "sides-supported", IPP_TAG_KEYWORD);
  urf_supported             = ippFindAttribute(printer->pinfo.attrs, "urf-supported", IPP_TAG_KEYWORD);

  for (i = 0, count = ippGetCount(document_format_supported), ptr = formats; i < count; i ++)
  {
    value = ippGetString(document_format_supported, i, NULL);

    if (!strcasecmp(value, "application/octet-stream"))
      continue;

    if (ptr > formats && ptr < (formats + sizeof(formats) - 1))
      *ptr++ = ',';

    cupsCopyString(ptr, value, sizeof(formats) - (size_t)(ptr - formats));
    ptr += strlen(ptr);

    if (ptr >= (formats + sizeof(formats) - 1))
      break;
  }

  memset(kind, 0, sizeof(kind));
  for (i = 0, count = ippGetCount(printer_kind), ptr = kind; i < count; i ++)
  {
    value = ippGetString(printer_kind, i, NULL);

    if (ptr > kind && ptr < (kind + sizeof(kind) - 1))
      *ptr++ = ',';

    cupsCopyString(ptr, value, sizeof(kind) - (size_t)(ptr - kind));
    ptr += strlen(ptr);

    if (ptr >= (kind + sizeof(kind) - 1))
      break;
  }

  memset(urf, 0, sizeof(urf));
  for (i = 0, count = ippGetCount(urf_supported), ptr = urf; i < count; i ++)
  {
    value = ippGetString(urf_supported, i, NULL);

    if (ptr > urf && ptr < (urf + sizeof(urf) - 1))
      *ptr++ = ',';

    cupsCopyString(ptr, value, sizeof(urf) - (size_t)(ptr - urf));
    ptr += strlen(ptr);

    if (ptr >= (urf + sizeof(urf) - 1))
      break;
  }

  // Build the TXT record for IPP...
  num_txt = cupsAddOption("rp", printer->resource + 1, 0, &txt);
  if ((value = ippGetString(printer_make_and_model, 0, NULL)) != NULL)
    num_txt = cupsAddOption("ty", value, num_txt, &txt);
  if ((value = ippGetString(printer_more_info, 0, NULL)) != NULL)
    num_txt = cupsAddOption("adminurl", value, num_txt, &txt);
  if ((value = ippGetString(printer_location, 0, NULL)) != NULL)
    num_txt = cupsAddOption("note", value, num_txt, &txt);
  num_txt = cupsAddOption("pdl", formats, num_txt, &txt);
  if (kind[0])
    num_txt = cupsAddOption("kind", kind, num_txt, &txt);
  if ((value = ippGetString(printer_uuid, 0, NULL)) != NULL)
    num_txt = cupsAddOption("UUID", value + 9, num_txt, &txt);
  if (!is_print3d)
  {
    num_txt = cupsAddOption("Color", ippGetBoolean(color_supported, 0) ? "T" : "F", num_txt, &txt);
    num_txt = cupsAddOption("Duplex", ippGetCount(sides_supported) > 1 ? "T" : "F", num_txt, &txt);
  }

  if (!is_print3d && Encryption != HTTP_ENCRYPTION_NEVER)
    num_txt = cupsAddOption("TLS", "1.3", num_txt, &txt);
  if (urf[0])
    num_txt = cupsAddOption("URF", urf, num_txt, &txt);
  num_txt = cupsAddOption("txtvers", "1", num_txt, &txt);
  num_txt = cupsAddOption("qtotal", "1", num_txt, &txt);

  // Handle DNS-SD name collisions...
  if (printer->dns_sd_collision)
  {
    printer->dns_sd_serial ++;
    printer->dns_sd_collision = false;
  }

  if (printer->dns_sd_serial > 1)
    snprintf(dns_sd_name, sizeof(dns_sd_name), "%s %d", printer->dns_sd_name, printer->dns_sd_serial);
  else
    cupsCopyString(dns_sd_name, printer->dns_sd_name, sizeof(dns_sd_name));

  // Create the service...
  cupsDNSSDServiceDelete(printer->dns_sd_service);
  if ((printer->dns_sd_service = cupsDNSSDServiceNew(DNSSDContext, CUPS_DNSSD_IF_INDEX_ANY, dns_sd_name, (cups_dnssd_service_cb_t)dnssd_callback, printer)) == NULL)
    goto fail;

  if ((value = ippGetString(printer_geo_location, 0, NULL)) != NULL)
    cupsDNSSDServiceSetLocation(printer->dns_sd_service, value);

  // Register the _printer._tcp (LPD) service type with a port number of 0 to
  // defend our service name but not actually support LPD...
  if (!cupsDNSSDServiceAdd(printer->dns_sd_service, "_printer._tcp", /*domain*/NULL, /*host*/NULL, /*port*/0, /*num_txt*/0, /*txt*/NULL))
    goto fail;

  // Then register the corresponding IPP service types with the real port
  // number to advertise our printer...
  if (!is_print3d)
  {
    if (DNSSDSubType && *DNSSDSubType)
      snprintf(regtype, sizeof(regtype), SERVER_IPP_TYPE ",%s", DNSSDSubType);
    else
      cupsCopyString(regtype, SERVER_IPP_TYPE, sizeof(regtype));

    if (!cupsDNSSDServiceAdd(printer->dns_sd_service, regtype, /*domain*/NULL, /*host*/NULL, (uint16_t)DefaultPort, num_txt, txt))
      goto fail;
  }

  if (Encryption != HTTP_ENCRYPTION_NEVER)
  {
    if (is_print3d)
    {
      if (DNSSDSubType && *DNSSDSubType)
	snprintf(regtype, sizeof(regtype), SERVER_IPPS_3D_TYPE ",%s", DNSSDSubType);
      else
	cupsCopyString(regtype, SERVER_IPPS_3D_TYPE, sizeof(regtype));
    }
    else if (DNSSDSubType && *DNSSDSubType)
      snprintf(regtype, sizeof(regtype), SERVER_IPPS_TYPE ",%s", DNSSDSubType);
    else
      cupsCopyString(regtype, SERVER_IPPS_TYPE, sizeof(regtype));

    if (!cupsDNSSDServiceAdd(printer->dns_sd_service, regtype, /*domain*/NULL, /*host*/NULL, (uint16_t)DefaultPort, num_txt, txt))
      goto fail;
  }

  // Similarly, register the _http._tcp,_printer (HTTP) service type with the
  // real port number to advertise our IPP printer...
  if (!cupsDNSSDServiceAdd(printer->dns_sd_service, SERVER_WEB_TYPE ",_printer", /*domain*/NULL, /*host*/NULL, (uint16_t)DefaultPort, num_txt, txt))
    goto fail;

  // Publish everything and return...
  cupsDNSSDServicePublish(printer->dns_sd_service);

  cupsFreeOptions(num_txt, txt);

  cupsRWUnlock(&printer->rwlock);

  return (1);

  // Common failure point...
  fail:

  cupsDNSSDServiceDelete(printer->dns_sd_service);
  printer->dns_sd_service = NULL;

  cupsFreeOptions(num_txt, txt);

  cupsRWUnlock(&printer->rwlock);

  return (0);
}


/*
 * 'serverRestartPrinter()' - Restart a printer.
 */

void
serverRestartPrinter(
    server_printer_t *printer)		/* I - Printer */
{
  server_event_t	event = SERVER_EVENT_NONE;
					/* Notification event */


  cupsRWLockWrite(&printer->rwlock);

  if (!printer->is_accepting)
  {
    printer->is_accepting = 1;
    event                 = SERVER_EVENT_PRINTER_STATE_CHANGED | SERVER_EVENT_PRINTER_RESTARTED;
  }

  if (printer->processing_job)
  {
    serverStopJob(printer->processing_job);

    printer->state_reasons |= SERVER_PREASON_PRINTER_RESTARTED;
    event                  = SERVER_EVENT_PRINTER_STATE_CHANGED;
  }
  else if (printer->state == IPP_PSTATE_STOPPED)
  {
    printer->state         = IPP_PSTATE_IDLE;
    printer->state_reasons = SERVER_PREASON_PRINTER_RESTARTED;
    event                  = SERVER_EVENT_PRINTER_STATE_CHANGED | SERVER_EVENT_PRINTER_RESTARTED;
  }

  if (event)
    serverAddEventNoLock(printer, NULL, NULL, event, printer->state == IPP_PSTATE_IDLE ? "Printer restarted." : "Printer restarting.");

  if (printer->state != IPP_PSTATE_PROCESSING)
    printer->state_reasons &= (server_preason_t)~SERVER_PREASON_PRINTER_RESTARTED;

  cupsRWUnlock(&printer->rwlock);

  if (printer->state == IPP_PSTATE_IDLE)
    serverCheckJobs(printer);
}


/*
 * 'serverResumePrinter()' - Start processing jobs for a printer.
 */

void
serverResumePrinter(
    server_printer_t *printer)		/* I - Printer */
{
  if (printer->state == IPP_PSTATE_STOPPED)
  {
    cupsRWLockWrite(&printer->rwlock);

    printer->state         = IPP_PSTATE_IDLE;
    printer->state_reasons &= (server_preason_t)~SERVER_PREASON_PAUSED;

    serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Starting printer.");

    cupsRWUnlock(&printer->rwlock);

    serverCheckJobs(printer);
  }
}


/*
 * 'serverUnregisterPrinter()' - Unregister the DNS-SD services.
 */

void
serverUnregisterPrinter(
    server_printer_t *printer)		/* I - Printer */
{
  if (!DNSSDEnabled)
    return;

  cupsRWLockWrite(&printer->rwlock);

  cupsDNSSDServiceDelete(printer->dns_sd_service);
  printer->dns_sd_service = NULL;

  cupsRWUnlock(&printer->rwlock);
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
 * 'compare_jobs()' - Compare two jobs.
 */

static int				/* O - Result of comparison */
compare_jobs(server_job_t *a,		/* I - First job */
             server_job_t *b)		/* I - Second job */
{
  return (b->id - a->id);
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
 * 'dnssd_callback()' - Handle DNS-SD registration events.
 */

static void
dnssd_callback(
    cups_dnssd_service_t *service,	/* I - Service registration */
    server_printer_t     *printer,	/* I - Printer */
    cups_dnssd_flags_t   flags)		/* I - Flags */
{
  if (flags & CUPS_DNSSD_FLAGS_ERROR)
  {
    fprintf(stderr, "Service registration for %s failed.\n", cupsDNSSDServiceGetName(service));
  }
  else if (flags & CUPS_DNSSD_FLAGS_COLLISION)
  {
    DNSSDUpdate               = true;
    printer->dns_sd_collision = true;
  }
}

//
// Option support functions for the IPP tools.
//
// Copyright © 2022 by the Printer Working Group.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "ipp-options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


//
// Constants...
//

#define DEFAULT_COLOR		"white"	// Default "media-color" value
#define DEFAULT_MARGIN_BOTTOM_TOP 1250	// Default bottom/top margin of 1/2"
#define DEFAULT_MARGIN_LEFT_RIGHT 625	// Default left/right margin of 1/4"
#define DEFAULT_SIZE_NAME	"iso_a4_210x297mm"
					// Default "media-size-name" value
#define DEFAULT_SOURCE		"auto"	// Default "media-source" value
#define DEFAULT_TYPE		"stationery"
					// Default "media-type" value


//
// Local functions...
//

static int			compare_overrides(ippopt_override_t *a, ippopt_override_t *b);
static ippopt_insert_sheet_t	*copy_insert_sheet(ippopt_insert_sheet_t *is);
static ippopt_override_t	*copy_override(ippopt_override_t *is);
static const char		*get_option(const char *name, size_t num_options, cups_option_t *options);
static bool			parse_media(const char *value, cups_size_t *media);


//
// 'ippOptionsCheckPage()' - Check whether a page number is included in the "page-ranges" value(s).
//

bool					// O - `true` if page in ranges, `false` otherwise
ippOptionsCheckPage(ipp_options_t *ippo,// I - IPP options
                    int           page)	// I - Page number (starting at 1)
{
  size_t	i;			// Looping var


  if (!ippo || ippo->num_page_ranges == 0)
    return (true);

  for (i = 0; i < ippo->num_page_ranges; i ++)
  {
    if (page >= ippo->page_ranges[i].lower && page <= ippo->page_ranges[i].upper)
      return (true);
  }

  return (false);
}


//
// 'ippOptionsDelete()' - Free memory used by IPP options.
//

void
ippOptionsDelete(ipp_options_t *ippo)	// I - IPP options
{
  // Range check input...
  if (!ippo)
    return;

  // Free memory
  cupsArrayDelete(ippo->insert_sheet);
  cupsArrayDelete(ippo->overrides);
  free(ippo);
}


//
// 'ippOptionsGetFirstPage()' - Get the first page to be printed.
//

int					// O - First page number (starting at 1
ippOptionsGetFirstPage(
    ipp_options_t *ippo)		// I - IPP options
{
  if (!ippo || ippo->num_page_ranges == 0)
    return (1);
  else
    return (ippo->page_ranges[0].lower);
}


//
// 'ippOptionsGetLastPage()' - Get the last page to be printed.
//

int					// O - Last page number (starting at 1
ippOptionsGetLastPage(
    ipp_options_t *ippo)		// I - IPP options
{
  if (!ippo || ippo->num_page_ranges == 0)
    return (INT_MAX);
  else
    return (ippo->page_ranges[ippo->num_page_ranges - 1].upper);
}


//
// 'ippOptionsGetOverride()' - Get the orientation and media for a given page and document.
//

ipp_orient_t				// O - "orientation-requested" value
ippOptionGetOverrides(
    ipp_options_t *ippo,		// I - IPP options
    int           document,		// I - Document number (starting at 1)
    int           page,			// I - Page number (starting at 1)
    cups_size_t   *media)		// O - "media"/"media-col" value
{
  ippopt_override_t	*override;	// "overrides" value
  ipp_orient_t		orient;		// "orientation-requested" value


  // Initialize defaults...
  if (media)
  {
    if (ippo)
      memcpy(media, &ippo->media, sizeof(cups_size_t));
    else
      memset(media, 0, sizeof(cups_size_t));
  }

  orient = ippo ? ippo->orientation_requested : IPP_ORIENT_NONE;

  // Range check input...
  if (!ippo || !media || document < 1 || page < 1)
    return (orient);

  // Look for potential overrides...
  for (override = (ippopt_override_t *)cupsArrayGetFirst(ippo->overrides); override; override = (ippopt_override_t *)cupsArrayGetNext(ippo->overrides))
  {
    // The array of overrides is sorted by document and page numbers...
    if (document < override->first_document)
      continue;				// Skip
    else if (document > override->last_document)
      break;				// Stop

    if (page < override->first_page)
      continue;				// Skip
    else if (page > override->last_page)
      break;				// Stop

    // Found a match, copy the override...
    memcpy(media, &override->media, sizeof(cups_size_t));
    orient = override->orientation_requested;
    break;
  }

  // Return the "orientation-requested" value for this page...
  return (orient);
}


//
// 'ippOptionsNew()' - Allocate memory for IPP options and populate.
//
// This function initializes an `ipp_options_t` structure from the environment
// and command-line options passed in "num_options" and "options".
//

ipp_options_t *				// O - IPP options
ippOptionsNew(size_t        num_options,// I - Number of command-line options
              cups_option_t *options)	// I - Command-line options
{
  ipp_options_t	*ippo;			// IPP options
  const char	*value;			// Option value...
  int		intvalue;		// Integer value...
  size_t	i;			// Looping var
  size_t	num_col;		// Number of collection values
  cups_option_t	*col;			// Collection values


  // Allocate memory and set defaults...
  if ((ippo = calloc(1, sizeof(ipp_options_t))) == NULL)
    return (NULL);

  ippo->copies                     = 1;
  ippo->image_orientation          = IPP_ORIENT_NONE;
  ippo->multiple_document_handling = IPPOPT_HANDLING_COLLATED_COPIES;
  ippo->number_up                  = 1;
  ippo->orientation_requested      = IPP_ORIENT_NONE;

  cupsCopyString(ippo->job_name, "Untitled", sizeof(ippo->job_name));
  cupsCopyString(ippo->job_originating_user_name, "Guest", sizeof(ippo->job_originating_user_name));
  cupsCopyString(ippo->job_sheets, "none", sizeof(ippo->job_sheets));
  cupsCopyString(ippo->sides, "one-sided", sizeof(ippo->sides));

  // "media" and "media-col" needs to be handled specially to make sure that
  // "media" can override "media-col-default"...
  if ((value = cupsGetOption("media", num_options, options)) == NULL)
    value = getenv("IPP_MEDIA");
  if (!value)
  {
    if ((value = get_option("media-col", num_options, options)) == NULL)
      value = get_option("media", num_options, options);
  }
  if (value)
    parse_media(value, &ippo->media);
  else
    parse_media(DEFAULT_SIZE_NAME, &ippo->media);

  ippo->job_error_sheet.media = ippo->media;
  ippo->job_sheets_media      = ippo->media;
  ippo->separator_media       = ippo->media;

  // Set the rest of the options...
  if ((value = get_option("copies", num_options, options)) != NULL && (intvalue = atoi(value)) >= 1 && intvalue <= 999)
    ippo->copies = intvalue;

  if ((value = get_option("force-front-side", num_options, options)) != NULL)
  {
    const char	*ptr;			// Pointer into value

    ptr = value;
    while (ptr && *ptr && isdigit(*ptr & 255) && ippo->num_force_front_side < (sizeof(ippo->force_front_side) / sizeof(ippo->force_front_side[0])))
    {
      ippo->force_front_side[ippo->num_force_front_side ++] = (int)strtol(ptr, (char **)&ptr, 10);

      if (ptr && *ptr == ',')
        ptr ++;
    }
  }

  if ((value = get_option("image-orientation", num_options, options)) != NULL && (intvalue = atoi(value)) >= IPP_ORIENT_PORTRAIT && intvalue <= IPP_ORIENT_NONE)
    ippo->image_orientation = (ipp_orient_t)intvalue;

  if ((value = get_option("imposition-template", num_options, options)) != NULL)
    cupsCopyString(ippo->imposition_template, value, sizeof(ippo->imposition_template));

  if ((value = get_option("insert-sheets", num_options, options)) != NULL && *value == '{')
  {
    // Parse "insert-sheets" collection value(s)...
    // TODO: Implement me
    ippo->insert_sheet = cupsArrayNew(NULL, NULL, NULL, 0, (cups_acopy_cb_t)copy_insert_sheet, (cups_afree_cb_t)free);
  }

  if ((value = get_option("job-error-sheet", num_options, options)) != NULL)
  {
    // Parse job-error-sheet collection value...
    num_col = cupsParseOptions(value, 0, &col);

    if ((value = cupsGetOption("job-error-sheet-when", num_col, col)) != NULL)
    {
      if (!strcmp(value, "always"))
        ippo->job_error_sheet.report = IPPOPT_ERROR_REPORT_ALWAYS;
      else if (!strcmp(value, "on-error"))
        ippo->job_error_sheet.report = IPPOPT_ERROR_REPORT_ON_ERROR;
    }

    cupsFreeOptions(num_col, col);
  }

  if ((value = get_option("job-name", num_options, options)) != NULL)
    cupsCopyString(ippo->job_name, value, sizeof(ippo->job_name));

  if ((value = get_option("job-originating-user-name", num_options, options)) != NULL)
    cupsCopyString(ippo->job_originating_user_name, value, sizeof(ippo->job_originating_user_name));

  if ((value = get_option("job-pages-per-set", num_options, options)) != NULL && (intvalue = atoi(value)) >= 1)
    ippo->job_pages_per_set = intvalue;

  if ((value = get_option("job-sheet-message", num_options, options)) != NULL)
    cupsCopyString(ippo->job_sheet_message, value, sizeof(ippo->job_sheet_message));

  if ((value = get_option("job-sheets-col", num_options, options)) != NULL)
  {
    // Parse "job-sheets-col" collection value...
    num_col = cupsParseOptions(value, 0, &col);

    if ((value = cupsGetOption("media-col", num_col, col)) == NULL)
      value = cupsGetOption("media", num_col, col);

    if (value)
      parse_media(value, &ippo->job_sheets_media);

    if ((value = get_option("job-sheets", num_col, col)) == NULL)
      value = "standard";

    cupsCopyString(ippo->job_sheets, value, sizeof(ippo->job_sheets));
    cupsFreeOptions(num_col, col);
  }
  else if ((value = get_option("job-sheets", num_options, options)) != NULL)
  {
    cupsCopyString(ippo->job_sheets, value, sizeof(ippo->job_sheets));
  }

  if ((value = get_option("multiple-document-handling", num_options, options)) != NULL)
  {
    static const char * const handlings[] =
    {					// "multiple-document-handling" values
      "separate-documents-collated-copies",
      "separate-documents-uncollated-copies",
      "single-document",
      "single-document-new-sheet"
    };

    for (i = 0; i < (sizeof(handlings) / sizeof(handlings[0])); i ++)
    {
      if (!strcmp(value, handlings[i]))
      {
        ippo->multiple_document_handling = (ippopt_handling_t)i;
        break;
      }
    }
  }

  if ((value = get_option("number-up", num_options, options)) != NULL && (intvalue = atoi(value)) >= 1)
    ippo->number_up = intvalue;

  if ((value = get_option("orientation-requested", num_options, options)) != NULL && (intvalue = atoi(value)) >= IPP_ORIENT_PORTRAIT && intvalue <= IPP_ORIENT_NONE)
    ippo->orientation_requested = (ipp_orient_t)intvalue;

  if ((value = get_option("output-bin", num_options, options)) != NULL)
    cupsCopyString(ippo->output_bin, value, sizeof(ippo->output_bin));

  if ((value = get_option("page-delivery", num_options, options)) != NULL)
  {
    static const char * const deliveries[] =
    {					// "page-delivery" values
      "same-order-face-down",
      "same-order-face-up",
      "reverse-order-face-down",
      "reverse-order-face-up"
    };

    for (i = 0; i < (sizeof(deliveries) / sizeof(deliveries[0])); i ++)
    {
      if (!strcmp(value, deliveries[i]))
      {
        ippo->page_delivery = (ippopt_delivery_t)i;
        break;
      }
    }
  }

  if ((value = get_option("page-ranges", num_options, options)) != NULL)
  {
    // Parse comma-delimited page ranges...
    const char	*ptr;			// Pointer into value
    int		first, last;		// First and last page

    ptr = value;
    while (ptr && *ptr && isdigit(*ptr & 255))
    {
      first = (int)strtol(ptr, (char **)&ptr, 10);

      if (ptr && *ptr == '-')
        last = (int)strtol(ptr + 1, (char **)&ptr, 10);
      else
        last = first;

      if (ippo->num_page_ranges < (sizeof(ippo->page_ranges) / sizeof(ippo->page_ranges[0])))
      {
        ippo->page_ranges[ippo->num_page_ranges].lower = first;
        ippo->page_ranges[ippo->num_page_ranges].upper = last;
        ippo->num_page_ranges ++;
      }

      if (ptr && *ptr == ',')
        ptr ++;
    }
  }

  if ((value = get_option("print-color-mode", num_options, options)) != NULL)
    cupsCopyString(ippo->print_color_mode, value, sizeof(ippo->print_color_mode));

  if ((value = get_option("print-content-optimize", num_options, options)) != NULL)
    cupsCopyString(ippo->print_content_optimize, value, sizeof(ippo->print_content_optimize));

  if ((value = get_option("print-quality", num_options, options)) != NULL && (intvalue = atoi(value)) >= IPP_QUALITY_DRAFT && intvalue <= IPP_QUALITY_HIGH)
    ippo->print_quality = (ipp_quality_t)intvalue;

  if ((value = get_option("print-rendering-intent", num_options, options)) != NULL)
    cupsCopyString(ippo->print_rendering_intent, value, sizeof(ippo->print_rendering_intent));

  if ((value = get_option("print-scaling", num_options, options)) != NULL)
  {
    static const char * const scalings[] =
    {					// "print-scaling" values
      "auto",
      "auto-fit",
      "fill",
      "fit",
      "none"
    };

    for (i = 0; i < (sizeof(scalings) / sizeof(scalings[0])); i ++)
    {
      if (!strcmp(value, scalings[i]))
      {
        ippo->print_scaling = (ippopt_scaling_t)i;
        break;
      }
    }
  }

  if ((value = get_option("printer-resolution", num_options, options)) != NULL)
  {
    int	xdpi, ydpi;			// X/Y resolution values

    if (sscanf(value, "%dx%ddpi", &xdpi, &ydpi) != 2)
    {
      if (sscanf(value, "%ddpi", &xdpi) == 1)
        ydpi = xdpi;
      else
        xdpi = ydpi = 0;
    }

    if (xdpi > 0 && ydpi > 0)
    {
      ippo->printer_resolution[0] = xdpi;
      ippo->printer_resolution[1] = ydpi;
    }
  }

  if ((value = get_option("separator-sheets", num_options, options)) != NULL)
  {
    // Parse separator-sheets collection value...
    num_col = cupsParseOptions(value, 0, &col);

    if ((value = cupsGetOption("media-col", num_col, col)) == NULL)
      value = cupsGetOption("media", num_col, col);

    if (value)
      parse_media(value, &ippo->separator_media);

    if ((value = get_option("separator-sheets-type", num_col, col)) != NULL)
    {
      static const char * const types[] =
      {					// "separator-sheets-type" values
	"none",
	"slip-sheets",
	"start-sheet",
	"end-sheet",
	"both-sheets"
      };

      for (i = 0; i < (sizeof(types) / sizeof(types[0])); i ++)
      {
	if (!strcmp(value, types[i]))
	{
	  ippo->separator_type = (ippopt_septype_t)i;
	  break;
	}
      }
    }

    cupsFreeOptions(num_col, col);
  }

  if ((value = get_option("sides", num_options, options)) != NULL)
    cupsCopyString(ippo->sides, value, sizeof(ippo->sides));

  if ((value = get_option("x-image-position", num_options, options)) != NULL)
  {
    static const char * const positions[] =
    {					// "x-image-position" values
      "none",
      "left",
      "center",
      "right"
    };

    for (i = 0; i < (sizeof(positions) / sizeof(positions[0])); i ++)
    {
      if (!strcmp(value, positions[i]))
      {
	ippo->x_image_position = (ippopt_imgpos_t)i;
	break;
      }
    }
  }

  if ((value = get_option("x-image-shift", num_options, options)) != NULL)
    ippo->x_side1_image_shift = ippo->x_side2_image_shift = atoi(value);

  if ((value = get_option("x-side1-image-shift", num_options, options)) != NULL)
    ippo->x_side1_image_shift = atoi(value);

  if ((value = get_option("x-side2-image-shift", num_options, options)) != NULL)
    ippo->x_side2_image_shift = atoi(value);

  if ((value = get_option("y-image-position", num_options, options)) != NULL)
  {
    static const char * const positions[] =
    {					// "y-image-position" values
      "none",
      "bottom",
      "center",
      "top"
    };

    for (i = 0; i < (sizeof(positions) / sizeof(positions[0])); i ++)
    {
      if (!strcmp(value, positions[i]))
      {
	ippo->y_image_position = (ippopt_imgpos_t)i;
	break;
      }
    }
  }

  if ((value = get_option("y-image-shift", num_options, options)) != NULL)
    ippo->y_side1_image_shift = ippo->y_side2_image_shift = atoi(value);

  if ((value = get_option("y-side1-image-shift", num_options, options)) != NULL)
    ippo->y_side1_image_shift = atoi(value);

  if ((value = get_option("y-side2-image-shift", num_options, options)) != NULL)
    ippo->y_side2_image_shift = atoi(value);

  if ((value = get_option("overrides", num_options, options)) != NULL && *value == '{')
  {
    // Parse "overrides" collection value(s)...
    // TODO: Implement me
    ippo->overrides = cupsArrayNew((cups_array_cb_t)compare_overrides, NULL, NULL, 0, (cups_acopy_cb_t)copy_override, (cups_afree_cb_t)free);
  }

  // Return the final IPP options...
  return (ippo);
}


//
// 'compare_overrides()' - Compare two "overrides" values...
//

static int				// O - Result of comparison
compare_overrides(ippopt_override_t *a,	// I - First override
                  ippopt_override_t *b)	// I - Second override
{
  if (a->first_document < b->first_document)
    return (-1);
  else if (a->first_document > b->first_document)
    return (1);
  else if (a->last_document < b->last_document)
    return (-1);
  else if (a->last_document > b->last_document)
    return (1);
  else if (a->first_page < b->first_page)
    return (-1);
  else if (a->first_page > b->first_page)
    return (1);
  else if (a->last_page < b->last_page)
    return (-1);
  else if (a->last_page > b->last_page)
    return (1);
  else
    return (0);
}


//
// 'copy_insert_sheet()' - Copy an "insert-sheet" value.
//

static ippopt_insert_sheet_t *		// O - New "insert-sheet" value
copy_insert_sheet(
    ippopt_insert_sheet_t *is)		// I - "insert-sheet" value
{
  ippopt_insert_sheet_t	*nis;		// New "insert-sheet" value


  if ((nis = (ippopt_insert_sheet_t *)malloc(sizeof(ippopt_insert_sheet_t))) != NULL)
    memcpy(nis, is, sizeof(ippopt_insert_sheet_t));

  return (nis);
}


//
// 'copy_override()' - Copy an "overrides" value.
//

static ippopt_override_t *		// O - New "overrides" value
copy_override(ippopt_override_t *ov)	// I - "overrides" value
{
  ippopt_override_t	*nov;		// New "overrides" value


  if ((nov = (ippopt_override_t *)malloc(sizeof(ippopt_override_t))) != NULL)
    memcpy(nov, ov, sizeof(ippopt_override_t));

  return (nov);
}


//
// 'get_option()' - Get the value of an option from the command-line or environment.
//

static const char *			// O - Value or `NULL` if not set
get_option(const char    *name,		// I - Attribute name
           size_t        num_options,	// I - Number of command-line options
           cups_option_t *options)	// I - Command-line options
{
  char		temp[1024],		// Temporary environment variable name
		*ptr;			// Pointer into temporary name
  const char	*value;			// Value


  if ((value = cupsGetOption(name, num_options, options)) == NULL)
  {
    // Try finding "IPP_NAME" in the environment...
    snprintf(temp, sizeof(temp), "IPP_%s", name);
    for (ptr = temp + 4; *ptr; ptr ++)
      *ptr = (char)toupper(*ptr);

    if ((value = getenv(temp)) == NULL)
    {
      // Nope, try "IPP_NAME_DEFAULT" in the environment...
      cupsConcatString(temp, "_DEFAULT", sizeof(temp));
      value = getenv(temp);
    }
  }

  return (value);
}


//
// 'parse_media()' - Parse a media/media-col value.
//

static bool				// O - `true` on success, `false` on error
parse_media(const char  *value,		// I - "media" or "media-col" value
            cups_size_t *media)		// O - Media value
{
  bool		margins_set = false,	// Have margins been set?
		ret = true;		// Return value
  pwg_media_t	*pwg = NULL;		// PWG media values


  // Initialize media
  memset(media, 0, sizeof(cups_size_t));

  if (*value == '{')
  {
    // Parse a "media-col" value...
    size_t	num_col;		// Number of "media-col" values
    cups_option_t *col;			// "media-col" values
    const char	*bottom_margin,		// "media-bottom-margin" value
  		*color,			// "media-color" value
		*left_margin,		// "media-left-margin" value
		*right_margin,		// "media-right-margin" value
		*size_col,		// "media-size" value
		*size_name,		// "media-size-name" value
		*source,		// "media-source" value
		*top_margin,		// "media-top-margin" value
		*type;			// "media-type" value

    num_col = cupsParseOptions(value, 0, &col);
    if ((size_name = cupsGetOption("media-size-name", num_col, col)) != NULL)
    {
      if ((pwg = pwgMediaForPWG(size_name)) != NULL)
        cupsCopyString(media->media, size_name, sizeof(media->media));
      else
        ret = false;
    }
    else if ((size_col = cupsGetOption("media-size", num_col, col)) != NULL)
    {
      size_t		num_size;	// Number of collection values
      cups_option_t	*size;		// Collection values
      const char	*x_dim,		// x-dimension
			*y_dim;		// y-dimension

      num_size = cupsParseOptions(size_col, 0, &size);
      if ((x_dim = cupsGetOption("x-dimension", num_size, size)) != NULL && (y_dim = cupsGetOption("y-dimension", num_size, size)) != NULL && (pwg = pwgMediaForSize(atoi(x_dim), atoi(y_dim))) != NULL)
        cupsCopyString(media->media, pwg->pwg, sizeof(media->media));
      else
        ret = false;

      cupsFreeOptions(num_size, size);
    }

    if (pwg)
    {
      // Copy width/length...
      media->width  = pwg->width;
      media->length = pwg->length;
    }

    // Get other media-col values...
    if ((bottom_margin = cupsGetOption("media-bottom-margin", num_col, col)) != NULL)
      media->bottom = atoi(bottom_margin);
    if ((left_margin = cupsGetOption("media-left-margin", num_col, col)) != NULL)
      media->left = atoi(left_margin);
    if ((right_margin = cupsGetOption("media-right-margin", num_col, col)) != NULL)
      media->right = atoi(right_margin);
    if ((top_margin = cupsGetOption("media-top-margin", num_col, col)) != NULL)
      media->top = atoi(top_margin);
    margins_set = bottom_margin != NULL || left_margin != NULL || right_margin != NULL || top_margin != NULL;

    if ((color = cupsGetOption("media-color", num_col, col)) != NULL)
      cupsCopyString(media->color, color, sizeof(media->color));
    if ((source = cupsGetOption("media-source", num_col, col)) != NULL)
      cupsCopyString(media->source, source, sizeof(media->source));
    if ((type = cupsGetOption("media-type", num_col, col)) != NULL)
      cupsCopyString(media->type, type, sizeof(media->type));

    // Free the "media-col" values...
    cupsFreeOptions(num_col, col);
  }
  else if ((pwg = pwgMediaForPWG(value)) != NULL)
  {
    // Use "media" size name...
    cupsCopyString(media->media, value, sizeof(media->media));
    media->width  = pwg->width;
    media->length = pwg->length;
  }
  else
  {
    // No media... :(
    ret = false;
  }

  // Set some defaults...
  if (!media->color[0])
    cupsCopyString(media->color, DEFAULT_COLOR, sizeof(media->color));

  if (!media->media[0])
  {
    pwg = pwgMediaForPWG(DEFAULT_SIZE_NAME);
    cupsCopyString(media->media, DEFAULT_SIZE_NAME, sizeof(media->media));
    media->width  = pwg->width;
    media->length = pwg->length;
  }

  if (!margins_set)
  {
    if (!strcmp(media->media, "iso_a6_105x148mm") || !strcmp(media->media, "na_index-4x6_4x6in") || !strcmp(media->media, "na_5x7_5x7in") || !strcmp(media->media, "na_govt-letter_8x10in") || strstr(media->media, "photo") != NULL)
    {
      // Standard photo sizes so use borderless margins...
      media->bottom = media->top = 0;
      media->left = media->right = 0;
    }
    else
    {
      // Normal media sizes so use default margins...
      media->bottom = media->top = DEFAULT_MARGIN_BOTTOM_TOP;
      media->left = media->right = DEFAULT_MARGIN_LEFT_RIGHT;
    }
  }

  if (!media->source[0])
    cupsCopyString(media->source, DEFAULT_SOURCE, sizeof(media->source));

  if (!media->type[0])
  {
    if (media->bottom == 0 && media->left == 0 && media->right == 0 && media->top == 0)
    {
      // Borderless so use 'photographic' type...
      cupsCopyString(media->type, "photographic", sizeof(media->type));
    }
    else
    {
      // Otherwise default type...
      cupsCopyString(media->type, DEFAULT_TYPE, sizeof(media->type));
    }
  }

  // Return...
  return (ret);
}


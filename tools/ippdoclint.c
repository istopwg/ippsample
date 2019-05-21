/*
 * ippdoclint utility for checking common print file formats.
 *
 * Copyright © 2018-2019 by the IEEE-ISTO Printer Working Group.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include <config.h>
#include <stdio.h>
#include <limits.h>
#include <cups/cups.h>
#include <cups/raster.h>
#include <cups/string-private.h>

#ifdef HAVE_COREGRAPHICS
#  include <CoreGraphics/CoreGraphics.h>
#elif defined(HAVE_MUPDF)
#  include <mupdf/fitz.h>
#endif /* HAVE_COREGRAPHICS */


/*
 * Local types...
 */

typedef struct lint_counters_s		/**** Page/Sheet/Etc. Counters ****/
{
  int	blank,				/* Number of blank pages/sheets/impressions */
	full_color,			/* Number of color pages/sheets/impressions */
	monochrome;			/* Number of monochrome pages/sheets/impressions */
} lint_counters_t;


/*
 * Local globals...
 */

static int		Errors = 0;		/* Number of errors found */
static lint_counters_t	Impressions = { 0, 0, 0 },
						/* Number of impressions */
			ImpressionsTwoSided = { 0, 0, 0 },
						/* Number of two-sided impressions */
			Pages = { 0, 0, 0 },	/* Number of input pages */
			Sheets = { 0, 0, 0 };	/* Number of media sheets */
static int		Verbosity = 0;		/* Log level */
static int		Warnings = 0;		/* Number of warnings found */


/*
 * Local functions...
 */

static int	lint_jpeg(const char *filename, int num_options, cups_option_t *options);
static int	lint_pdf(const char *filename, int num_options, cups_option_t *options);
static int	lint_raster(const char *filename, const char *content_type);
static int	load_env_options(cups_option_t **options);
static int	read_apple_raster_header(cups_file_t *fp, cups_page_header2_t *header);
static int	read_pwg_raster_header(cups_file_t *fp, unsigned syncword, cups_page_header2_t *header);
static int	read_raster_image(cups_file_t *fp, cups_page_header2_t *header, unsigned page);
static void	usage(int status) _CUPS_NORETURN;


/*
 * 'main()' - Main entry for ippdoclint.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  const char	*opt,			/* Current option */
		*content_type,		/* Content type of file */
		*filename;		/* File to check */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */


 /*
  * Process the command-line...
  */

  content_type = getenv("CONTENT_TYPE");
  filename     = NULL;
  num_options  = load_env_options(&options);

  if ((opt = getenv("SERVER_LOGLEVEL")) != NULL)
  {
    if (!strcmp(opt, "debug"))
      Verbosity = 2;
    else if (!strcmp(opt, "info"))
      Verbosity = 1;
  }

  for (i = 1; i < argc; i ++)
  {
    if (!strncmp(argv[i], "--", 2))
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
	fprintf(stderr, "ERROR: Unknown option '%s'.\n", argv[i]);
	usage(1);
      }
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
	  case 'i' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      content_type = argv[i];
	      break;

	  case 'o' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      num_options = cupsParseOptions(argv[i], num_options, &options);
	      break;

	  case 'v' : /* Be verbose... */
	      Verbosity ++;
	      break;

	  default :
	      fprintf(stderr, "ERROR: Unknown option '-%c'.\n", *opt);
	      usage(1);
	      break;
	}
      }
    }
    else if (!filename)
      filename = argv[i];
    else
      usage(1);
  }

 /*
  * Check that we have everything we need...
  */

  if (!filename)
    usage(1);

  if (!content_type)
  {
    if ((opt = strrchr(filename, '.')) != NULL)
    {
      if (!strcmp(opt, ".pdf"))
        content_type = "application/pdf";
      else if (!strcmp(opt, ".jpg") || !strcmp(opt, ".jpeg"))
        content_type = "image/jpeg";
      else if (!strcmp(opt, ".pwg"))
        content_type = "image/pwg-raster";
      else if (!strcmp(opt, ".urf"))
        content_type = "image/urf";
    }
  }

  if (!content_type)
  {
    fprintf(stderr, "ERROR: Unknown format for \"%s\", please specify with '-i' option.\n", filename);
    usage(1);
  }
  else if (!strcmp(content_type, "image/jpeg"))
  {
    if (!lint_jpeg(filename, num_options, options))
      return (1);
  }
  else if (!strcmp(content_type, "application/pdf"))
  {
    if (!lint_pdf(filename, num_options, options))
      return (1);
  }
  else if (!strcmp(content_type, "image/pwg-raster") || !strcmp(content_type, "image/urf"))
  {
    if (!lint_raster(filename, content_type))
      return (1);
  }
  else
  {
    fprintf(stderr, "ERROR: Unsupported format \"%s\" for \"%s\".\n", content_type, filename);
    usage(1);
  }

 /*
  * Write ATTR lines for the following Job attributes:
  *
  * - job-impressions
  * - job-impressions-col
  * - job-impressions-completed
  * - job-impressions-completed-col
  * - job-media-sheets
  * - job-media-sheets-col
  * - job-media-sheets-completed
  * - job-media-sheets-completed-col
  * - job-pages
  * - job-pages-col
  * - job-pages-completed
  * - job-pages-completed-col
  *
  * Also write a STATE line if the document format is bad...
  */

  if (Errors)
    fputs("STATE: +document-format-error\n", stderr);

  fprintf(stderr, "ATTR: job-pages=%d job-pages-completed=%d\n", Pages.full_color + Pages.monochrome, Pages.full_color + Pages.monochrome);
  fprintf(stderr, "ATTR: job-pages-col={full-color=%d monochrome=%d} job-pages-completed-col={full-color=%d monochrome=%d}\n", Pages.full_color, Pages.monochrome, Pages.full_color, Pages.monochrome);

  fprintf(stderr, "ATTR: job-impressions=%d job-impressions-completed=%d\n", Impressions.blank + Impressions.full_color + Impressions.monochrome + ImpressionsTwoSided.blank + ImpressionsTwoSided.full_color + ImpressionsTwoSided.monochrome, Impressions.blank + Impressions.full_color + Impressions.monochrome + Impressions.monochrome + ImpressionsTwoSided.blank + ImpressionsTwoSided.full_color + ImpressionsTwoSided.monochrome);
  fprintf(stderr, "ATTR: job-impressions-col={blank=%d blank-two-sided=%d full-color=%d full-color-two-sided=%d monochrome=%d monochrome-two-sided=%d} job-impressions-completed-col={blank=%d blank-two-sided=%d full-color=%d full-color-two-sided=%d monochrome=%d monochrome-two-sided=%d}\n", Impressions.blank, ImpressionsTwoSided.blank, Impressions.full_color, ImpressionsTwoSided.full_color, Impressions.monochrome, ImpressionsTwoSided.monochrome, Impressions.blank, ImpressionsTwoSided.blank, Impressions.full_color, ImpressionsTwoSided.full_color, Impressions.monochrome, ImpressionsTwoSided.monochrome);

  Sheets.blank      = Impressions.blank + (ImpressionsTwoSided.blank + 1) / 2;
  Sheets.full_color = Impressions.full_color + (ImpressionsTwoSided.full_color + 1) / 2;
  Sheets.monochrome = Impressions.monochrome + (ImpressionsTwoSided.monochrome + 1) / 2;

  fprintf(stderr, "ATTR: job-media-sheets=%d job-media-sheets-completed=%d\n", Sheets.blank + Sheets.full_color + Sheets.monochrome, Sheets.blank + Sheets.full_color + Sheets.monochrome);
  fprintf(stderr, "ATTR: job-media-sheets-col={blank=%d full-color=%d monochrome=%d} job-media-sheets-completed-col={blank=%d full-color=%d monochrome=%d}\n", Sheets.blank, Sheets.full_color, Sheets.monochrome, Sheets.blank, Sheets.full_color, Sheets.monochrome);

  return (0);
}


/*
 * 'lint_jpeg()' - Check a JPEG file.
 */

static int				/* O - 1 on success, 0 on failure */
lint_jpeg(const char    *filename,	/* I - File to check */
          int           num_options,	/* I - Number of options */
          cups_option_t *options)	/* I - Options */
{
  const char	*value;			/* Option value */
  int		copies;			/* copies value */
  const char	*color_mode;		/* print-color-mode value */
  cups_file_t	*fp;			/* File pointer */
  unsigned char	buffer[65536],		/* Read buffer */
		*bufptr,		/* Pointer info buffer */
		*bufend;		/* Pointer to end of buffer */
  ssize_t	bytes;			/* Bytes read */
  size_t	length;			/* Length of marker */


  if ((value = cupsGetOption("copies", num_options, options)) != NULL)
    copies = atoi(value);
  else
    copies = 1;

  color_mode = cupsGetOption("print-color-mode", num_options, options);

  if ((fp = cupsFileOpen(filename, "rb")) == NULL)
  {
    fprintf(stderr, "ERROR: Unable to open \"%s\": %s\n", filename, cupsLastErrorString());
    return (0);
  }

  if ((bytes = cupsFileRead(fp, (char *)buffer, sizeof(buffer))) < 3)
  {
    fputs("ERROR: Unable to read JPEG file.\n", stderr);
    cupsFileClose(fp);
    return (0);
  }

  if (memcmp(buffer, "\377\330\377", 3))
  {
    fputs("ERROR: Bad JPEG file.\n", stderr);
    cupsFileClose(fp);
    return (0);
  }

  bufptr = buffer + 2;
  bufend = buffer + bytes;

 /*
  * Scan the file for a SOFn marker, then we can get the dimensions...
  */

  while (bufptr < bufend)
  {
    if (*bufptr == 0xff)
    {
      bufptr ++;

      if (bufptr >= bufend)
      {
       /*
	* If we are at the end of the current buffer, re-fill and continue...
	*/

	if ((bytes = cupsFileRead(fp, (char *)buffer, sizeof(buffer))) <= 0)
	{
	  fputs("ERROR: Short JPEG file.\n", stderr);
	  cupsFileClose(fp);
	  return (0);
	}

	bufptr = buffer;
	bufend = buffer + bytes;
      }

      if (*bufptr == 0xff)
	continue;

      if ((bufptr + 16) >= bufend)
      {
       /*
	* Read more of the marker...
	*/

	bytes = (ssize_t)(bufend - bufptr);

	memmove(buffer, bufptr, bytes);
	bufptr = buffer;
	bufend = buffer + bytes;

	if ((bytes = cupsFileRead(fp, (char *)bufend, sizeof(buffer) - (size_t)bytes)) <= 0)
	{
	  fputs("ERROR: Short JPEG file.\n", stderr);
	  cupsFileClose(fp);
	  return (0);
	}

	bufend += bytes;
      }

      length = (size_t)((bufptr[1] << 8) | bufptr[2]);

      if ((*bufptr >= 0xc0 && *bufptr <= 0xc3) || (*bufptr >= 0xc5 && *bufptr <= 0xc7) || (*bufptr >= 0xc9 && *bufptr <= 0xcb) || (*bufptr >= 0xcd && *bufptr <= 0xcf))
      {
       /*
	* SOFn marker, look for dimensions...
	*/

	int width  = (bufptr[6] << 8) | bufptr[7];
	int height = (bufptr[4] << 8) | bufptr[5];
	int ncolors = bufptr[8];

        fprintf(stderr, "DEBUG: JPEG image is %dx%dx%d\n", width, height, ncolors);
        if (ncolors > 1 && (!color_mode || strcmp(color_mode, "monochrome")))
        {
          Pages.full_color ++;
          Impressions.full_color += copies;
	}
	else
	{
	  Pages.monochrome ++;
	  Impressions.monochrome += copies;
	}
        break;
      }

     /*
      * Skip past this marker...
      */

      bufptr ++;
      bytes = (ssize_t)(bufend - bufptr);

      while (length >= bytes)
      {
	length -= (size_t)bytes;

	if ((bytes = cupsFileRead(fp, (char *)buffer, sizeof(buffer))) <= 0)
	{
	  fputs("ERROR: Short JPEG file.\n", stderr);
	  cupsFileClose(fp);
	  return (0);
	}

	bufptr = buffer;
	bufend = buffer + bytes;
      }

      if (length > bytes)
	break;

      bufptr += length;
    }
  }

  return (1);
}


/*
 * 'lint_pdf()' - Check a PDF file.
 */

static int				/* O - 1 on success, 0 on failure */
lint_pdf(const char    *filename,	/* I - File to check */
	 int           num_options,	/* I - Number of options */
	 cups_option_t *options)	/* I - Options */
{
  const char		*value;		/* Option value */
  int			copies;		/* copies value */
  const char		*color_mode;	/* print-color-mode value */
  int			duplex;		/* Duplex printing? */
  int			first_page,	/* First page in range */
			last_page,	/* Last page in range */
			num_pages;	/* Number of pages */
#ifdef HAVE_COREGRAPHICS
  CFURLRef		url;		/* CFURL object for PDF filename */
  CGPDFDocumentRef	document = NULL;/* Input document */
#elif defined(HAVE_MUPDF)
  fz_context		*context;	/* MuPDF context */
  fz_document		*document;	/* Document to print */
#endif /* HAVE_COREGRAPHICS */


 /*
  * Gather options...
  */

  if ((value = cupsGetOption("copies", num_options, options)) != NULL)
    copies = atoi(value);
  else
    copies = 1;

  if ((value = cupsGetOption("page-ranges", num_options, options)) != NULL)
  {
    if (sscanf(value, "%u-%u", &first_page, &last_page) != 2)
    {
      first_page = 1;
      last_page  = INT_MAX;
    }
  }
  else
  {
    first_page = 1;
    last_page  = INT_MAX;
  }

  color_mode = cupsGetOption("print-color-mode", num_options, options);

  if ((value = cupsGetOption("sides", num_options, options)) != NULL)
    duplex = !strncmp(value, "two-sided-", 10);
  else
    duplex = 0;

#ifdef HAVE_COREGRAPHICS
 /*
  * Open the PDF...
  */

  if ((url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)filename, (CFIndex)strlen(filename), false)) == NULL)
  {
    fputs("ERROR: Unable to create CFURL for file.\n", stderr);
    return (0);
  }

  document = CGPDFDocumentCreateWithURL(url);
  CFRelease(url);

  if (!document)
  {
    fputs("ERROR: Unable to create CFPDFDocument for file.\n", stderr);
    return (0);
  }

  if (CGPDFDocumentIsEncrypted(document))
  {
   /*
    * Only support encrypted PDFs with a blank password...
    */

    if (!CGPDFDocumentUnlockWithPassword(document, ""))
    {
      fputs("ERROR: Document is encrypted and cannot be unlocked.\n", stderr);
      CGPDFDocumentRelease(document);
      Errors ++;
      return (0);
    }
  }

  if (!CGPDFDocumentAllowsPrinting(document))
  {
    fputs("ERROR: Document does not allow printing.\n", stderr);
    CGPDFDocumentRelease(document);
    Errors ++;
    return (0);
  }

  num_pages = (int)CGPDFDocumentGetNumberOfPages(document);

  fprintf(stderr, "DEBUG: Total pages in PDF document is %d.\n", num_pages);

  if (first_page > num_pages)
  {
    fputs("ERROR: \"page-ranges\" value does not include any pages to print in the document.\n", stderr);
    CGPDFDocumentRelease(document);
    return (0);
  }

  if (last_page > num_pages)
    last_page = num_pages;

 /*
  * Close the PDF file...
  */

  CGPDFDocumentRelease(document);

 /*
  * For now, assume all pages are color unless 'monochrome' is specified.  In
  * the future we can use the CGPDFContentStream, CGPDFOperatorTable, and
  * CGPDFScanner APIs to capture the graphics operations on each page and then
  * mark pages as color or grayscale...
  */


#elif defined(HAVE_MUPDF)
 /*
  * Open the PDF file...
  */

  if ((context = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED)) == NULL)
  {
    fputs("ERROR: Unable to create context.\n", stderr);
    Errors ++;
    return (0);
  }

  fz_register_document_handlers(context);

  fz_try(context) document = fz_open_document(context, filename);
  fz_catch(context)
  {
    fprintf(stderr, "ERROR: Unable to open '%s': %s\n", filename, fz_caught_message(context));
    fz_drop_context(context);
    Errors ++;
    return (0);
  }

  if (fz_needs_password(context, document))
  {
    fputs("ERROR: Document is encrypted and cannot be unlocked.\n", stderr);
    fz_drop_document(context, document);
    fz_drop_context(context);
    Errors ++;
    return (0);
  }

  num_pages = (int)fz_count_pages(context, document);
  if (first_page > num_pages)
  {
    fputs("ERROR: \"page-ranges\" value does not include any pages to print in the document.\n", stderr);

    fz_drop_document(context, document);
    fz_drop_context(context);

    return (0);
  }

  if (last_page > num_pages)
    last_page = num_pages;

 /*
  * Close the PDF file...
  */

  fz_drop_document(context, document);
  fz_drop_context(context);

 /*
  * For now, assume all pages are color unless 'monochrome' is specified.  In
  * the future we might use MuPDF functions to mark individual pages as color or
  * grayscale...
  */

#endif /* HAVE_COREGRAPHICS */

 /*
  * Update the page counters...
  */

  num_pages = last_page - first_page + 1;

  if (!color_mode || strcmp(color_mode, "monochrome"))
  {
   /*
    * All pages are color...
    */

    Pages.full_color += num_pages;

    if (duplex)
    {
      if (num_pages & 1)
        ImpressionsTwoSided.blank += copies;

      ImpressionsTwoSided.full_color += copies * num_pages;
    }
    else
    {
      Impressions.full_color += copies * num_pages;
    }
  }
  else
  {
   /*
    * All pages are grayscale...
    */

    Pages.monochrome += num_pages;

    if (duplex)
    {
      if (num_pages & 1)
        ImpressionsTwoSided.blank += copies;

      ImpressionsTwoSided.monochrome += copies * num_pages;
    }
    else
    {
      Impressions.monochrome += copies * num_pages;
    }
  }

  return (1);
}


/*
 * 'lint_raster()' - Check an Apple/CUPS/PWG Raster file.
 */

static int				/* O - 1 on success, 0 on failure */
lint_raster(const char *filename,	/* I - File to check */
	    const char *content_type)	/* I - Content type */
{
  cups_file_t		*fp;		/* File pointer */
  cups_page_header2_t	header;		/* Page header */
  unsigned		page = 0;	/* Page number */


  (void)content_type;

  if ((fp = cupsFileOpen(filename, "rb")) == NULL)
  {
    fprintf(stderr, "ERROR: Unable to open \"%s\": %s\n", filename, cupsLastErrorString());
    return (0);
  }

  if (!_cups_strcasecmp(content_type, "image/pwg-raster"))
  {
    unsigned		syncword;	/* Sync word */

    if (cupsFileRead(fp, (char *)&syncword, sizeof(syncword)) != sizeof(syncword))
    {
      fputs("ERROR: Unable to read sync word from PWG Raster file.\n", stderr);
      Errors ++;
    }
    else if (syncword != CUPS_RASTER_SYNCv2 && syncword != CUPS_RASTER_REVSYNCv2)
    {
      fprintf(stderr, "ERROR: Bad sync word 0x%08x seen in PWG Raster file.\n", syncword);
      Errors ++;
    }
    else
    {
      while (read_pwg_raster_header(fp, syncword, &header))
      {
	page ++;
	if (!read_raster_image(fp, &header, page))
	  break;
      }
    }
  }
  else
  {
    unsigned char	fheader[12];	/* File header */
    unsigned		num_pages;	/* Number of pages */

    if (cupsFileRead(fp, (char *)fheader, sizeof(fheader)) != sizeof(fheader))
    {
      fputs("ERROR: Unable to read header from Apple raster file.\n", stderr);
      Errors ++;
    }
    else  if (memcmp(fheader, "UNIRAST", 8))
    {
      fputs("ERROR: Bad Apple Raster header seen.\n", stderr);
      Errors ++;
    }
    else
    {
      num_pages = (unsigned)((fheader[8] << 24) | (fheader[9] << 16) | (fheader[10] << 8) | fheader[11]);

      while (read_apple_raster_header(fp, &header))
      {
	page ++;
	if (!read_raster_image(fp, &header, page))
	  break;
      }

      if (num_pages > 0 && page != num_pages)
      {
	fprintf(stderr, "ERROR: Actual number of pages (%u) does not match file header (%u).\n", page, num_pages);
	Errors ++;
      }
    }
  }

  cupsFileClose(fp);

  return (Errors == 0);
}


/*
 * 'load_env_options()' - Load options from the environment.
 */

extern char **environ;

static int				/* O - Number of options */
load_env_options(
    cups_option_t **options)		/* I - Options */
{
  int	i;				/* Looping var */
  char	name[256],			/* Option name */
	*nameptr,			/* Pointer into name */
	*envptr;			/* Pointer into environment variable */
  int	num_options = 0;		/* Number of options */


  *options = NULL;

 /*
  * Load all of the IPP_xxx environment variables as options...
  */

  for (i = 0; environ[i]; i ++)
  {
    envptr = environ[i];

    if (strncmp(envptr, "IPP_", 4))
      continue;

    for (nameptr = name, envptr += 4; *envptr && *envptr != '='; envptr ++)
    {
      if (nameptr > (name + sizeof(name) - 1))
        continue;

      if (!strncmp(envptr, "_DEFAULT=", 9))
        break;
      else if (*envptr == '_')
        *nameptr++ = '-';
      else
        *nameptr++ = (char)_cups_tolower(*envptr);
    }

    *nameptr = '\0';

    if (!strncmp(envptr, "_DEFAULT=", 9))
    {
     /*
      * For xxx-default values, only override if base value isn't set.
      */

      if (cupsGetOption(name, num_options, *options))
        continue;

      envptr += 9;
    }
    else if (*envptr == '=')
      envptr ++;

    num_options = cupsAddOption(name, envptr, num_options, options);
  }

  return (num_options);
}


/*
 * 'read_apple_raster_header()' - Read a page header from an Apple raster file.
 */

static int				/* O - 1 on success, 0 on error */
read_apple_raster_header(
    cups_file_t         *fp,		/* I - File pointer */
    cups_page_header2_t *header)	/* O - Raster header */
{
  unsigned char	pheader[32];		/* Page header */


  memset(header, 0, sizeof(cups_page_header2_t));

  if (cupsFileRead(fp, (char *)pheader, sizeof(pheader)) != sizeof(pheader))
    return (0);

  switch (pheader[1])
  {
    case 0 : /* W */
        header->cupsColorSpace = CUPS_CSPACE_SW;
        header->cupsNumColors  = 1;
        break;

    case 1 : /* sRGB */
        header->cupsColorSpace = CUPS_CSPACE_SRGB;
        header->cupsNumColors  = 3;
        break;

    case 3 : /* AdobeRGB */
        header->cupsColorSpace = CUPS_CSPACE_ADOBERGB;
        header->cupsNumColors  = 3;
        break;

    case 4 : /* DeviceW */
        header->cupsColorSpace = CUPS_CSPACE_W;
        header->cupsNumColors  = 1;
        break;

    case 5 : /* DeviceRGB */
        header->cupsColorSpace = CUPS_CSPACE_RGB;
        header->cupsNumColors  = 3;
        break;

    case 6 : /* DeviceCMYK */
        header->cupsColorSpace = CUPS_CSPACE_CMYK;
        header->cupsNumColors  = 4;
        break;

    default :
        fprintf(stderr, "ERROR: Unknown Apple Raster colorspace %u.\n", pheader[1]);
        Errors ++;
        return (0);
  }

  if ((header->cupsNumColors == 1 && pheader[0] != 8 && pheader[0] != 16) ||
      (header->cupsNumColors == 3 && pheader[0] != 24 && pheader[0] != 48) ||
      (header->cupsNumColors == 4 && pheader[0] != 32 && pheader[0] != 64))
  {
    fprintf(stderr, "ERROR: Invalid bits per pixel value %u.\n", pheader[0]);
    Errors ++;
    return (0);
  }

  header->cupsBitsPerPixel = pheader[0];
  header->cupsBitsPerColor = pheader[0] / header->cupsNumColors;
  header->cupsWidth        = (unsigned)((pheader[12] << 24) | (pheader[13] << 16) | (pheader[14] << 8) | pheader[15]);
  header->cupsHeight       = (unsigned)((pheader[16] << 24) | (pheader[17] << 16) | (pheader[18] << 8) | pheader[19]);
  header->cupsBytesPerLine = header->cupsWidth * header->cupsBitsPerPixel / 8;

  return (1);
}


/*
 * 'read_pwg_raster_header()' - Read a page header from a PWG raster file.
 */

static int				/* O - 1 on success, 0 on error */
read_pwg_raster_header(
    cups_file_t         *fp,		/* I - File pointer */
    unsigned            syncword,	/* I - Sync word from the file */
    cups_page_header2_t *header)	/* O - Raster header */
{
  int		i;			/* Looping/temp var */
  unsigned	num_colors,		/* Number of colors */
		bytes_per_line;		/* Expected bytes per line */
  static const char * const when_enum[] =
  {					/* Human-readable 'When' values, also used by AdvanceMedia, Jog */
    "Never",
    "AfterDocument",
    "AfterJob",
    "AfterSet",
    "AfterPage"
  };
  static const char * const media_position_enum[] =
  {					/* Human-readable media position values */
    "Auto",
    "Main",
    "Alternate",
    "LargeCapacity",
    "Manual",
    "Envelope",
    "Disc",
    "Photo",
    "Hagaki",
    "MainRoll",
    "AlternateRoll",
    "Top",
    "Middle",
    "Bottom",
    "Side",
    "Left",
    "Right",
    "Center",
    "Rear",
    "ByPassTray",
    "Tray1",
    "Tray2",
    "Tray3",
    "Tray4",
    "Tray5",
    "Tray6",
    "Tray7",
    "Tray8",
    "Tray9",
    "Tray10",
    "Tray11",
    "Tray12",
    "Tray13",
    "Tray14",
    "Tray15",
    "Tray16",
    "Tray17",
    "Tray18",
    "Tray19",
    "Tray20",
    "Roll1",
    "Roll2",
    "Roll3",
    "Roll4",
    "Roll5",
    "Roll6",
    "Roll7",
    "Roll8",
    "Roll9",
    "Roll10",
  };
  static const char * const orientation_enum[] =
  {					/* Human-readable orientation values */
    "Portrait",
    "Landscape",
    "ReversePortrait",
    "ReverseLandscape"
  };
  static const char * const print_quality_enum[] =
  {					/* Human-readable print quality values */
    "Default",
    "",
    "",
    "Draft",
    "Normal",
    "High"
  };
  static const char * const color_space_enum[] =
  {					/* Human-readable color space values */
    "W",	/* 0 */
    "Rgb",	/* 1 */
    "",		/* 2 */
    "Black",	/* 3 */
    "",		/* 4 */
    "",		/* 5 */
    "Cmyk",	/* 6 */
    "",		/* 7 */
    "",		/* 8 */
    "",		/* 9 */
    "",		/* 10 */
    "",		/* 11 */
    "",		/* 12 */
    "",		/* 13 */
    "",		/* 14 */
    "",		/* 15 */
    "",		/* 16 */
    "",		/* 17 */
    "Sgray",	/* 18 */
    "Srgb",	/* 19 */
    "AdobeRgb",	/* 20 */
    "",		/* 21 */
    "",		/* 22 */
    "",		/* 23 */
    "",		/* 24 */
    "",		/* 25 */
    "",		/* 26 */
    "",		/* 27 */
    "",		/* 28 */
    "",		/* 29 */
    "",		/* 30 */
    "",		/* 31 */
    "",		/* 32 */
    "",		/* 33 */
    "",		/* 34 */
    "",		/* 35 */
    "",		/* 36 */
    "",		/* 37 */
    "",		/* 38 */
    "",		/* 39 */
    "",		/* 40 */
    "",		/* 41 */
    "",		/* 42 */
    "",		/* 43 */
    "",		/* 44 */
    "",		/* 45 */
    "",		/* 46 */
    "",		/* 47 */
    "Device1",	/* 48 */
    "Device2",	/* 49 */
    "Device3",	/* 50 */
    "Device4",	/* 51 */
    "Device5",	/* 52 */
    "Device6",	/* 53 */
    "Device7",	/* 54 */
    "Device8",	/* 55 */
    "Device9",	/* 56 */
    "Device10",	/* 57 */
    "Device11",	/* 58 */
    "Device12",	/* 59 */
    "Device13",	/* 60 */
    "Device14",	/* 61 */
    "Device15"	/* 62 */
  };


  if (cupsFileRead(fp, (char *)header, sizeof(cups_page_header2_t)) != sizeof(cups_page_header2_t))
    return (0);

  if (syncword == CUPS_RASTER_REVSYNCv2)
  {
   /*
    * Swap bytes for integer values in page header...
    */

    unsigned	len,			/* Looping var */
		*s,			/* Current word */
		temp;			/* Temporary copy */

    for (len = 81, s = &(header->AdvanceDistance); len > 0; len --, s ++)
    {
      temp = *s;
      *s   = ((temp & 0xff) << 24) |
	     ((temp & 0xff00) << 8) |
	     ((temp & 0xff0000) >> 8) |
	     ((temp & 0xff000000) >> 24);
    }
  }

  if (memcmp(header->MediaClass, "PwgRaster", 10))
  {
    fputs("ERROR: PwgRaster value in header is incorrect.\n", stderr);
    Errors ++;
    return (0);
  }

  if (header->AdvanceDistance != 0 || header->AdvanceMedia != 0 || header->Collate != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[256-267] area.\n", stderr);
    Warnings ++;
  }

  fprintf(stderr, "DEBUG: MediaColor=\"%s\"\n", header->MediaColor);
  fprintf(stderr, "DEBUG: MediaType=\"%s\"\n", header->MediaType);
  fprintf(stderr, "DEBUG: PrintContentOptimize=\"%s\"\n", header->OutputType);

  if (header->CutMedia > CUPS_CUT_PAGE)
  {
    fprintf(stderr, "INFO: Bad CutMedia value %u.\n", header->CutMedia);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: CutMedia=%u (%s)\n", header->CutMedia, when_enum[header->CutMedia]);

  if (header->Duplex > 1)
  {
    fprintf(stderr, "INFO: Bad Duplex value %u.\n", header->Duplex);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: Duplex=%u (%s)\n", header->Duplex, header->Duplex ? "true" : "false");

  if (header->HWResolution[0] == 0 || header->HWResolution[1] == 0)
  {
    fprintf(stderr, "INFO: Bad HWResolution value [%u %u].\n", header->HWResolution[0], header->HWResolution[1]);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: HWResolution=[%u %u]\n", header->HWResolution[0], header->HWResolution[1]);

  if (header->ImagingBoundingBox[0] != 0 || header->ImagingBoundingBox[1] != 0 || header->ImagingBoundingBox[2] != 0 || header->ImagingBoundingBox[3] != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[284-299] area.\n", stderr);
    Warnings ++;
  }

  if (header->InsertSheet > 1)
  {
    fprintf(stderr, "INFO: Bad InsertSheet value %u.\n", header->InsertSheet);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: InsertSheet=%u (%s)\n", header->InsertSheet, header->InsertSheet ? "true" : "false");


  if (header->Jog > CUPS_JOG_SET)
  {
    fprintf(stderr, "INFO: Bad Jog value %u.\n", header->Jog);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: Jog=%u (%s)\n", header->Jog, when_enum[header->Jog]);

  if (header->LeadingEdge > CUPS_EDGE_RIGHT)
  {
    fprintf(stderr, "INFO: Bad LeadingEdge value %u.\n", header->LeadingEdge);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: LeadingEdge=%u (%s)\n", header->LeadingEdge, header->LeadingEdge ? "LongEdgeFirst" : "ShortEdgeFirst");

  if (header->Margins[0] != 0 || header->Margins[1] != 0 || header->ManualFeed != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[312-323] area.\n", stderr);
    Warnings ++;
  }

  if (header->MediaPosition >= (sizeof(media_position_enum) / sizeof(media_position_enum[0])))
  {
    fprintf(stderr, "INFO: Bad MediaPosition value %u.\n", header->MediaPosition);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: MediaPosition=%u (%s)\n", header->MediaPosition, media_position_enum[header->MediaPosition]);

  fprintf(stderr, "DEBUG: MediaWeight=%u\n", header->MediaWeight);

  if (header->MirrorPrint != 0 || header->NegativePrint != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[332-339] area.\n", stderr);
    Warnings ++;
  }

  fprintf(stderr, "DEBUG: NumCopies=%u\n", header->NumCopies);

  if (header->Orientation > CUPS_ORIENT_270)
  {
    fprintf(stderr, "INFO: Bad Orientation value %u.\n", header->Orientation);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: Orientation=%u (%s)\n", header->Orientation, orientation_enum[header->Orientation]);

  if (header->OutputFaceUp != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[348-351] area.\n", stderr);
    Warnings ++;
  }

  if (header->PageSize[0] == 0 || header->PageSize[1] == 0)
  {
    fprintf(stderr, "INFO: Bad PageSize value [%u %u].\n", header->PageSize[0], header->PageSize[1]);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: PageSize=[%u %u]\n", header->PageSize[0], header->PageSize[1]);

  if (header->Separations != 0 || header->TraySwitch != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[360-367] area.\n", stderr);
    Warnings ++;
  }

  if (header->Tumble > 1)
  {
    fprintf(stderr, "INFO: Bad Tumble value %u.\n", header->Tumble);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: Tumble=%u (%s)\n", header->Tumble, header->Tumble ? "true" : "false");

  if (header->cupsWidth == 0 || header->cupsHeight == 0)
  {
    fprintf(stderr, "ERROR: Bad Width x Height value %u x %u.\n", header->cupsWidth, header->cupsHeight);
    Errors ++;
    return (0);
  }
  else
    fprintf(stderr, "DEBUG: Width x Height=%u x %u\n", header->cupsWidth, header->cupsHeight);

  if (header->cupsMediaType != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[380-383] area.\n", stderr);
    Warnings ++;
  }

  switch (header->cupsColorSpace)
  {
    case CUPS_CSPACE_W :
    case CUPS_CSPACE_K :
    case CUPS_CSPACE_SW :
        num_colors = 1;
        break;

    case CUPS_CSPACE_RGB :
    case CUPS_CSPACE_SRGB :
    case CUPS_CSPACE_ADOBERGB :
        num_colors = 3;
        break;

    case CUPS_CSPACE_CMYK :
        num_colors = 4;
        break;

    case CUPS_CSPACE_DEVICE1 :
    case CUPS_CSPACE_DEVICE2 :
    case CUPS_CSPACE_DEVICE3 :
    case CUPS_CSPACE_DEVICE4 :
    case CUPS_CSPACE_DEVICE5 :
    case CUPS_CSPACE_DEVICE6 :
    case CUPS_CSPACE_DEVICE7 :
    case CUPS_CSPACE_DEVICE8 :
    case CUPS_CSPACE_DEVICE9 :
    case CUPS_CSPACE_DEVICEA :
    case CUPS_CSPACE_DEVICEB :
    case CUPS_CSPACE_DEVICEC :
    case CUPS_CSPACE_DEVICED :
    case CUPS_CSPACE_DEVICEE :
    case CUPS_CSPACE_DEVICEF :
        num_colors = header->cupsColorSpace - CUPS_CSPACE_DEVICE1 + 1;
        break;

    default :
        fprintf(stderr, "ERROR: Bad ColorSpace value %u.\n", header->cupsColorSpace);
        Errors ++;
        return (0);
  }

  fprintf(stderr, "DEBUG: ColorSpace=%u (%s)\n", header->cupsColorSpace, color_space_enum[header->cupsColorSpace]);

  if (header->cupsColorOrder != CUPS_ORDER_CHUNKED)
  {
    fprintf(stderr, "ERROR: Bad ColorOrder value %u.\n", header->cupsColorOrder);
    Errors ++;
    return (0);
  }
  else
    fputs("DEBUG: ColorOrder=0 (Chunky)\n", stderr);

  if (header->cupsNumColors != num_colors)
  {
    fprintf(stderr, "INFO: Bad NumColors value %u.\n", header->cupsNumColors);
    Warnings ++;
    header->cupsNumColors = (unsigned)num_colors;
  }

  switch (header->cupsBitsPerColor)
  {
    case 1 :
        if (num_colors != 1)
        {
	  fprintf(stderr, "ERROR: Bad BitsPerColor value %u.\n", header->cupsBitsPerColor);
	  Errors ++;
	  return (0);
        }
        else
          fputs("DEBUG: BitsPerColor=1\n", stderr);
	break;

    case 8 :
    case 16 :
        fprintf(stderr, "DEBUG: BitsPerColor=%u\n", header->cupsBitsPerColor);
        break;

    default :
        fprintf(stderr, "ERROR: Bad BitsPerColor value %u.\n", header->cupsBitsPerColor);
        Errors ++;
        return (0);
  }

  if (header->cupsBitsPerPixel != (num_colors * header->cupsBitsPerColor))
  {
    fprintf(stderr, "ERROR: Bad BitsPerPixel value %u.\n", header->cupsBitsPerPixel);
    Errors ++;
    return (0);
  }
  else
    fprintf(stderr, "DEBUG: BitsPerPixel=%u\n", header->cupsBitsPerPixel);

  bytes_per_line = (header->cupsWidth * header->cupsBitsPerPixel + 7) / 8;

  if (header->cupsBytesPerLine != bytes_per_line)
  {
    fprintf(stderr, "ERROR: Bad BytesPerLine value %u.\n", header->cupsBytesPerLine);
    Errors ++;
    return (0);
  }
  else
    fprintf(stderr, "DEBUG: BytesPerLine=%u\n", header->cupsBytesPerLine);

  if (header->cupsCompression != 0 || header->cupsRowCount != 0 || header->cupsRowFeed != 0 || header->cupsRowStep != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[404-419] area.\n", stderr);
    Warnings ++;
  }

  if (header->cupsBorderlessScalingFactor != 0 || header->cupsPageSize[0] != 0 || header->cupsPageSize[1] != 0 || header->cupsImagingBBox[0] != 0 || header->cupsImagingBBox[1] != 0 || header->cupsImagingBBox[2] != 0 || header->cupsImagingBBox[3] != 0)
  {
    fputs("INFO: Non-zero values present in Reserved[424-451] area.\n", stderr);
    Warnings ++;
  }

  fprintf(stderr, "DEBUG: TotalPageCount=%u\n", header->cupsInteger[0]);

  i = (int)header->cupsInteger[1];

  if (i != 1 && i != -1)
  {
    fprintf(stderr, "INFO: Bad CrossFeedTransform value %d.\n", i);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: CrossFeedTransform=%d\n", i);

  i = (int)header->cupsInteger[2];

  if (i != 1 && i != -1)
  {
    fprintf(stderr, "INFO: Bad FeedTransform value %d.\n", i);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: FeedTransform=%d\n", i);

  fprintf(stderr, "DEBUG: ImageBoxLeft=%u\n", header->cupsInteger[3]);
  fprintf(stderr, "DEBUG: ImageBoxTop=%u\n", header->cupsInteger[4]);
  fprintf(stderr, "DEBUG: ImageBoxRight=%u\n", header->cupsInteger[5]);
  fprintf(stderr, "DEBUG: ImageBoxBottom=%u\n", header->cupsInteger[6]);
  fprintf(stderr, "DEBUG: AlternatePrimary=0x%08x\n", header->cupsInteger[7]);

  if (header->cupsInteger[8] == 1 || header->cupsInteger[8] == 2 || header->cupsInteger[8] > 5)
  {
    fprintf(stderr, "INFO: Bad PrintQuality value %u.\n", header->cupsInteger[8]);
    Warnings ++;
  }
  else
    fprintf(stderr, "DEBUG: PrintQuality=%u (%s)\n", header->cupsInteger[8], print_quality_enum[header->cupsInteger[8]]);

  for (i = 9; i < 14; i ++)
  {
    if (header->cupsInteger[i] != 0)
      break;
  }

  if (i < 14)
  {
    fputs("INFO: Non-zero values present in Reserved[488-507] area.\n", stderr);
    Warnings ++;
  }

  fprintf(stderr, "DEBUG: VendorIdentifier=%u\n", header->cupsInteger[14]);
  fprintf(stderr, "DEBUG: VendorLength=%u\n", header->cupsInteger[15]);

  if (header->cupsMarkerType[0] != 0 || memcmp(header->cupsMarkerType, header->cupsMarkerType + 1, sizeof(header->cupsMarkerType) - 1))
  {
    fputs("INFO: Non-zero values present in Reserved[1604-1667] area.\n", stderr);
    Warnings ++;
  }

  fprintf(stderr, "DEBUG: RenderingIntent=\"%s\"\n", header->cupsRenderingIntent);
  fprintf(stderr, "DEBUG: PageSizeName=\"%s\"\n", header->cupsPageSizeName);

  return (1);
}


/*
 * 'read_raster_image()' - Read the raster page image...
 */

static int				/* O - 1 on success, 0 on error */
read_raster_image(
    cups_file_t         *fp,		/* I - File to read from */
    cups_page_header2_t *header,	/* I - Page header */
    unsigned            page)		/* I - Page number */
{
  int		ch;			/* Character from file */
  unsigned	height,			/* Height (lines) remaining */
		width,			/* Width (columns/pixels) remaining */
		repeat,			/* Line repeat value */
		count,			/* Number of columns/pixels */
		bytes,			/* Bytes in sequence */
		bpp;			/* Bytes per pixel */
  unsigned char	*buffer,		/* Line buffer */
		*bufptr,		/* Pointer into buffer */
		white;			/* White color */
  int		blank = 1,		/* Is the page blank? */
		color = header->cupsNumColors > 4;
					/* Is the page in color? */


  fprintf(stderr, "DEBUG: Reading page %u.\n", page);

  buffer = malloc(header->cupsBytesPerLine);

  if (header->cupsColorSpace == CUPS_CSPACE_W || header->cupsColorSpace == CUPS_CSPACE_RGB || header->cupsColorSpace == CUPS_CSPACE_SW || header->cupsColorSpace == CUPS_CSPACE_SRGB || header->cupsColorSpace == CUPS_CSPACE_ADOBERGB)
    white = 0xff;
  else
    white = 0x00;

  if (header->cupsBitsPerPixel == 1)
    bpp = 1;
  else
    bpp = header->cupsBitsPerPixel / 8;

  for (height = header->cupsHeight; height > 0; height -= repeat)
  {
   /*
    * Read the line repeat code...
    */

    if ((ch = cupsFileGetChar(fp)) == EOF)
    {
      fprintf(stderr, "ERROR: Early end-of-file at line %u.\n", header->cupsHeight - height + 1);
      Errors ++;
      free(buffer);
      return (0);
    }

    repeat = (unsigned)ch + 1;

    if (repeat > height)
    {
      fprintf(stderr, "ERROR: Bad repeat count %u at line %u.\n", repeat, header->cupsHeight - height + 1);
      Errors ++;
      free(buffer);
      return (0);
    }

    for (width = header->cupsWidth; width > 0; width -= count)
    {
     /*
      * Read the packbits code...
      */

      if ((ch = cupsFileGetChar(fp)) == EOF)
      {
	fprintf(stderr, "ERROR: Early end-of-file at line %u, column %u.\n", header->cupsHeight - height + 1, header->cupsWidth - width + 1);
	Errors ++;
	free(buffer);
	return (0);
      }

      if (ch == 0x80)
      {
       /*
        * Clear to end of line...
	*/

        break;
      }
      else if (ch & 0x80)
      {
       /*
        * Literal sequwnce...
        */

        count = 257 - (unsigned)ch;
        bytes = count * bpp;
      }
      else
      {
       /*
        * Repeat sequence...
        */

        count = (unsigned)ch + 1;
        bytes = bpp;
      }

      if (header->cupsBitsPerPixel == 1)
      {
	count *= 8;
	if (count > width && (count - width) < 8)
	  count = width;
      }

      if (count > width)
      {
	fprintf(stderr, "ERROR: Bad literal count %u at line %u, column %u.\n", count, header->cupsHeight - height + 1, header->cupsWidth - width + 1);
	Errors ++;
	free(buffer);
	return (0);
      }

     /*
      * Read a pixel fragment...
      */

      if (cupsFileRead(fp, (char *)buffer, bytes) < bytes)
      {
	fprintf(stderr, "ERROR: Early end-of-file at line %u, column %u.\n", header->cupsHeight - height + 1, header->cupsWidth - width + 1);
	Errors ++;
	free(buffer);
	return (0);
      }

     /*
      * Check for blank/color...
      */

      if (blank && (buffer[0] != white || memcmp(buffer, buffer + 1, bytes - 1)))
        blank = 0;

      if (!color && header->cupsNumColors > 1)
      {
        if (header->cupsNumColors == 3)
        {
         /*
          * Scan RGB data...
          */

          if (header->cupsBitsPerColor == 8)
          {
            for (bufptr = buffer; bytes > 0; bufptr += 3, bytes -= 3)
            {
              if (bufptr[0] != bufptr[1] || bufptr[1] != bufptr[2])
              {
                color = 1;
                break;
	      }
            }
          }
          else
          {
            for (bufptr = buffer; bytes > 0; bufptr += 6, bytes -= 6)
            {
              if (bufptr[0] != bufptr[2] || bufptr[1] != bufptr[3] || bufptr[2] != bufptr[4] || bufptr[3] != bufptr[5])
              {
                color = 1;
                break;
	      }
            }
          }
        }
        else
        {
         /*
          * Scan CMYK data...
          */

          if (header->cupsBitsPerColor == 8)
          {
            for (bufptr = buffer; bytes > 0; bufptr += 4, bytes -= 4)
            {
              if (bufptr[0] != bufptr[1] || bufptr[1] != bufptr[2])
              {
                color = 1;
                break;
	      }
            }
          }
          else
          {
            for (bufptr = buffer; bytes > 0; bufptr += 8, bytes -= 8)
            {
              if (bufptr[0] != bufptr[2] || bufptr[1] != bufptr[3] || bufptr[2] != bufptr[4] || bufptr[3] != bufptr[5])
              {
                color = 1;
                break;
	      }
            }
          }
	}
      }
    }
  }

  fprintf(stderr, "DEBUG: %s-sided %s\n", header->Duplex ? "two" : "one", blank ? "blank" : color ? "full-color" : "monochrome");

  if (header->Duplex)
  {
    if (blank)
      ImpressionsTwoSided.blank ++;
    else if (color)
      ImpressionsTwoSided.full_color ++;
    else
      ImpressionsTwoSided.monochrome ++;
  }
  else
  {
    if (blank)
      Impressions.blank ++;
    else if (color)
      Impressions.full_color ++;
    else
      Impressions.monochrome ++;
  }

  if (color)
    Pages.full_color ++;
  else
    Pages.monochrome ++;

  free(buffer);

  return (1);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(int status)			/* I - Exit status */
{
  puts("Usage: ippdoclint [options] filename");
  puts("Options:");
  puts("  --help              Show program usage.");
  puts("  --version           Show program version.");
  puts("  -i content-type     Set MIME media type for file.");
  puts("  -o name=value       Set print options.");
  puts("  -v                  Be verbose.");

  exit(status);
}

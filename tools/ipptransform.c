/*
 * ipptransform utility for converting PDF and JPEG files to raster data or HP PCL.
 *
 * Copyright 2016 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#include <cups/cups.h>
#include <cups/raster.h>
#include <cups/array-private.h>
#include <cups/string-private.h>

#ifdef __APPLE__
#  include <ApplicationServices/ApplicationServices.h>
#endif /* __APPLE__ */

#include "threshold64.h"


/*
 * Local types...
 */

typedef ssize_t (*xform_write_cb_t)(void *, const unsigned char *, size_t);

typedef struct xform_raster_s xform_raster_t;

struct xform_raster_s
{
  int			num_options;	/* Number of job options */
  cups_option_t		*options;	/* Job options */
  cups_page_header2_t	header;		/* Page header */
  unsigned char		*band_buffer;	/* Band buffer */
  unsigned		band_height;	/* Band height */

  /* Set by start_job callback */
  cups_raster_t		*ras;		/* Raster stream */

  /* Set by start_page callback */
  unsigned		left, top, right, bottom;
					/* Image (print) box with origin at top left */
  unsigned		out_blanks;	/* Blank lines */
  size_t		out_length;	/* Output buffer size */
  unsigned char		*out_buffer;	/* Output (bit) buffer */

  /* Callbacks */
  void			(*end_job)(xform_raster_t *, xform_write_cb_t, void *);
  void			(*end_page)(xform_raster_t *, unsigned, xform_write_cb_t, void *);
  void			(*start_job)(xform_raster_t *, xform_write_cb_t, void *);
  void			(*start_page)(xform_raster_t *, unsigned, xform_write_cb_t, void *);
  void			(*write_line)(xform_raster_t *, unsigned, const unsigned char *, xform_write_cb_t, void *);
};


/*
 * Local functions...
 */

static int	load_env_options(cups_option_t **options);
static void	pcl_end_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_end_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	pcl_init(xform_raster_t *ras);
static void	pcl_printf(xform_write_cb_t cb, void *ctx, const char *format, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
static void	pcl_start_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_start_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	pcl_write_line(xform_raster_t *ras, unsigned y, const unsigned char *line, xform_write_cb_t cb, void *ctx);
static void	raster_end_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_end_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	raster_init(xform_raster_t *ras);
static void	raster_start_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_start_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	raster_write_line(xform_raster_t *ras, unsigned y, const unsigned char *line, xform_write_cb_t cb, void *ctx);
static void	usage(int status) __attribute__((noreturn));
static ssize_t	write_fd(int *fd, const unsigned char *buffer, size_t bytes);
static int	xform_jpeg(const char *filename, const char *format, const char *resolutions, const char *types, int num_options, cups_option_t *options, xform_write_cb_t cb, void *ctx);
static int	xform_pdf(const char *filename, const char *format, const char *resolutions, const char *types, const char *sheet_back, int num_options, cups_option_t *options, xform_write_cb_t cb, void *ctx);
static int	xform_setup(xform_raster_t *ras, const char *format, const char *resolutions, const char *types, int color, unsigned pages, int num_options, cups_option_t *options);


/*
 * 'main()' - Main entry for transform utility.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  const char	*filename = NULL,	/* File to transform */
		*content_type,		/* Source content type */
		*device_uri,		/* Destination URI */
		*output_type,		/* Destination content type */
		*resolutions,		/* pwg-raster-document-resolution-supported */
		*sheet_back,		/* pwg-raster-document-sheet-back */
		*types,			/* pwg-raster-document-type-supported */
		*opt;			/* Option character */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  int		fd = 1;			/* Output file/socket */


 /*
  * Process the command-line...
  */

  num_options  = load_env_options(&options);
  content_type = getenv("CONTENT_TYPE");
  device_uri   = getenv("DEVICE_URI");
  output_type  = getenv("OUTPUT_TYPE");
  resolutions  = getenv("PWG_RASTER_DOCUMENT_RESOLUTION_SUPPORTED");
  sheet_back   = getenv("PWG_RASTER_DOCUMENT_SHEET_BACK");
  types        = getenv("PWG_RASTER_DOCUMENT_TYPE_SUPPORTED");

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
	  case 'd' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      device_uri = argv[i];
	      break;
	  case 'i' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      content_type = argv[i];
	      break;
	  case 'm' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      output_type = argv[i];
	      break;
	  case 'o' :
	      i ++;
	      if (i >= argc)
	        usage(1);

	      num_options = cupsParseOptions(argv[i], num_options, &options);
	      break;
	  case 'r' : /* pwg-raster-document-resolution-supported values */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      resolutions = argv[i];
	      break;
	  case 's' : /* pwg-raster-document-sheet-back value */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      sheet_back = argv[i];
	      break;
	  case 't' : /* pwg-raster-document-type-supported values */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      types = argv[i];
	      break;
	  default :
	      printf("ipptransform: Unknown option '-%c'.\n", *opt);
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
    }
  }

  if (!content_type)
  {
    printf("Unknown format for \"%s\", please specify with '-i' option.\n", filename);
    usage(1);
  }
  else if (strcmp(content_type, "application/pdf") && strcmp(content_type, "image/jpeg"))
  {
    printf("Unsupported format \"%s\" for \"%s\".\n", content_type, filename);
    usage(1);
  }

  if (!output_type)
  {
    puts("Unknown output format, please specify with '-m' option.");
    usage(1);
  }
  else if (strcmp(output_type, "application/vnd.hp-pcl") && strcmp(output_type, "image/pwg-raster"))
  {
    printf("Unsupported output format \"%s\".\n", output_type);
    usage(1);
  }

  if (!resolutions)
    resolutions = "300dpi";
  if (!sheet_back)
    sheet_back = "normal";
  if (!types)
    types = "sgray_8";

 /*
  * Do transform...
  */

  if (!strcmp(content_type, "application/pdf"))
    return (xform_pdf(filename, output_type, resolutions, types, sheet_back, num_options, options, (xform_write_cb_t)write_fd, &fd));
  else
    return (xform_jpeg(filename, output_type, resolutions, types, num_options, options, (xform_write_cb_t)write_fd, &fd));

  return (0);
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

      if (*envptr == '_')
        *nameptr++ = '-';
      else
        *nameptr++ = (char)_cups_tolower(*envptr);
    }

    *nameptr = '\0';
    if (*envptr == '=')
      envptr ++;

    num_options = cupsAddOption(name, envptr, num_options, options);
  }

  return (num_options);
}


/*
 * 'pcl_end_job()' - End a PCL "job".
 */

static void
pcl_end_job(xform_raster_t   *ras,	/* I - Raster information */
            xform_write_cb_t cb,	/* I - Write callback */
            void             *ctx)	/* I - Write context */
{
  (void)ras;

 /*
  * Send a PCL reset sequence.
  */

  (*cb)(ctx, (const unsigned char *)"\033E", 2);
}


/*
 * 'pcl_end_page()' - End of PCL page.
 */

static void
pcl_end_page(xform_raster_t   *ras,	/* I - Raster information */
	     unsigned         page,	/* I - Current page */
             xform_write_cb_t cb,	/* I - Write callback */
             void             *ctx)	/* I - Write context */
{
 /*
  * End graphics...
  */

  (*cb)(ctx, (const unsigned char *)"\033*r0B", 5);

 /*
  * Formfeed as needed...
  */

  if (!(ras->header.Duplex && (page & 1)))
    (*cb)(ctx, (const unsigned char *)"\014", 1);

 /*
  * Free the output buffer...
  */

  free(ras->out_buffer);
  ras->out_buffer = NULL;
}


/*
 * 'pcl_init()' - Initialize callbacks for PCL output.
 */

static void
pcl_init(xform_raster_t *ras)		/* I - Raster information */
{
  ras->end_job    = pcl_end_job;
  ras->end_page   = pcl_end_page;
  ras->start_job  = pcl_start_job;
  ras->start_page = pcl_start_page;
  ras->write_line = pcl_write_line;
}


/*
 * 'pcl_printf()' - Write a formatted string.
 */

static void
pcl_printf(xform_write_cb_t cb,		/* I - Write callback */
           void             *ctx,	/* I - Write context */
	   const char       *format,	/* I - Printf-style format string */
	   ...)				/* I - Additional arguments as needed */
{
  va_list	ap;			/* Argument pointer */
  char		buffer[8192];		/* Buffer */


  va_start(ap, format);
  vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  (*cb)(ctx, (const unsigned char *)buffer, strlen(buffer));
}


/*
 * 'pcl_start_job()' - Start a PCL "job".
 */

static void
pcl_start_job(xform_raster_t   *ras,	/* I - Raster information */
              xform_write_cb_t cb,	/* I - Write callback */
              void             *ctx)	/* I - Write context */
{
  (void)ras;

 /*
  * Send a PCL reset sequence.
  */

  (*cb)(ctx, (const unsigned char *)"\033E", 2);
}


/*
 * 'pcl_start_page()' - Start a PCL page.
 */

static void
pcl_start_page(xform_raster_t   *ras,	/* I - Raster information */
               unsigned         page,	/* I - Current page */
               xform_write_cb_t cb,	/* I - Write callback */
               void             *ctx)	/* I - Write context */
{
 /*
  * Setup margins to be 1/2" top and bottom and 1/4" (or .135" for A4) on the
  * left and right.
  */

  ras->top    = ras->header.HWResolution[1] / 2;
  ras->bottom = ras->header.cupsHeight - ras->header.HWResolution[1] / 2 - 1;

  if (ras->header.PageSize[1] == 842)
  {
    ras->left  = (ras->header.cupsWidth - 8 * ras->header.HWResolution[0]) / 2;
    ras->right = ras->left + 8 * ras->header.HWResolution[0] - 1;
  }
  else
  {
    ras->left  = ras->header.HWResolution[0] / 4;
    ras->right = ras->header.cupsWidth - ras->header.HWResolution[0] / 4 - 1;
  }

  if (!ras->header.Duplex || (page & 1))
  {
   /*
    * Set the media size...
    */

    pcl_printf(cb, ctx, "\033&l6D\033&k12H");/* Set 6 LPI, 10 CPI */
    pcl_printf(cb, ctx, "\033&l0O");	/* Set portrait orientation */

    switch (ras->header.PageSize[1])
    {
      case 540 : /* Monarch Envelope */
          pcl_printf(cb, ctx, "\033&l80A");
	  break;

      case 595 : /* A5 */
          pcl_printf(cb, ctx, "\033&l25A");
	  break;

      case 624 : /* DL Envelope */
          pcl_printf(cb, ctx, "\033&l90A");
	  break;

      case 649 : /* C5 Envelope */
          pcl_printf(cb, ctx, "\033&l91A");
	  break;

      case 684 : /* COM-10 Envelope */
          pcl_printf(cb, ctx, "\033&l81A");
	  break;

      case 709 : /* B5 Envelope */
          pcl_printf(cb, ctx, "\033&l100A");
	  break;

      case 756 : /* Executive */
          pcl_printf(cb, ctx, "\033&l1A");
	  break;

      case 792 : /* Letter */
          pcl_printf(cb, ctx, "\033&l2A");
	  break;

      case 842 : /* A4 */
          pcl_printf(cb, ctx, "\033&l26A");
	  break;

      case 1008 : /* Legal */
          pcl_printf(cb, ctx, "\033&l3A");
	  break;

      case 1191 : /* A3 */
          pcl_printf(cb, ctx, "\033&l27A");
	  break;

      case 1224 : /* Tabloid */
          pcl_printf(cb, ctx, "\033&l6A");
	  break;
    }

   /*
    * Set length and top margin, turn off perforation skip...
    */

    pcl_printf(cb, ctx, "\033&l%dP\033&l0E\033&l0L", ras->header.PageSize[1] / 12);

    if (ras->header.Duplex)
    {
      int mode = ras->header.Duplex ? 1 + ras->header.Tumble != 0 : 0;

      pcl_printf(cb, ctx, "\033&l%dS", mode);	/* Set duplex mode */
    }
  }

 /*
  * Set graphics mode...
  */

  pcl_printf(cb, ctx, "\033*t%uR", ras->header.HWResolution[0]);
					/* Set resolution */
  pcl_printf(cb, ctx, "\033*r%uS", ras->right - ras->left);
					/* Set width */
  pcl_printf(cb, ctx, "\033*r%uT", ras->bottom - ras->top);
					/* Set height */
  pcl_printf(cb, ctx, "\033&a0H");	/* Set horizontal position */
  pcl_printf(cb, ctx, "\033&a0V");	/* Set top-of-page */

  pcl_printf(cb, ctx, "\033*r1A");	/* Start graphics */

 /*
  * Allocate the output buffer...
  */

  ras->out_blanks = 0;
  ras->out_length = (ras->right - ras->left + 8) / 8;
  ras->out_buffer = malloc(ras->out_length);
}


/*
 * 'pcl_write_line()' - Write a line of raster data.
 */

static void
pcl_write_line(
    xform_raster_t      *ras,		/* I - Raster information */
    unsigned            y,		/* I - Line number */
    const unsigned char *line,		/* I - Pixels on line */
    xform_write_cb_t    cb,		/* I - Write callback */
    void                *ctx)		/* I - Write context */
{
  int		x;			/* Column number */
  unsigned char	bit,			/* Current bit */
		byte,			/* Current byte */
		*bufptr;		/* Pointer into buffer */


  if (line[0] == 255 && !memcmp(line, line + 1, ras->header.cupsWidth - 1))
  {
   /*
    * Blank line...
    */

    ras->out_blanks ++;
    return;
  }

  y &= 63;

  for (x = 0, bit = 128, byte = 0, bufptr = ras->out_buffer; x < ras->header.cupsWidth; x ++, line ++)
  {
    if (*line <= threshold[x & 63][y])
      byte |= bit;

    if (bit == 1)
    {
      *bufptr++ = byte;
      byte      = 0;
      bit       = 128;
    }
    else
      bit >>= 1;
  }

  if (bit != 128)
    *bufptr++ = byte;

  if (ras->out_blanks > 0)
  {
    pcl_printf(cb, ctx, "\033*b%dY", ras->out_blanks);
    ras->out_blanks = 0;
  }

  pcl_printf(cb, ctx, "\033*r%dW", (int)(bufptr - ras->out_buffer));
  (*cb)(ctx, ras->out_buffer, (size_t)(bufptr - ras->out_buffer));
}


/*
 * 'raster_end_job()' - End a raster "job".
 */

static void
raster_end_job(xform_raster_t   *ras,	/* I - Raster information */
	       xform_write_cb_t cb,	/* I - Write callback */
	       void             *ctx)	/* I - Write context */
{
  (void)cb;
  (void)ctx;

  cupsRasterClose(ras->ras);
}


/*
 * 'raster_end_page()' - End of raster page.
 */

static void
raster_end_page(xform_raster_t   *ras,	/* I - Raster information */
	        unsigned         page,	/* I - Current page */
		xform_write_cb_t cb,	/* I - Write callback */
		void             *ctx)	/* I - Write context */
{
  (void)ras;
  (void)page;
  (void)cb;
  (void)ctx;
}


/*
 * 'raster_init()' - Initialize callbacks for raster output.
 */

static void
raster_init(xform_raster_t *ras)	/* I - Raster information */
{
  ras->end_job    = raster_end_job;
  ras->end_page   = raster_end_page;
  ras->start_job  = raster_start_job;
  ras->start_page = raster_start_page;
  ras->write_line = raster_write_line;
}


/*
 * 'raster_start_job()' - Start a raster "job".
 */

static void
raster_start_job(xform_raster_t   *ras,	/* I - Raster information */
		 xform_write_cb_t cb,	/* I - Write callback */
		 void             *ctx)	/* I - Write context */
{
  ras->ras = cupsRasterOpenIO((cups_raster_iocb_t)cb, ctx, CUPS_RASTER_WRITE_PWG);
}


/*
 * 'raster_start_page()' - Start a raster page.
 */

static void
raster_start_page(xform_raster_t   *ras,/* I - Raster information */
		  unsigned         page,/* I - Current page */
		  xform_write_cb_t cb,	/* I - Write callback */
		  void             *ctx)/* I - Write context */
{
  (void)page;
  (void)cb;
  (void)ctx;

  cupsRasterWriteHeader2(ras->ras, &ras->header);
}


/*
 * 'raster_write_line()' - Write a line of raster data.
 */

static void
raster_write_line(
    xform_raster_t      *ras,		/* I - Raster information */
    unsigned            y,		/* I - Line number */
    const unsigned char *line,		/* I - Pixels on line */
    xform_write_cb_t    cb,		/* I - Write callback */
    void                *ctx)		/* I - Write context */
{
  (void)y;
  (void)cb;
  (void)ctx;

  cupsRasterWritePixels(ras->ras, (unsigned char *)line, ras->header.cupsBytesPerLine);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(int status)			/* I - Exit status */
{
  puts("Usage: ipptransform filename [options]");

  exit(status);
}


/*
 * 'write_fd()' - Write to a file/socket.
 */

static ssize_t				/* O - Number of bytes written or -1 on error */
write_fd(int                 *fd,	/* I - File descriptor */
         const unsigned char *buffer,	/* I - Buffer */
         size_t              bytes)	/* I - Number of bytes to write */
{
  ssize_t	temp,			/* Temporary byte count */
		total = 0;		/* Total bytes written */


  while (bytes > 0)
  {
    if ((temp = write(*fd, buffer, bytes)) < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      else
        return (-1);
    }

    total  += temp;
    bytes  -= (size_t)temp;
    buffer += temp;
  }

  return (total);
}


/*
 * 'xform_jpeg()' - Transform a JPEG image for printing.
 */

static int				/* O - 0 on success, 1 on error */
xform_jpeg(const char       *filename,	/* I - File to transform */
           const char       *format,	/* I - Output format (MIME media type) */
           const char       *resolutions,/* I - Supported resolutions */
	   const char       *types,	/* I - Supported types */
           int              num_options,/* I - Number of options */
           cups_option_t    *options,	/* I - Options */
           xform_write_cb_t cb,		/* I - Write callback */
           void             *ctx)	/* I - Write context */
{
  // TODO: Implement me
  (void)filename;
  (void)format;
  (void)resolutions;
  (void)types;
  (void)num_options;
  (void)options;
  (void)cb;
  (void)ctx;

  return (1);
}


/*
 * 'xform_pdf()' - Transform a PDF file for printing.
 */

static int				/* O - 0 on success, 1 on error */
xform_pdf(const char       *filename,	/* I - File to transform */
          const char       *format,	/* I - Output format (MIME media type) */
          const char       *resolutions,/* I - Supported resolutions */
	  const char       *types,	/* I - Supported types */
	  const char       *sheet_back,	/* I - Back side transform */
          int              num_options,	/* I - Number of options */
          cups_option_t    *options,	/* I - Options */
          xform_write_cb_t cb,		/* I - Write callback */
          void             *ctx)	/* I - Write context */
{
  CFURLRef		url;		/* CFURL object for PDF filename */
  CGPDFDocumentRef	document= NULL;	/* Input document */
  xform_raster_t	ras;		/* Raster info */
  unsigned		pages = 1;	/* Number of pages */
  int			color = 1;	/* Does the PDF have color? */
  const char		*page_ranges;	/* "page-ranges" option */


  (void)sheet_back; /* TODO: Support back side transforms */

 /*
  * Open the PDF file...
  */

  if ((url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)filename, (CFIndex)strlen(filename), false)) == NULL)
    return (1);

  document = CGPDFDocumentCreateWithURL(url);
  CFRelease(url);

  if (!document)
    return (1);

  if (CGPDFDocumentIsEncrypted(document))
  {
   /*
    * Only support encrypted PDFs with a blank password...
    */

    if (!CGPDFDocumentUnlockWithPassword(document, ""))
    {
      fputs("ERROR: Document is encrypted and cannot be unlocked.\n", stderr);
      CGPDFDocumentRelease(document);
      return (1);
    }
  }

  if (!CGPDFDocumentAllowsPrinting(document))
  {
    fputs("ERROR: Document does not allow printing.\n", stderr);
    CGPDFDocumentRelease(document);
    return (1);
  }

  pages = (unsigned)CGPDFDocumentGetNumberOfPages(document);

 /*
  * Setup the raster context...
  */

  if (xform_setup(&ras, format, resolutions, types, color, pages, num_options, options))
  {
    CGPDFDocumentRelease(document);
    return (1);
  }


  CGPDFDocumentRelease(document);
  return (0);
}


/*
 * 'xform_setup()' - Setup a raster context for printing.
 */

static int				/* O - 0 on success, -1 on failure */
xform_setup(xform_raster_t *ras,	/* I - Raster information */
            const char     *format,	/* I - Output format (MIME media type) */
	    const char     *resolutions,/* I - Supported resolutions */
	    const char     *types,	/* I - Supported types */
	    int            color,	/* I - Document contains color? */
            unsigned       pages,	/* I - Number of pages */
            int            num_options,	/* I - Number of options */
            cups_option_t  *options)	/* I - Options */
{
  const char	*media,			/* "media" option */
		*media_col;		/* "media-col" option */
  pwg_media_t	*pwg_media;		/* PWG media value */
  int		width = -1,		/* Page width in PWG units */
		length = -1;		/* Page length in PWG units */
  const char	*print_quality,		/* "print-quality" option */
		*printer_resolution;	/* "printer-resolution" option */
  cups_array_t	*res_array,		/* Resolutions in array */
		*type_array;		/* Types in array */


 /*
  * Initialize raster information...
  */

  memset(ras, 0, sizeof(xform_raster_t));

  ras->num_options = num_options;
  ras->options     = options;

  if (!strcmp(format, "application/vnd.hp-pcl"))
  {
   /*
    * HP PCL...
    */

    ras->end_job    = pcl_end_job;
    ras->end_page   = pcl_end_page;
    ras->start_job  = pcl_start_job;
    ras->start_page = pcl_start_page;
    ras->write_line = pcl_write_line;
  }
  else
  {
   /*
    * PWG Raster...
    */

    ras->end_job    = raster_end_job;
    ras->end_page   = raster_end_page;
    ras->start_job  = raster_start_job;
    ras->start_page = raster_start_page;
    ras->write_line = raster_write_line;
  }

 /*
  * Figure out the media size...
  */

  if ((media = cupsGetOption("media", num_options, options)) != NULL)
  {
    if ((pwg_media = pwgMediaForPWG(media)) == NULL)
      pwg_media = pwgMediaForLegacy(media);

    if (pwg_media)
    {
      width  = pwg_media->width;
      length = pwg_media->length;
    }
    else
    {
      fprintf(stderr, "ERROR: Unknown \"media\" value '%s'.\n", media);
      return (-1);
    }
  }
  else if ((media_col = cupsGetOption("media-col", num_options, options)) != NULL)
  {
    int			num_cols;	/* Number of collection values */
    cups_option_t	*cols;		/* Collection values */
    const char		*media_size_name,
			*media_size;	/* Collection attributes */

    num_cols = cupsParseOptions(media_col, 0, &cols);
    if ((media_size_name = cupsGetOption("media-size-name", num_cols, cols)) != NULL)
    {
      if ((pwg_media = pwgMediaForPWG(media_size_name)) != NULL)
      {
	width  = pwg_media->width;
	length = pwg_media->length;
      }
      else
      {
	fprintf(stderr, "ERROR: Unknown \"media-size-name\" value '%s'.\n", media_size_name);
	cupsFreeOptions(num_cols, cols);
	return (-1);
      }
    }
    else if ((media_size = cupsGetOption("media-size", num_cols, cols)) != NULL)
    {
      int		num_sizes;	/* Number of collection values */
      cups_option_t	*sizes;		/* Collection values */
      const char	*x_dim,		/* Collection attributes */
			*y_dim;

      num_sizes = cupsParseOptions(media_size, 0, &sizes);
      if ((x_dim = cupsGetOption("x-dimension", num_sizes, sizes)) != NULL && (y_dim = cupsGetOption("y-dimension", num_sizes, sizes)) != NULL)
      {
        width  = atoi(x_dim);
	length = atoi(y_dim);
      }
      else
      {
        fprintf(stderr, "ERROR: Bad \"media-size\" value '%s'.\n", media_size);
	cupsFreeOptions(num_sizes, sizes);
	cupsFreeOptions(num_cols, cols);
	return (-1);
      }

      cupsFreeOptions(num_sizes, sizes);
    }

    cupsFreeOptions(num_cols, cols);
  }

  if (width <= 0 || length <= 0)
  {
   /*
    * Use default size...
    */

    const char	*media_default = getenv("PRINTER_MEDIA_DEFAULT");
				/* "media-default" value */

    if (!media_default)
      media_default = "na_letter_8.5x11in";

    if ((pwg_media = pwgMediaForPWG(media_default)) != NULL)
    {
      width  = pwg_media->width;
      length = pwg_media->length;
    }
    else
    {
      fprintf(stderr, "ERROR: Unknown \"media-default\" value '%s'.\n", media_default);
      return (-1);
    }
  }

 /*
  * Figure out the proper resolution, etc.
  */

  res_array = _cupsArrayNewStrings(resolutions, ',');

  if ((printer_resolution = cupsGetOption("printer-resolution", num_options, options)) != NULL && !cupsArrayFind(resolutions, printer_resolution))
  {
    fprintf(stderr, "INFO: Unsupported \"printer-resolution\" value '%s'.\n", printer_resolution);
    printer_resolution = NULL;
  }

  if (!printer_resolution)
  {
    if ((print_quality = cupsGetOption("print-quality", num_options, options)) != NULL)
    {
      switch (atoi(print_quality))
      {
        case IPP_QUALITY_DRAFT :
	    printer_resolution = cupsArrayIndex(res_array, 0);
	    break;

        case IPP_QUALITY_NORMAL :
	    printer_resolution = cupsArrayIndex(res_array, cupsArrayCount(res_array) / 2);
	    break;

        case IPP_QUALITY_HIGH :
	    printer_resolution = cupsArrayIndex(res_array, cupsArrayCount(res_array) - 1);
	    break;

	default :
	    fprintf(stderr, "INFO: Unsupported \"print-quality\" value '%s'.\n", print_quality);
	    break;
    }
  }

  if (!printer_resolution)
    printer_resolution = cupsArrayIndex(res_array, cupsArrayCount(res_array) / 2);

  if (!printer_resolution)
  {
    fputs("ERROR: No \"printer-resolution\" or \"pwg-raster-document-resolution-supported\" value.\n", stderr);
    return (-1);
  }

 /*
  * Parse the "printer-resolution" value...
  */

  if (sscanf(printer_resolution, "%ux%udpi", ras->header.HWResolution + 0, ras->header.HWResolution + 1) != 2)
  {
    if (sscanf(printer_resolution, "%udpi", ras->header.HWResolution + 0) == 1)
    {
      ras->header.HWResolution[1] = ras->header.HWResolution[0];
    }
    else
    {
      fprintf(stderr, "ERROR: Bad resolution value '%s'.\n", printer_resolution);
      return (-1);
    }
  }

 /*
  * Now figure out the color space to use...
  */

  return (0);
}

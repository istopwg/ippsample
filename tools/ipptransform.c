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
static int	xform_setup(xform_raster_t *ras, const char *format, unsigned xdpi, unsigned ydpi, const char *type, unsigned pages, int num_options, cups_option_t *options);


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
		*opt;			/* Option character */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */


 /*
  * Process the command-line...
  */

  num_options  = load_env_options(&options);
  content_type = getenv("CONTENT_TYPE");
  device_uri   = getenv("DEVICE_URI");
  output_type  = getenv("OUTPUT_TYPE");

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

static int				/* O - 1 on success, 0 on error */
xform_jpeg(const char       *filename,	/* I - File to transform */
           const char       *format,	/* I - Output format (MIME media type) */
           const char       *resolutions,/* I - Supported resolutions */
	   const char       *types,	/* I - Supported types */
           int              num_options,/* I - Number of options */
           cups_option_t    *options,	/* I - Options */
           xform_write_cb_t cb,		/* I - Write callback */
           void             *ctx)	/* I - Write context */
{
}


/*
 * 'xform_pdf()' - Transform a PDF file for printing.
 */

static int				/* O - 1 on success, 0 on error */
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
}


/*
 * 'xform_setup()' - Setup a raster context for printing.
 */

static int				/* O - 1 on success, 0 on failure */
xform_setup(xform_raster_t *ras,	/* I - Raster information */
            const char     *format,	/* I - Output format (MIME media type) */
	    unsigned       xdpi,	/* I - Horizontal resolution in DPI */
	    unsigned       ydpi,	/* I - Vertical resolution in DPI */
	    const char     *type,	/* I - Color space and bit depth */
            unsigned       pages,	/* I - Number of pages */
            int            num_options,	/* I - Number of options */
            cups_option_t  *options)	/* I - Options */
{
}

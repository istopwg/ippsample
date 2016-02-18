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


/*
 * Local types...
 */

typedef ssize_t (*xform_write_cb_t)(void *, const void *, size_t);

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
static ssize_t	write_fd(int *ctx, const void *buffer, size_t bytes);
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

  (*cb)(ctx, "\033E", 2);
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

  (*cb)(ctx, "\033*r0B", 5);

 /*
  * Formfeed as needed...
  */

  if (!(ras->header.Duplex && (page & 1)))
    (*cb)(ctx, "\014", 1);
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

  (*cb)(ctx, buffer, strlen(buffer));
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

  (*cb)(ctx, "\033E", 2);
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
  (void)ras;
  (void)y;
  (void)line;
  (void)cb;
  (void)ctx;

#if 0
	/*
	 * 'CompressData()' - Compress a line of graphics.
	 */

	void
	CompressData(unsigned char *line,	/* I - Data to compress */
		     unsigned      length,	/* I - Number of bytes */
		     unsigned      plane,	/* I - Color plane */
		     unsigned      type)	/* I - Type of compression */
	{
	  unsigned char	*line_ptr,		/* Current byte pointer */
			*line_end,		/* End-of-line byte pointer */
			*comp_ptr,		/* Pointer into compression buffer */
			*start;			/* Start of compression sequence */
	  unsigned	count;			/* Count of bytes for output */


	  switch (type)
	  {
	    default :
	       /*
		* Do no compression...
		*/

		line_ptr = line;
		line_end = line + length;
		break;

	    case 1 :
	       /*
		* Do run-length encoding...
		*/

		line_end = line + length;
		for (line_ptr = line, comp_ptr = CompBuffer;
		     line_ptr < line_end;
		     comp_ptr += 2, line_ptr += count)
		{
		  for (count = 1;
		       (line_ptr + count) < line_end &&
			   line_ptr[0] == line_ptr[count] &&
			   count < 256;
		       count ++);

		  comp_ptr[0] = (unsigned char)(count - 1);
		  comp_ptr[1] = line_ptr[0];
		}

		line_ptr = CompBuffer;
		line_end = comp_ptr;
		break;

	    case 2 :
	       /*
		* Do TIFF pack-bits encoding...
		*/

		line_ptr = line;
		line_end = line + length;
		comp_ptr = CompBuffer;

		while (line_ptr < line_end)
		{
		  if ((line_ptr + 1) >= line_end)
		  {
		   /*
		    * Single byte on the end...
		    */

		    *comp_ptr++ = 0x00;
		    *comp_ptr++ = *line_ptr++;
		  }
		  else if (line_ptr[0] == line_ptr[1])
		  {
		   /*
		    * Repeated sequence...
		    */

		    line_ptr ++;
		    count = 2;

		    while (line_ptr < (line_end - 1) &&
			   line_ptr[0] == line_ptr[1] &&
			   count < 127)
		    {
		      line_ptr ++;
		      count ++;
		    }

		    *comp_ptr++ = (unsigned char)(257 - count);
		    *comp_ptr++ = *line_ptr++;
		  }
		  else
		  {
		   /*
		    * Non-repeated sequence...
		    */

		    start    = line_ptr;
		    line_ptr ++;
		    count    = 1;

		    while (line_ptr < (line_end - 1) &&
			   line_ptr[0] != line_ptr[1] &&
			   count < 127)
		    {
		      line_ptr ++;
		      count ++;
		    }

		    *comp_ptr++ = (unsigned char)(count - 1);

		    memcpy(comp_ptr, start, count);
		    comp_ptr += count;
		  }
		}

		line_ptr = CompBuffer;
		line_end = comp_ptr;
		break;
	  }

	 /*
	  * Set the length of the data and write a raster plane...
	  */

	  printf("\033*b%d%c", (int)(line_end - line_ptr), plane);
	  fwrite(line_ptr, (size_t)(line_end - line_ptr), 1, stdout);
	}


	/*
	 * 'OutputLine()' - Output a line of graphics.
	 */

	void
	OutputLine(cups_page_header2_t *header)	/* I - Page header */
	{
	  unsigned	plane,			/* Current plane */
			bytes,			/* Bytes to write */
			count;			/* Bytes to convert */
	  unsigned char	bit,			/* Current plane data */
			bit0,			/* Current low bit data */
			bit1,			/* Current high bit data */
			*plane_ptr,		/* Pointer into Planes */
			*bit_ptr;		/* Pointer into BitBuffer */


	 /*
	  * Output whitespace as needed...
	  */

	  if (Feed > 0)
	  {
	    printf("\033*b%dY", Feed);
	    Feed = 0;
	  }

	 /*
	  * Write bitmap data as needed...
	  */

	  bytes = (header->cupsWidth + 7) / 8;

	  for (plane = 0; plane < NumPlanes; plane ++)
	    if (ColorBits == 1)
	    {
	     /*
	      * Send bits as-is...
	      */

	      CompressData(Planes[plane], bytes, plane < (NumPlanes - 1) ? 'V' : 'W',
			   header->cupsCompression);
	    }
	    else
	    {
	     /*
	      * Separate low and high bit data into separate buffers.
	      */

	      for (count = header->cupsBytesPerLine / NumPlanes,
		       plane_ptr = Planes[plane], bit_ptr = BitBuffer;
		   count > 0;
		   count -= 2, plane_ptr += 2, bit_ptr ++)
	      {
		bit = plane_ptr[0];

		bit0 = (unsigned char)(((bit & 64) << 1) | ((bit & 16) << 2) | ((bit & 4) << 3) | ((bit & 1) << 4));
		bit1 = (unsigned char)((bit & 128) | ((bit & 32) << 1) | ((bit & 8) << 2) | ((bit & 2) << 3));

		if (count > 1)
		{
		  bit = plane_ptr[1];

		  bit0 |= (unsigned char)((bit & 1) | ((bit & 4) >> 1) | ((bit & 16) >> 2) | ((bit & 64) >> 3));
		  bit1 |= (unsigned char)(((bit & 2) >> 1) | ((bit & 8) >> 2) | ((bit & 32) >> 3) | ((bit & 128) >> 4));
		}

		bit_ptr[0]     = bit0;
		bit_ptr[bytes] = bit1;
	      }

	     /*
	      * Send low and high bits...
	      */

	      CompressData(BitBuffer, bytes, 'V', header->cupsCompression);
	      CompressData(BitBuffer + bytes, bytes, plane < (NumPlanes - 1) ? 'V' : 'W',
			   header->cupsCompression);
	    }

	  fflush(stdout);
	}
#endif // 0
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
  ras->ras = cupsRasterOpen(CUPS_RASTER_
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
  (void)cb;
  (void)ctx;

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
  (void)cb;
  (void)ctx;

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
write_fd(int        *ctx,		/* I - File descriptor */
         const void *buffer,		/* I - Buffer */
         size_t     bytes)		/* I - Number of bytes to write */
{
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

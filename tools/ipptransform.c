//
// Utility for converting PDF and JPEG files to raster data or HP PCL.
//
// Copyright © 2016-2022 by the IEEE-ISTO Printer Working Group.
// Copyright © 2016-2019 by Apple Inc.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include "ipp-options.h"
#include <cups/raster.h>
#include <cups/thread.h>
#include <pdfio.h>
#include <pdfio-content.h>

#ifdef HAVE_COREGRAPHICS
#  include <CoreGraphics/CoreGraphics.h>
#  include <ImageIO/ImageIO.h>

extern void CGContextSetCTM(CGContextRef c, CGAffineTransform m);
#endif // HAVE_COREGRAPHICS

#include "dither.h"


// Macros...
#define XFORM_MATCH(a,b)	(abs(a-b) <= 100)


// Constants...
#define XFORM_MAX_LAYOUT		16
#define XFORM_MAX_PAGES		10000
#define XFORM_MAX_RASTER	16777216

#define XFORM_TEXT_SIZE		10.0	// Point size of plain text output
#define XFORM_TEXT_HEIGHT	12.0	// Point height of plain text output
#define XFORM_TEXT_WIDTH	0.6	// Width of Courier characters

#define XFORM_RED_MASK		0x000000ff
#define XFORM_GREEN_MASK	0x0000ff00
#define XFORM_BLUE_MASK		0x00ff0000
#define XFORM_RGB_MASK		(XFORM_RED_MASK | XFORM_GREEN_MASK |  XFORM_BLUE_MASK)
#define XFORM_BG_MASK		(XFORM_BLUE_MASK | XFORM_GREEN_MASK)
#define XFORM_RG_MASK		(XFORM_RED_MASK | XFORM_GREEN_MASK)


// Local types...
typedef ssize_t (*xform_write_cb_t)(void *, const void *, size_t);
					// Write callback

typedef struct xform_document_s		// Document information
{
  const char	*filename,		// Document filename
		*format;		// Document format
  char		tempfile[1024];		// Temporary PDF file, if any
  const char	*pdf_filename;		// PDF filename
  pdfio_file_t	*pdf;			// PDF file for document
  int		first_page,		// First page number in document
		last_page,		// Last page number in document
		num_pages;		// Number of pages to print in document
} xform_document_t;

typedef struct xform_page_s		// Output page
{
  pdfio_file_t	*pdf;			// Output PDF file
  size_t	layout;			// Current layout cell
  pdfio_obj_t	*input[XFORM_MAX_LAYOUT];
					// Input page objects
  pdfio_dict_t	*pagedict;		// Page dictionary
  pdfio_dict_t	*resdict;		// Resource dictionary
  pdfio_dict_t	*resmap[XFORM_MAX_LAYOUT];
					// Resource name map
  pdfio_dict_t	*restype;		// Current resource type dictionary
  pdfio_stream_t *output;		// Output page stream
} xform_page_t;

typedef struct xform_prepare_s		// Preparation data
{
  ipp_options_t	*options;		// Print options
  cups_array_t	*errors;		// Error messages
  int		document,		// Current document
		num_inpages;		// Number of input pages
  pdfio_file_t	*pdf;			// Output PDF file
  pdfio_rect_t	media;			// Default media box
  pdfio_rect_t	crop;			// Default crop box
  size_t	num_outpages;		// Number of output pages
  xform_page_t	outpages[XFORM_MAX_PAGES];
					// Output pages
  size_t	num_layout;		// Number of layout rectangles
  pdfio_rect_t	layout[XFORM_MAX_LAYOUT];
					// Layout rectangles
  bool		use_duplex_xform;	// Use the back side transform matrix?
  pdfio_matrix_t duplex_xform;		// Back side transform matrix
} xform_prepare_t;

typedef struct xform_raster_s xform_raster_t;
					// Raster context

struct xform_raster_s			// Raster context
{
  const char		*format;	// Output format
  cups_page_header_t	header;		// Page header
  cups_page_header_t	back_header;	// Page header for back side
  cups_page_header_t	sep_header;	// Separator page header
  bool			borderless;	// Borderless media?
  unsigned char		*band_buffer;	// Band buffer
  unsigned		band_height;	// Band height
  unsigned		band_bpp;	// Bytes per pixel in band

  // Set by start_job callback
  cups_raster_t		*ras;		// Raster stream

  // Set by start_page callback
  unsigned		left, top, right, bottom;
					// Image (print) box with origin at top left
  unsigned		out_blanks;	// Blank lines
  size_t		out_length;	// Output buffer size
  unsigned char		*out_buffer;	// Output (bit) buffer
  unsigned char		*comp_buffer;	// Compression buffer

  unsigned char		dither[64][64];	// Dither array

  // Callbacks
  void			(*end_job)(xform_raster_t *, xform_write_cb_t, void *);
  void			(*end_page)(xform_raster_t *, unsigned, xform_write_cb_t, void *);
  void			(*start_job)(xform_raster_t *, xform_write_cb_t, void *);
  void			(*start_page)(xform_raster_t *, unsigned, xform_write_cb_t, void *);
  void			(*write_line)(xform_raster_t *, unsigned, const unsigned char *, xform_write_cb_t, void *);
};


// Local globals...
static int	Verbosity = 0;		// Log level


// Local functions...
static bool	convert_image(xform_prepare_t *p, xform_document_t *d, int document);
static bool	convert_raster(xform_prepare_t *p, xform_document_t *d, int document);
static bool	convert_text(xform_prepare_t *p, xform_document_t *d, int document);
static void	copy_page(xform_prepare_t *p, xform_page_t *outpage, size_t layout);
static bool	generate_job_error_sheet(xform_prepare_t *p);
static bool	generate_job_sheets(xform_prepare_t *p);
static void	*monitor_ipp(const char *device_uri);
#ifdef HAVE_COREGRAPHICS
static void	pack_rgba(unsigned char *row, size_t num_pixels);
static void	pack_rgba16(unsigned char *row, size_t num_pixels);
#endif // HAVE_COREGRAPHICS
static bool	page_dict_cb(pdfio_dict_t *dict, const char *key, xform_page_t *outpage);
static void	pcl_end_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_end_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	pcl_init(xform_raster_t *ras);
static void	pcl_printf(xform_write_cb_t cb, void *ctx, const char *format, ...) _CUPS_FORMAT(3, 4);
static void	pcl_start_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	pcl_start_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	pcl_write_line(xform_raster_t *ras, unsigned y, const unsigned char *line, xform_write_cb_t cb, void *ctx);
static void	pdfio_end_page(xform_prepare_t *p, pdfio_stream_t *st);
static bool	pdfio_error_cb(pdfio_file_t *pdf, const char *message, void *cb_data);
static const char *pdfio_password_cb(void *cb_data, const char *filename);
static pdfio_stream_t *pdfio_start_page(xform_prepare_t *p, pdfio_dict_t *dict);
static bool	prepare_documents(size_t num_documents, xform_document_t *documents, ipp_options_t *options, const char *sheet_back, char *outfile, size_t outsize, unsigned *outpages);
static void	prepare_log(xform_prepare_t *p, bool error, const char *message, ...);
static void	prepare_number_up(xform_prepare_t *p);
static void	prepare_pages(xform_prepare_t *p, size_t num_documents, xform_document_t *documents);
static void	raster_end_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_end_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	raster_init(xform_raster_t *ras);
static void	raster_start_job(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static void	raster_start_page(xform_raster_t *ras, unsigned page, xform_write_cb_t cb, void *ctx);
static void	raster_write_line(xform_raster_t *ras, unsigned y, const unsigned char *line, xform_write_cb_t cb, void *ctx);
static bool	resource_dict_cb(pdfio_dict_t *dict, const char *key, xform_page_t *outpage);
static void	size_to_rect(cups_size_t *size, pdfio_rect_t *media, pdfio_rect_t *crop);
static void	usage(int status) _CUPS_NORETURN;
static ssize_t	write_fd(int *fd, const unsigned char *buffer, size_t bytes);
static bool	xform_document(const char *filename, unsigned pages, ipp_options_t *options, const char *outformat, const char *resolutions, const char *sheet_back, const char *types, xform_write_cb_t cb, void *ctx);
static bool	xform_separator(xform_raster_t *ras, xform_write_cb_t cb, void *ctx);
static bool	xform_setup(xform_raster_t *ras, ipp_options_t *options, const char *outformat, const char *resolutions, const char *types, const char *sheet_back, bool color, unsigned pages);


//
// 'main()' - Main entry for transform utility.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line args
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  const char	*device_uri,		// Destination URI
		*output_type,		// Destination content type
		*resolutions,		// pwg-raster-document-resolution-supported
		*sheet_back,		// pwg-raster-document-sheet-back
		*types,			// pwg-raster-document-type-supported
		*opt;			// Option character
  size_t	num_files = 0;		// Number of files
  xform_document_t files[1000];		// Files to convert
  size_t	num_options = 0;	// Number of options
  cups_option_t	*options = NULL;	// Options
  ipp_options_t	*ipp_options;		// IPP options
  char		pdf_file[1024];		// Temporary PDF filename
  unsigned	pdf_pages;		// Number of pages in PDF file
  int		fd = 1;			// Output file/socket
  http_t	*http = NULL;		// Output HTTP connection
  void		*write_ptr = &fd;	// Pointer to file/socket/HTTP connection
  char		resource[1024],		// URI resource path
		temp[128];		// Temporary string
  xform_write_cb_t write_cb = (xform_write_cb_t)write_fd;
					// Write callback
  int		status = 0;		// Exit status
  cups_thread_t monitor = 0;		// Monitoring thread ID


  // Process the command-line...
  memset(files, 0, sizeof(files));

  device_uri   = getenv("DEVICE_URI");
  output_type  = getenv("OUTPUT_TYPE");
  resolutions  = getenv("IPP_PWG_RASTER_DOCUMENT_RESOLUTION_SUPPORTED");
  sheet_back   = getenv("IPP_PWG_RASTER_DOCUMENT_SHEET_BACK");
  types        = getenv("IPP_PWG_RASTER_DOCUMENT_TYPE_SUPPORTED");

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
        puts(IPPSAMPLE_VERSION);
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
	  case 'd' :
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-d'.\n", stderr);
	        usage(1);
	      }

	      device_uri = argv[i];
	      break;

	  case 'f' :
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-f'.\n", stderr);
	        usage(1);
	      }

	      if (!freopen(argv[i], "w", stdout))
	      {
	        fprintf(stderr, "ERROR: Unable to open '%s': %s\n", argv[i], strerror(errno));
	        return (1);
	      }
	      break;

	  case 'i' :
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-i'.\n", stderr);
	        usage(1);
	      }

	      if (num_files < (sizeof(files) / sizeof(files[0])))
	      {
	        files[num_files].format = argv[i];
	      }
	      else
	      {
	        fputs("ERROR: Too many files.\n", stderr);
	        return (1);
	      }
	      break;

	  case 'm' :
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-m'.\n", stderr);
	        usage(1);
	      }

	      output_type = argv[i];
	      break;

	  case 'o' :
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-o'.\n", stderr);
	        usage(1);
	      }

	      num_options = cupsParseOptions(argv[i], num_options, &options);
	      break;

	  case 'r' : // pwg-raster-document-resolution-supported values
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-r'.\n", stderr);
	        usage(1);
	      }

	      resolutions = argv[i];
	      break;

	  case 's' : // pwg-raster-document-sheet-back value
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-s'.\n", stderr);
	        usage(1);
	      }

	      sheet_back = argv[i];
	      break;

	  case 't' : // pwg-raster-document-type-supported values
	      i ++;
	      if (i >= argc)
	      {
	        fputs("ERROR: Missing argument after '-t'.\n", stderr);
	        usage(1);
	      }

	      types = argv[i];
	      break;

	  case 'v' : // Be verbose...
	      Verbosity ++;
	      break;

	  default :
	      fprintf(stderr, "ERROR: Unknown option '-%c'.\n", *opt);
	      usage(1);
	      break;
	}
      }
    }
    else if (num_files < (sizeof(files) / sizeof(files[0])))
    {
      if (!files[num_files].format)
      {
        if (num_files == 0)
        {
          files[num_files].format = getenv("CONTENT_TYPE");
	}
	else
	{
	  snprintf(temp, sizeof(temp), "CONTENT_TYPE%u", (unsigned)num_files + 1);
          files[num_files].format = getenv(temp);
	}
      }

      if (!files[num_files].format)
      {
	if ((opt = strrchr(argv[i], '.')) != NULL)
	{
	  if (!strcmp(opt, ".jpg") || !strcmp(opt, ".jpeg"))
	    files[num_files].format = "image/jpeg";
	  else if (!strcmp(opt, ".pcl"))
	    files[num_files].format = "application/vnd.hp-PCL";
	  else if (!strcmp(opt, ".pdf"))
	    files[num_files].format = "application/pdf";
	  else if (!strcmp(opt, ".png"))
	    files[num_files].format = "image/png";
	  else if (!strcmp(opt, ".pxl"))
	    files[num_files].format = "application/vnd.hp-PCLXL";
	  else if (!strcmp(opt, ".pwg"))
	    files[num_files].format = "image/pwg-raster";
	  else if (!strcmp(opt, ".txt") || !strcmp(opt, ".md"))
	    files[num_files].format = "text/plain";
	  else if (!strcmp(opt, ".urf"))
	    files[num_files].format = "image/urf";
	}
      }

      if (!files[num_files].format)
      {
	fprintf(stderr, "ERROR: Unknown format for '%s', please specify with '-i' option.\n", argv[i]);
	usage(1);
      }
      else if (strcmp(files[num_files].format, "application/pdf") && strcmp(files[num_files].format, "image/jpeg") && strcmp(files[num_files].format, "image/png") && strcmp(files[num_files].format, "text/plain"))
      {
	fprintf(stderr, "ERROR: Unsupported format '%s' for '%s'.\n", files[num_files].format, argv[i]);
	usage(1);
      }

      files[num_files ++].filename = argv[i];
    }
    else
    {
      fputs("ERROR: Too many files.\n", stderr);
      return (1);
    }
  }

  // Check that we have everything we need...
  if (num_files == 0)
    usage(1);

  if (!output_type)
  {
    fputs("ERROR: Unknown output format, please specify with '-m' option.\n", stderr);
    usage(1);
  }
  else if (strcmp(output_type, "application/pdf") && strcmp(output_type, "application/vnd.hp-pcl") && strcmp(output_type, "image/pwg-raster") && strcmp(output_type, "image/urf"))
  {
    fprintf(stderr, "ERROR: Unsupported output format '%s'.\n", output_type);
    usage(1);
  }

  // Prepare a PDF file for printing...
  ipp_options = ippOptionsNew(num_options, options);

  if (!prepare_documents(num_files, files, ipp_options, sheet_back, pdf_file, sizeof(pdf_file), &pdf_pages))
  {
    // Unable to prepare documents, exit...
    ippOptionsDelete(ipp_options);
    return (1);
  }

  // If the device URI is specified, open the connection...
  if (device_uri)
  {
    char	scheme[32],		// URI scheme
		userpass[256],		// URI user:pass
		host[256],		// URI host
		service[32];		// Service port
    int		port;			// URI port number
    http_addrlist_t *list;		// Address list for socket

    if (httpSeparateURI(HTTP_URI_CODING_ALL, device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
    {
      fprintf(stderr, "ERROR: Invalid device URI '%s'.\n", device_uri);
      usage(1);
    }

    if (strcmp(scheme, "socket") && strcmp(scheme, "ipp") && strcmp(scheme, "ipps"))
    {
      fprintf(stderr, "ERROR: Unsupported device URI scheme '%s'.\n", scheme);
      usage(1);
    }

    snprintf(service, sizeof(service), "%d", port);
    if ((list = httpAddrGetList(host, AF_UNSPEC, service)) == NULL)
    {
      fprintf(stderr, "ERROR: Unable to lookup device URI host '%s': %s\n", host, cupsLastErrorString());
      return (1);
    }

    if (!strcmp(scheme, "socket"))
    {
      // AppSocket connection...
      if (!httpAddrConnect(list, &fd, 30000, NULL))
      {
	fprintf(stderr, "ERROR: Unable to connect to '%s' on port %d: %s\n", host, port, cupsLastErrorString());
	return (1);
      }
    }
    else
    {
      // IPP/IPPS connection...
      http_encryption_t encryption;	// Encryption mode
      ipp_t		*request,	// IPP request
			*response;	// IPP response
      ipp_attribute_t	*attr;		// operations-supported
      int		create_job = 0;	// Support for Create-Job/Send-Document?
      int		gzip;		// gzip compression supported?
      const char	*job_name;	// Title of job
      const char	*media;		// Value of "media" option
      const char	*sides;		// Value of "sides" option
      static const char * const pattrs[] =
      {					// requested-attributes
        "compression-supported",
        "operations-supported"
      };

      if (port == 443 || !strcmp(scheme, "ipps"))
        encryption = HTTP_ENCRYPTION_ALWAYS;
      else
        encryption = HTTP_ENCRYPTION_IF_REQUESTED;

      if ((http = httpConnect(host, port, list, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
      {
	fprintf(stderr, "ERROR: Unable to connect to '%s' on port %d: %s\n", host, port, cupsLastErrorString());
	return (1);
      }

      // See if it supports Create-Job + Send-Document...
      request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
      ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

      response = cupsDoRequest(http, request, resource);
      if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE)
      {
        fprintf(stderr, "ERROR: Unable to get printer capabilities: %s\n", cupsLastErrorString());
	return (1);
      }

      if ((attr = ippFindAttribute(response, "operations-supported", IPP_TAG_ENUM)) == NULL)
      {
        fputs("ERROR: Unable to get list of supported operations from printer.\n", stderr);
	return (1);
      }

      create_job = ippContainsInteger(attr, IPP_OP_CREATE_JOB) && ippContainsInteger(attr, IPP_OP_SEND_DOCUMENT);
      gzip       = ippContainsString(ippFindAttribute(response, "compression-supported", IPP_TAG_KEYWORD), "gzip");

      ippDelete(response);

      // Create the job and start printing...
      if ((job_name = getenv("IPP_JOB_NAME")) == NULL)
      {
	if ((job_name = strrchr(files[0].filename, '/')) != NULL)
	  job_name ++;
	else
	  job_name = files[0].filename;
      }

      if (create_job)
      {
        int	job_id;			// Job ID

        request = ippNewRequest(IPP_OP_CREATE_JOB);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, job_name);

        response = cupsDoRequest(http, request, resource);
        job_id   = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);

        ippDelete(response);

	if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE)
	{
	  fprintf(stderr, "ERROR: Unable to create print job: %s\n", cupsLastErrorString());
	  return (1);
	}
	else if (job_id <= 0)
	{
          fputs("ERROR: No job-id for created print job.\n", stderr);
	  return (1);
	}

        request = ippNewRequest(IPP_OP_SEND_DOCUMENT);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
	ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, output_type);
	if (gzip)
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, "gzip");
        ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", 1);
      }
      else
      {
        request = ippNewRequest(IPP_OP_PRINT_JOB);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, output_type);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, job_name);
	if (gzip)
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, "gzip");
      }

      if ((media = cupsGetOption("media", num_options, options)) != NULL)
        ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "media", NULL, media);

      if ((sides = cupsGetOption("sides", num_options, options)) != NULL)
        ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "sides", NULL, sides);

      if (cupsSendRequest(http, request, resource, 0) != HTTP_STATUS_CONTINUE)
      {
        fprintf(stderr, "ERROR: Unable to send print data: %s\n", cupsLastErrorString());
	return (1);
      }

      ippDelete(request);

      if (gzip)
        httpSetField(http, HTTP_FIELD_CONTENT_ENCODING, "gzip");

      write_cb  = (xform_write_cb_t)httpWrite;
      write_ptr = http;

      monitor = cupsThreadCreate((cups_thread_func_t)monitor_ipp, (void *)device_uri);
    }

    httpAddrFreeList(list);
  }

  if (strcmp(output_type, "application/pdf"))
  {
    // Do raster transform...
    if (!resolutions)
      resolutions = "300dpi";
    if (!sheet_back)
      sheet_back = "normal";
    if (!types)
      types = "sgray_8";

    if (!xform_document(pdf_file, pdf_pages, ipp_options, output_type, resolutions, sheet_back, types, write_cb, write_ptr))
      status = 1;
  }
  else
  {
    // Send PDF...
    cups_file_t	*fp;			// PDF file
    char	buffer[32768];		// Buffer
    ssize_t	bytes;			// Number of bytes

    if ((fp = cupsFileOpen(pdf_file, "r")) == NULL)
    {
      fprintf(stderr, "ERROR: Unable to open '%s': %s\n", pdf_file, strerror(errno));
      status = 1;
    }
    else
    {
      while ((bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
      {
        if ((write_cb)(write_ptr, buffer, (size_t)bytes) < 0)
        {
          fputs("ERROR: Unable to send data.\n", stderr);
          status = 1;
          break;
	}
      }

      cupsFileClose(fp);
    }
  }

  ippOptionsDelete(ipp_options);

  if (http)
  {
    ippDelete(cupsGetResponse(http, resource));

    if (cupsLastError() > IPP_STATUS_OK_EVENTS_COMPLETE)
    {
      fprintf(stderr, "ERROR: Unable to send print data: %s\n", cupsLastErrorString());
      status = 1;
    }

    httpClose(http);
  }
  else if (fd != 1)
  {
    close(fd);
  }

  if (monitor)
    cupsThreadCancel(monitor);

  return (status);
}


//
// 'convert_image()' - Convert an image to a PDF file.
//
// This function handles scaling and cropping the image to the output media
// size, as needed.
//

static bool				// O - `true` on success, `false` on failure
convert_image(
    xform_prepare_t  *p,		// I - Preparation data
    xform_document_t *d,		// I - Document
    int              document)		// I - Document number
{
  pdfio_file_t	*pdf;			// Temporary PDF file
  pdfio_obj_t	*image;			// Image object
  pdfio_dict_t	*dict;			// Page dictionary
  char		iname[32];		// Image object name
  pdfio_stream_t *st;			// Page stream
  double	x, y, w, h;		// Image rectangle
  double	cx, cy, cw, ch;		// Crop box rectangle
  double	iw, ih;			// Original image width/height
  int		irot;			// Image rotation
  double	ratio;			// Scaling factor for image
  ippopt_scaling_t scaling;		// Actual print-scaling to use


  // Create a temporary PDF file...
  if ((pdf = pdfioFileCreateTemporary(d->tempfile, sizeof(d->tempfile), "1.7", &p->media, &p->media, pdfio_error_cb, p)) == NULL)
  {
    return (false);
  }

  // Create an image object for the file...
  if ((image = pdfioFileCreateImageObjFromFile(pdf, d->filename, false)) == NULL)
  {
    prepare_log(p, true, "Input Document %d: Unable to open '%s' - %s", document, d->filename, strerror(errno));
    goto error;
  }

  // Figure out where to print the image...
  iw = pdfioImageGetWidth(image);
  ih = pdfioImageGetHeight(image);

  cx = p->crop.x1;
  cy = p->crop.y1;
  cw = p->crop.x2 - cx;
  ch = p->crop.y2 - cy;

  switch (p->options->image_orientation)
  {
    case IPP_ORIENT_PORTRAIT :
        irot = 0;
        break;
    case IPP_ORIENT_LANDSCAPE :
        irot = 1;
        break;
    case IPP_ORIENT_REVERSE_PORTRAIT :
        irot = 2;
        break;
    case IPP_ORIENT_REVERSE_LANDSCAPE :
        irot = 3;
        break;
    case IPP_ORIENT_NONE : // Auto
        if ((iw > ih && cw < ch) || (iw < ih && cw > ch))
          irot = 3;
	else
	  irot = 0;
	break;
  }

  if (p->options->print_scaling == IPPOPT_SCALING_AUTO)
  {
    if (irot & 1)
    {
      if (iw > ch || ih > cw)
      {
        if (cx == 0.0 && cy == 0.0)
          scaling = IPPOPT_SCALING_FIT;
	else
	  scaling = IPPOPT_SCALING_FILL;
      }
      else
      {
        scaling = IPPOPT_SCALING_NONE;
      }
    }
    else
    {
      if (iw > cw || ih > ch)
      {
        if (cx == 0.0 && cy == 0.0)
          scaling = IPPOPT_SCALING_FIT;
	else
	  scaling = IPPOPT_SCALING_FILL;
      }
      else
      {
        scaling = IPPOPT_SCALING_NONE;
      }
    }
  }
  else if (p->options->print_scaling == IPPOPT_SCALING_AUTO_FIT)
  {
    if (irot & 1)
    {
      if (iw > ch || ih > cw)
	scaling = IPPOPT_SCALING_FIT;
      else
        scaling = IPPOPT_SCALING_NONE;
    }
    else
    {
      if (iw > cw || ih > ch)
	scaling = IPPOPT_SCALING_FIT;
      else
        scaling = IPPOPT_SCALING_NONE;
    }
  }
  else
  {
    scaling = p->options->print_scaling;
  }

  if (scaling == IPPOPT_SCALING_NONE)
  {
    // No scaling...
    ratio = 1.0;
  }
  else
  {
    if (irot & 1)
      ratio = ch / iw;
    else
      ratio = cw / iw;

    if (scaling == IPPOPT_SCALING_FIT)
    {
      // Scale to fit...
      if (irot & 1)
      {
        if ((ih * ratio) > cw)
          ratio = cw / ih;
      }
      else
      {
        if ((ih * ratio) > ch)
          ratio = ch / ih;
      }
    }
    else
    {
      // Scale to fill...
      if (irot & 1)
      {
        if ((ih * ratio) < cw)
          ratio = cw / ih;
      }
      else
      {
        if ((ih * ratio) < ch)
          ratio = ch / ih;
      }
    }
  }

  if (irot & 1)
  {
    w = ih * ratio;
    h = iw * ratio;
  }
  else
  {
    w = iw * ratio;
    h = ih * ratio;
  }

  switch (p->options->x_image_position)
  {
    case IPPOPT_IMGPOS_NONE :
        x = cx;
        break;
    case IPPOPT_IMGPOS_CENTER :
        x = cx + (cw - w - 72.0 * p->options->x_side1_image_shift / 2540.0) / 2.0;
        break;
    case IPPOPT_IMGPOS_BOTTOM_LEFT :
        x = cx + 72.0 * p->options->x_side1_image_shift / 2540.0;
        break;
    case IPPOPT_IMGPOS_TOP_RIGHT :
        x = cx + cw - w - 72.0 * p->options->x_side1_image_shift / 2540.0;
        break;
  }

  switch (p->options->y_image_position)
  {
    case IPPOPT_IMGPOS_NONE :
        y = cy;
        break;
    case IPPOPT_IMGPOS_CENTER :
        y = cy + (ch - h - 72.0 * p->options->y_side1_image_shift / 2540.0) / 2.0;
        break;
    case IPPOPT_IMGPOS_BOTTOM_LEFT :
        y = cy + 72.0 * p->options->y_side1_image_shift / 2540.0;
        break;
    case IPPOPT_IMGPOS_TOP_RIGHT :
        y = cy + ch - h - 72.0 * p->options->y_side1_image_shift / 2540.0;
        break;
  }

  // Create a page dictionary for the image...
  if ((dict = pdfioDictCreate(pdf)) == NULL)
  {
    prepare_log(p, true, "Input Document %d: Unable to create page dictionary.", document);
    goto error;
  }

  snprintf(iname, sizeof(iname), "IM%d", document);
  pdfioPageDictAddImage(dict, iname, image);

  // Create the page...
  if ((st = pdfioFileCreatePage(pdf, dict)) == NULL)
  {
    prepare_log(p, true, "Input Document %d: Unable to create page object.", document);
    goto error;
  }

  // Draw the image, cropped...
  pdfioContentSave(st);

  pdfioContentPathRect(st, cx, cy, cw, ch);
  pdfioContentClip(st, false);
  pdfioContentPathEnd(st);

  switch (irot)
  {
    case 0 :
	pdfioContentDrawImage(st, iname, x, y, w, h);
	break;
    case 1 :
        pdfioStreamPrintf(st, "q 0 %g %g 0 %g %g cm /%s Do Q\n", -h, w, x, y + h, iname);
        break;
    case 2 :
        pdfioStreamPrintf(st, "q 0 %g %g 0 %g %g cm /%s Do Q\n", -w, -h, x + w, y + h, iname);
        break;
    case 3 :
        pdfioStreamPrintf(st, "q 0 %g %g 0 %g %g cm /%s Do Q\n", h, -w, x + w, y, iname);
        break;
  }

  pdfioContentRestore(st);

  // Close the page and file...
  pdfioStreamClose(st);
  pdfioFileClose(pdf);

  d->pdf_filename = d->tempfile;

  return (true);

  // If we get here something bad happened...
  error:

  pdfioFileClose(pdf);

  unlink(d->tempfile);
  d->tempfile[0] = '\0';

  return (false);
}


//
// 'convert_raster()' - Convert a PWG or Apple raster file to a PDF file.
//

static bool				// O - `true` on success, `false` on failure
convert_raster(
    xform_prepare_t  *p,		// I - Preparation data
    xform_document_t *d,		// I - Document
    int              document)		// I - Document number
{
  // TODO: Implement convert_raster
  (void)p;
  (void)d;
  (void)document;
  return (false);
}


//
// 'convert_text()' - Convert a plain text file to a PDF file.
//
// Text is rendered as 12pt Courier, 8 columns per tab, with no special
// formatting.
//

static bool				// O - `true` on success, `false` on failure
convert_text(
    xform_prepare_t  *p,		// I - Preparation data
    xform_document_t *d,		// I - Document
    int              document)		// I - Document number
{
  pdfio_file_t	*pdf;			// Temporary PDF file
  pdfio_stream_t *st = NULL;		// Page stream
  pdfio_obj_t	*courier;		// Courier font
  pdfio_dict_t	*dict;			// Page dictionary
  cups_file_t	*fp;			// File
  char		line[1024],		// Line from file
		*lineptr,		// Pointer into line
		outline[1024],		// Output line
		*outptr;		// Pointer into output line
  unsigned	page = 0,		// Current page number
		column = 0,		// Column on line
		columns,		// Columns per line
		linenum = 0,		// Current line
		lines;			// Number of lines per page


  // Open the text file...
  if ((fp = cupsFileOpen(d->filename, "r")) == NULL)
  {
    fprintf(stderr, "ERROR: Input Document %d: %s\n", document, strerror(errno));
    return (false);
  }

  // Create a temporary PDF file...
  if ((pdf = pdfioFileCreateTemporary(d->tempfile, sizeof(d->tempfile), "1.7", &p->media, &p->media, pdfio_error_cb, p)) == NULL)
  {
    cupsFileClose(fp);
    return (false);
  }

  // Calculate columns and rows based on media margins...
  // (Default margins are 0.5" at the top and bottom and 0.25" on the sides)
  columns = (unsigned)((p->crop.x2 - p->crop.x1) / (XFORM_TEXT_WIDTH * XFORM_TEXT_SIZE));
  lines   = (unsigned)((p->crop.y2 - p->crop.y1) / XFORM_TEXT_HEIGHT);

  if (Verbosity)
  {
    fprintf(stderr, "DEBUG: CropBox=[%g %g %g %g]\n", p->crop.x1, p->crop.y1, p->crop.x2, p->crop.y2);
    fprintf(stderr, "DEBUG: Converting text, %u lines of %u columns per page.\n", lines, columns);
  }

  // Create font and page dictionaries...
  courier = pdfioFileCreateFontObjFromBase(pdf, "Courier");
  dict    = pdfioDictCreate(pdf);

  pdfioPageDictAddFont(dict, "F1", courier);

  // Read lines from the text file...
  while (cupsFileGets(fp, line, sizeof(line)))
  {
    // Make sure the line contains at least one space...
    if (!line[0])
    {
      line[0] = ' ';
      line[1] = '\0';
    }

    // Loop through the line and write lines...
    for (column = 0, lineptr = line, outptr = outline; *lineptr; lineptr ++)
    {
      if (*lineptr == '\t')
      {
        // Tab every 8 columns...
        do
        {
          if (outptr < (outline + sizeof(outline) - 1))
            *outptr++ = ' ';

	  column ++;
        }
        while (column & 7);
      }
      else if ((*lineptr & 0xe0) == 0xc0 && (lineptr[1] & 0xc0) == 0x80)
      {
        // 2-byte UTF-8 character...
        if (outptr < (outline + sizeof(outline) - 2))
        {
          memcpy(outptr, lineptr, 2);
          outptr += 2;
        }

        lineptr ++;
        column ++;
      }
      else if ((*lineptr & 0xf0) == 0xe0 && (lineptr[1] & 0xc0) == 0x80 && (lineptr[2] & 0xc0) == 0x80)
      {
        // 3-byte UTF-8 character...
        if (outptr < (outline + sizeof(outline) - 3))
        {
          memcpy(outptr, lineptr, 3);
          outptr += 3;
        }

        lineptr += 2;
        column ++;
      }
      else if ((*lineptr & 0xf8) == 0xf0 && (lineptr[1] & 0xc0) == 0x80 && (lineptr[2] & 0xc0) == 0x80 && (lineptr[3] & 0xc0) == 0x80)
      {
        // 4-byte UTF-8 character...
        if (outptr < (outline + sizeof(outline) - 4))
        {
          memcpy(outptr, lineptr, 4);
          outptr += 4;
        }

        lineptr += 3;
        column ++;
      }
      else
      {
        // ASCII character...
        if (outptr < (outline + sizeof(outline) - 1))
	  *outptr++ = *lineptr;

	column ++;
      }

      if (column >= columns || !lineptr[1])
      {
        // End of line, write it out...
        *outptr = '\0';
        outptr  = outline;

        if (!st)
        {
          // Start new page...
          double	x, y;		// Initial text position

          // Set initial position using the crop box and any image shift specified by the corresponding attributes/options...
          page ++;
          x = p->crop.x1;
          y = p->crop.y2 - XFORM_TEXT_HEIGHT;

          if (page & 1)
          {
            if (p->options->x_image_position == IPPOPT_IMGPOS_TOP_RIGHT)
	      x -= 72.0 * p->options->x_side1_image_shift / 2540.0;
	    else
	      x += 72.0 * p->options->x_side1_image_shift / 2540.0;

            if (p->options->y_image_position == IPPOPT_IMGPOS_BOTTOM_LEFT)
	      y += 72.0 * p->options->y_side1_image_shift / 2540.0;
	    else
	      y -= 72.0 * p->options->y_side1_image_shift / 2540.0;
	  }
	  else
          {
            if (p->options->x_image_position == IPPOPT_IMGPOS_TOP_RIGHT)
	      x -= 72.0 * p->options->x_side2_image_shift / 2540.0;
	    else
	      x += 72.0 * p->options->x_side2_image_shift / 2540.0;

            if (p->options->y_image_position == IPPOPT_IMGPOS_BOTTOM_LEFT)
	      y += 72.0 * p->options->y_side2_image_shift / 2540.0;
	    else
	      y -= 72.0 * p->options->y_side2_image_shift / 2540.0;
	  }

	  // Start a new page with black text at the top left...
          st = pdfioFileCreatePage(pdf, dict);
          pdfioContentTextBegin(st);
          pdfioContentSetTextFont(st, "F1", XFORM_TEXT_SIZE);
          pdfioContentSetTextLeading(st, XFORM_TEXT_HEIGHT);
          pdfioContentTextMoveTo(st, x, y);
          pdfioContentSetFillColorDeviceGray(st, 0.0);
        }

	// Add a single line of text to the page...
        pdfioContentTextShowf(st, false, "%s\n", outline);

	linenum ++;
	column = 0;
      }

      if (linenum >= lines)
      {
        // End of page...
        pdfioContentTextEnd(st);
        pdfioStreamClose(st);
        st = NULL;
        linenum = 0;
      }
    }
  }

  // Finish current page, if any...
  if (st)
  {
    pdfioContentTextEnd(st);
    pdfioStreamClose(st);
  }

  // Close Files...
  pdfioFileClose(pdf);
  cupsFileClose(fp);

  d->pdf_filename = d->tempfile;

  return (true);
}


//
// 'copy_page()' - Copy the input page to the output page.
//

static void
copy_page(xform_prepare_t *p,		// I - Preparation data
          xform_page_t    *outpage,	// I - Output page
          size_t          layout)	// I - Layout cell
{
  pdfio_rect_t	*cell = p->layout + layout;
					// Layout cell
  pdfio_dict_t	*idict;			// Input page dictionary
  pdfio_rect_t	irect;			// Input page rectangle
  double	cwidth,			// Cell width
		cheight,		// Cell height
		iwidth,			// Input page width
		iheight,		// Input page height
		scaling;		// Scaling factor
  bool		rotate;			// Rotate 90 degrees?
  pdfio_matrix_t cm;			// Cell transform matrix
  size_t	i,			// Looping var
		count;			// Number of input page streams
  pdfio_stream_t *st;			// Input page stream
  char		buffer[65536],		// Copy buffer
		*bufptr,		// Pointer into buffer
		*bufstart,		// Start of current sequence
		*bufend,		// End of buffer
		name[256],		// Name buffer
		*nameptr;		// Pointer into name
  ssize_t	bytes;			// Bytes read
  const char	*resname;		// Resource name


  // Skip if this cell is empty...
  if (!outpage->input[layout])
    return;

  // Save state for this cell...
  pdfioContentSave(outpage->output);

  // Clip to cell...
  if (getenv("IPPTRANSFORM_DEBUG"))
  {
    // Draw a box around the cell for debugging...
    pdfioContentSetStrokeColorDeviceGray(outpage->output, 0.0);
    pdfioContentPathRect(outpage->output, cell->x1, cell->y1, cell->x2 - cell->x1, cell->y2 - cell->y1);
    pdfioContentStroke(outpage->output);
  }

  pdfioContentPathRect(outpage->output, cell->x1, cell->y1, cell->x2 - cell->x1, cell->y2 - cell->y1);
  pdfioContentClip(outpage->output, false);
  pdfioContentPathEnd(outpage->output);

  // Transform input page to output cell...
  idict = pdfioObjGetDict(outpage->input[layout]);

  if (!pdfioDictGetRect(idict, "CropBox", &irect))
  {
    // No crop box, use media box...
    if (!pdfioDictGetRect(idict, "MediaBox", &irect))
    {
      // No media box, use output page size...
      irect = p->media;
    }
  }

  cwidth  = cell->x2 - cell->x1;
  cheight = cell->y2 - cell->y1;

  iwidth  = irect.x2 - irect.x1;
  iheight = irect.y2 - irect.y1;

  if ((iwidth > iheight && cwidth < cheight) || (iwidth < iheight && cwidth > cheight))
  {
    // Need to rotate...
    rotate  = true;
    iwidth  = irect.y2 - irect.y1;
    iheight = irect.x2 - irect.x1;
  }
  else
  {
    // No rotation...
    rotate = false;
  }

  scaling = cwidth / iwidth;
  if (p->options->print_scaling == IPPOPT_SCALING_FILL)
  {
    // Scale to fill...
    if ((iheight * scaling) < cheight)
      scaling = cheight / iheight;
  }
  else
  {
    // Scale to fit...
    if ((iheight * scaling) > cheight)
      scaling = cheight / iheight;
  }

  if (rotate)
  {
    cm[0][0] = 0.0;
    cm[0][1] = -scaling;
    cm[1][0] = scaling;
    cm[1][1] = 0.0;
    cm[2][0] = cell->x1;
    cm[2][1] = cell->y2;
  }
  else
  {
    cm[0][0] = scaling;
    cm[0][1] = 0.0;
    cm[1][0] = 0.0;
    cm[1][1] = scaling;
    cm[2][0] = cell->x1;
    cm[2][1] = cell->y1;
  }

  if (Verbosity)
    fprintf(stderr, "DEBUG: Page %u, cell %u/%u, cm=[%g %g %g %g %g %g], input=%p\n", (unsigned)(outpage - p->outpages + 1), (unsigned)layout + 1, (unsigned)p->num_layout, cm[0][0], cm[0][1], cm[1][0], cm[1][1], cm[2][0], cm[2][1], (void *)outpage->input[layout]);

  pdfioContentMatrixConcat(outpage->output, cm);

  // Copy content streams...
  for (i = 0, count = pdfioPageGetNumStreams(outpage->input[layout]); i < count; i ++)
  {
    fprintf(stderr, "DEBUG: Opening content stream %u/%u...\n", (unsigned)i + 1, (unsigned)count);

    if ((st = pdfioPageOpenStream(outpage->input[layout], i, true)) != NULL)
    {
      fprintf(stderr, "DEBUG: Opened stream %u, resmap[%u]=%p\n", (unsigned)i + 1, (unsigned)layout, (void *)outpage->resmap[layout]);
      if (outpage->resmap[layout])
      {
        // Need to map resource names...
	while ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) > 0)
	{
	  for (bufptr = buffer, bufstart = buffer, bufend = buffer + bytes; bufptr < bufend;)
	  {
	    if (*bufptr == '/')
	    {
	      // Start of name...
	      bool done = false;		// Done with name?

	      bufptr ++;
	      pdfioStreamWrite(outpage->output, bufstart, (size_t)(bufptr - bufstart));

              nameptr = name;

              do
              {
		if (bufptr >= bufend)
		{
		  // Read another buffer's worth...
		  if ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) <= 0)
		    break;

                  bufptr = buffer;
                  bufend = buffer + bytes;
		}

		if (strchr("<>(){}[]/% \t\n\r", *bufptr))
		{
		  // Delimiting character...
		  done = true;
		}
		else if (*bufptr == '#')
		{
		  int	j,			// Looping var
			ch = 0;			// Escaped character

                  for (j = 0; j < 2; j ++)
                  {
		    bufptr ++;
		    if (bufptr >= bufend)
		    {
		      // Read another buffer's worth...
		      if ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) <= 0)
			break;

		      bufptr = buffer;
		      bufend = buffer + bytes;
		    }

		    if (!isxdigit(*bufptr & 255))
		      break;
		    else if (isdigit(*bufptr))
		      ch = (ch << 4) | (*bufptr - '0');
		    else
		      ch = (ch << 4) | (tolower(*bufptr) - 'a' + 10);
		  }

                  if (nameptr < (name + sizeof(name) - 1))
                  {
                    // Save escaped character...
                    *nameptr++ = (char)ch;
		    bufptr ++;
		  }
		  else
		  {
		    // Not enough room...
		    break;
		  }
		}
		else if (nameptr < (name + sizeof(name) - 1))
		{
		  // Save literal character...
		  *nameptr++ = *bufptr++;
		}
		else
		{
		  // Not enough room...
		  break;
		}
	      }
	      while (!done);

              bufstart = bufptr;
	      *nameptr = '\0';

	      // See if it needs to be mapped...
	      if ((resname = pdfioDictGetName(outpage->resmap[layout], name)) == NULL)
		resname = name;

	      pdfioStreamPuts(outpage->output, resname);
	    }
	    else if (buffer[0] == '(')
	    {
	      // Skip string...
	      bool	done = false;		// Are we done yet?
	      int	parens = 0;		// Number of parenthesis

	      do
	      {
		bufptr ++;
		if (bufptr >= bufend)
		{
		  // Save what has been skipped so far...
		  pdfioStreamWrite(outpage->output, bufstart, (size_t)(bufptr - bufstart));

                  // Read another buffer's worth...
		  if ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) <= 0)
		    break;

		  bufptr   = buffer;
		  bufstart = buffer;
		  bufend   = buffer + bytes;
		}

		if (*bufptr == ')')
		{
		  if (parens > 0)
		    parens --;
		  else
		    done = true;

		  bufptr ++;
		}
		else if (*bufptr == '(')
		{
		  parens ++;
		  bufptr ++;
		}
		else if (*bufptr == '\\')
		{
		  // Skip escaped character...
		  bufptr ++;

		  if (bufptr >= bufend)
		  {
		    // Save what has been skipped so far...
		    pdfioStreamWrite(outpage->output, bufstart, (size_t)(bufptr - bufstart));

		    // Read another buffer's worth...
		    if ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) <= 0)
		      break;

		    bufptr   = buffer;
		    bufstart = buffer;
		    bufend   = buffer + bytes;
		  }

                  bufptr ++;
		}
		else
		{
		  bufptr ++;
		}
	      }
	      while (!done);
	    }
	    else
	    {
	      // Skip one character...
	      bufptr ++;
	    }
	  }

	  if (bufptr > bufstart)
	  {
	    // Write the remainder...
	    pdfioStreamWrite(outpage->output, bufstart, (size_t)(bufptr - bufstart));
	  }
	}
      }
      else
      {
        // Copy page stream verbatim...
        while ((bytes = pdfioStreamRead(st, buffer, sizeof(buffer))) > 0)
          pdfioStreamWrite(outpage->output, buffer, (size_t)bytes);
      }

      pdfioStreamClose(st);
    }
  }

  // Restore state...
  pdfioContentRestore(outpage->output);
}


//
// 'generate_job_error_sheet()' - Generate a job error sheet.
//

static bool				// O - `true` on success, `false` on failure
generate_job_error_sheet(
    xform_prepare_t  *p)		// I - Preparation data
{
  pdfio_stream_t *st;			// Page stream
  pdfio_obj_t	*courier;		// Courier font
  pdfio_dict_t	*dict;			// Page dictionary
  size_t	i,			// Looping var
		count;			// Number of pages
  const char	*msg;			// Current message
  size_t	mcount;			// Number of messages


  // Create a page dictionary with the Courier font...
  courier = pdfioFileCreateFontObjFromBase(p->pdf, "Courier");
  dict    = pdfioDictCreate(p->pdf);

  pdfioPageDictAddFont(dict, "F1", courier);

  // Figure out how many impressions to produce...
  if (!strcmp(p->options->sides, "one-sided"))
    count = 1;
  else
    count = 2;

  // Create pages...
  for (i = 0; i < count; i ++)
  {
    // Create the error sheet...
    st = pdfio_start_page(p, dict);

    // The job error sheet is a banner with the following information:
    //
    //   Errors:
    //     ...
    //
    //   Warnings:
    //     ...

    pdfioContentSetFillColorDeviceGray(st, 0.0);
    pdfioContentTextBegin(st);
    pdfioContentTextMoveTo(st, p->crop.x1, p->crop.y2 - 2.0 * XFORM_TEXT_SIZE);
    pdfioContentSetTextFont(st, "F1", 2.0 * XFORM_TEXT_SIZE);
    pdfioContentSetTextLeading(st, 2.0 * XFORM_TEXT_HEIGHT);
    pdfioContentTextShow(st, false, "Errors:\n");

    pdfioContentSetTextFont(st, "F1", XFORM_TEXT_SIZE);
    pdfioContentSetTextLeading(st, XFORM_TEXT_HEIGHT);

    for (msg = (const char *)cupsArrayGetFirst(p->errors), mcount = 0; msg; msg = (const char *)cupsArrayGetNext(p->errors))
    {
      if (*msg == 'E')
      {
	pdfioContentTextShowf(st, false, "  %s\n", msg + 1);
	mcount ++;
      }
    }

    if (mcount == 0)
      pdfioContentTextShow(st, false, "  No Errors\n");

    pdfioContentSetTextFont(st, "F1", 2.0 * XFORM_TEXT_SIZE);
    pdfioContentSetTextLeading(st, 2.0 * XFORM_TEXT_HEIGHT);
    pdfioContentTextShow(st, false, "\n");
    pdfioContentTextShow(st, false, "Warnings:\n");

    pdfioContentSetTextFont(st, "F1", XFORM_TEXT_SIZE);
    pdfioContentSetTextLeading(st, XFORM_TEXT_HEIGHT);

    for (msg = (const char *)cupsArrayGetFirst(p->errors), mcount = 0; msg; msg = (const char *)cupsArrayGetNext(p->errors))
    {
      if (*msg == 'I')
      {
	pdfioContentTextShowf(st, false, "  %s\n", msg + 1);
	mcount ++;
      }
    }

    if (mcount == 0)
      pdfioContentTextShow(st, false, "  No Warnings\n");

    pdfioContentTextEnd(st);
    pdfio_end_page(p, st);
  }

  return (true);
}


//
// 'generate_job_sheets()' - Generate a job banner sheet.
//

static bool				// O - `true` on success, `false` on failure
generate_job_sheets(
    xform_prepare_t  *p)		// I - Preparation data
{
  pdfio_stream_t *st;			// Page stream
  pdfio_obj_t	*courier;		// Courier font
  pdfio_dict_t	*dict;			// Page dictionary
  size_t	i,			// Looping var
		count;			// Number of pages

  // Create a page dictionary with the Courier font...
  courier = pdfioFileCreateFontObjFromBase(p->pdf, "Courier");
  dict    = pdfioDictCreate(p->pdf);

  pdfioPageDictAddFont(dict, "F1", courier);

  // Figure out how many impressions to produce...
  if (!strcmp(p->options->sides, "one-sided"))
    count = 1;
  else
    count = 2;

  // Create pages...
  for (i = 0; i < count; i ++)
  {
    st = pdfio_start_page(p, dict);

    // The job sheet is a banner with the following information:
    //
    //     Title: job-title
    //      User: job-originating-user-name
    //     Pages: job-media-sheets
    //   Message: job-sheet-message

    pdfioContentTextBegin(st);
    pdfioContentSetTextFont(st, "F1", 2.0 * XFORM_TEXT_SIZE);
    pdfioContentSetTextLeading(st, 2.0 * XFORM_TEXT_HEIGHT);
    pdfioContentTextMoveTo(st, p->media.x2 / 8.0, p->media.y2 / 2.0 + 2 * (XFORM_TEXT_HEIGHT + XFORM_TEXT_SIZE));
    pdfioContentSetFillColorDeviceGray(st, 0.0);

    pdfioContentTextShowf(st, false, "  Title: %s\n", p->options->job_name);
    pdfioContentTextShowf(st, false, "   User: %s\n", p->options->job_originating_user_name);
    pdfioContentTextShowf(st, false, "  Pages: %u\n", (unsigned)(p->num_outpages / count));
    if (p->options->job_sheet_message[0])
      pdfioContentTextShowf(st, false, "Message: %s\n", p->options->job_sheet_message);

    pdfioContentTextEnd(st);
    pdfio_end_page(p, st);
  }

  return (true);
}


//
// 'monitor_ipp()' - Monitor IPP printer status.
//

static void *				// O - Thread exit status
monitor_ipp(const char *device_uri)	// I - Device URI
{
  int		i;			// Looping var
  http_t	*http;			// HTTP connection
  ipp_t		*request,		// IPP request
		*response;		// IPP response
  ipp_attribute_t *attr;		// IPP response attribute
  char		scheme[32],		// URI scheme
		userpass[256],		// URI user:pass
		host[256],		// URI host
		resource[1024];		// URI resource
  int		port;			// URI port number
  http_encryption_t encryption;		// Encryption to use
  int		delay = 1,		// Current delay
		next_delay,		// Next delay
		prev_delay = 0;		// Previous delay
  char		pvalues[10][1024];	// Current printer attribute values
  static const char * const pattrs[10] =// Printer attributes we need
  {
    "marker-colors",
    "marker-levels",
    "marker-low-levels",
    "marker-high-levels",
    "marker-names",
    "marker-types",
    "printer-alert",
    "printer-state-reasons",
    "printer-supply",
    "printer-supply-description"
  };


  httpSeparateURI(HTTP_URI_CODING_ALL, device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource));

  if (port == 443 || !strcmp(scheme, "ipps"))
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  while ((http = httpConnect(host, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
  {
    fprintf(stderr, "ERROR: Unable to connect to '%s' on port %d: %s\n", host, port, cupsLastErrorString());
    sleep(30);
  }

 /*
  * Report printer state changes until we are canceled...
  */

  for (;;)
  {
   /*
    * Poll for the current state...
    */

    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, device_uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

    response = cupsDoRequest(http, request, resource);

   /*
    * Report any differences...
    */

    for (attr = ippGetFirstAttribute(response); attr; attr = ippGetNextAttribute(response))
    {
      const char *name = ippGetName(attr);
      char	value[1024];		// Name and value


      if (!name)
        continue;

      for (i = 0; i < (int)(sizeof(pattrs) / sizeof(pattrs[0])); i ++)
        if (!strcmp(name, pattrs[i]))
	  break;

      if (i >= (int)(sizeof(pattrs) / sizeof(pattrs[0])))
        continue;

      ippAttributeString(attr, value, sizeof(value));

      if (strcmp(value, pvalues[i]))
      {
        if (!strcmp(name, "printer-state-reasons"))
	  fprintf(stderr, "STATE: %s\n", value);
	else
	  fprintf(stderr, "ATTR: %s='%s'\n", name, value);

        cupsCopyString(pvalues[i], value, sizeof(pvalues[i]));
      }
    }

    ippDelete(response);

   /*
    * Sleep until the next update...
    */

    sleep((unsigned)delay);

    next_delay = (delay + prev_delay) % 12;
    prev_delay = next_delay < delay ? 0 : delay;
    delay      = next_delay;
  }

  return (NULL);
}


#ifdef HAVE_COREGRAPHICS
//
// 'pack_rgba()' - Pack RGBX scanlines into RGB scanlines.
//
// This routine is suitable only for 8 bit RGBX data packed into RGB bytes.
//

static void
pack_rgba(unsigned char *row,		// I - Row of pixels to pack
	  size_t        num_pixels)	// I - Number of pixels in row
{
  size_t	num_quads = num_pixels / 4;
					// Number of 4 byte samples to pack
  size_t	leftover_pixels = num_pixels & 3;
					// Number of pixels remaining
  unsigned	*quad_row = (unsigned *)row;
					// 32-bit pixel pointer
  unsigned	*dest = quad_row;	// Destination pointer
  unsigned char *src_byte;		// Remaining source bytes
  unsigned char *dest_byte;		// Remaining destination bytes


 /*
  * Copy all of the groups of 4 pixels we can...
  */

  while (num_quads > 0)
  {
    *dest++ = (quad_row[0] & XFORM_RGB_MASK) | (quad_row[1] << 24);
    *dest++ = ((quad_row[1] & XFORM_BG_MASK) >> 8) |
              ((quad_row[2] & XFORM_RG_MASK) << 16);
    *dest++ = ((quad_row[2] & XFORM_BLUE_MASK) >> 16) | (quad_row[3] << 8);
    quad_row += 4;
    num_quads --;
  }

 /*
  * Then handle the leftover pixels...
  */

  src_byte  = (unsigned char *)quad_row;
  dest_byte = (unsigned char *)dest;

  while (leftover_pixels > 0)
  {
    *dest_byte++ = *src_byte++;
    *dest_byte++ = *src_byte++;
    *dest_byte++ = *src_byte++;
    src_byte ++;
    leftover_pixels --;
  }
}


//
// 'pack_rgba16()' - Pack 16 bit per component RGBX scanlines into RGB scanlines.
//
// This routine is suitable only for 16 bit RGBX data packed into RGB bytes.
//

static void
pack_rgba16(unsigned char *row,		// I - Row of pixels to pack
	    size_t        num_pixels)	// I - Number of pixels in row
{
  const unsigned	*from = (unsigned *)row;
					// 32 bits from row
  unsigned		*dest = (unsigned *)row;
					// Destination pointer


  while (num_pixels > 1)
  {
    *dest++ = from[0];
    *dest++ = (from[1] & 0x0000ffff) | ((from[2] & 0x0000ffff) << 16);
    *dest++ = ((from[2] & 0xffff0000) >> 16) | ((from[3] & 0x0000ffff) << 16);
    from += 4;
    num_pixels -= 2;
  }

  if (num_pixels)
  {
    *dest++ = *from++;
    *dest++ = *from++;
  }
}
#endif // HAVE_COREGRAPHICS


//
// 'page_dict_cb()' - Merge page dictionaries from multiple input pages.
//
// This function detects resource conflicts and maps conflicting names as
// needed.
//

static bool				// O - `true` to continue, `false` to stop
page_dict_cb(pdfio_dict_t *dict,	// I - Dictionary
             const char   *key,		// I - Dictionary key
             xform_page_t *outpage)	// I - Output page
{
  pdfio_array_t	*arrayres;		// Array resource
  pdfio_array_t	*arrayval = NULL;	// Array value
  pdfio_dict_t	*dictval = NULL;	// Dictionary value
  pdfio_obj_t	*objval;		// Object value


  fprintf(stderr, "DEBUG: page_dict_cb(dict=%p, key=\"%s\", outpage=%p), type=%d\n", (void *)dict, key, (void *)outpage, pdfioDictGetType(dict, key));

  if (strcmp(key, "ColorSpace") && strcmp(key, "ExtGState") && strcmp(key, "Font") && strcmp(key, "Pattern") && strcmp(key, "ProcSet") && strcmp(key, "Properties") && strcmp(key, "Shading") && strcmp(key, "XObject"))
    return (true);

  switch (pdfioDictGetType(dict, key))
  {
    case PDFIO_VALTYPE_ARRAY : // Array resource
        arrayval = pdfioDictGetArray(dict, key);
        break;

    case PDFIO_VALTYPE_DICT : // Dictionary resource
        dictval = pdfioDictGetDict(dict, key);
        break;

    case PDFIO_VALTYPE_INDIRECT : // Object reference to dictionary
        objval   = pdfioDictGetObj(dict, key);
        arrayval = pdfioObjGetArray(objval);
        dictval  = pdfioObjGetDict(objval);

        fprintf(stderr, "DEBUG: page_dict_cb: objval=%p(%u), arrayval=%p, dictval=%p\n", (void *)objval, (unsigned)pdfioObjGetNumber(objval), (void *)arrayval, (void *)dictval);
        break;

    default :
        break;
  }

  if (arrayval)
  {
    // Copy/merge an array resource...
    if ((arrayres = pdfioDictGetArray(outpage->resdict, key)) == NULL)
    {
      // Copy array
      pdfioDictSetArray(outpage->resdict, pdfioStringCreate(outpage->pdf, key), pdfioArrayCopy(outpage->pdf, arrayval));
    }
    else if (!strcmp(key, "ProcSet"))
    {
      // Merge ProcSet array
      size_t		i, j,		// Looping var
			ic, jc;		// Counts
      const char	*iv, *jv;	// Values

      for (i = 0, ic = pdfioArrayGetSize(arrayval); i < ic; i ++)
      {
	if ((iv = pdfioArrayGetName(arrayval, i)) == NULL)
	  continue;

	for (j = 0, jc = pdfioArrayGetSize(arrayres); j < jc; j ++)
	{
	  if ((jv = pdfioArrayGetName(arrayres, j)) == NULL)
	    continue;

	  if (!strcmp(iv, jv))
	    break;
	}

	if (j >= jc)
	  pdfioArrayAppendName(arrayres, pdfioStringCreate(outpage->pdf, iv));
      }
    }
  }
  else if (dictval)
  {
    // Copy/merge dictionary...
    if ((outpage->restype = pdfioDictGetDict(outpage->resdict, key)) == NULL)
      pdfioDictSetDict(outpage->resdict, pdfioStringCreate(outpage->pdf, key), pdfioDictCopy(outpage->pdf, dictval));
    else
      pdfioDictIterateKeys(dictval, (pdfio_dict_cb_t)resource_dict_cb, outpage);
  }

  return (true);
}


//
// 'pcl_end_job()' - End a PCL "job".
//

static void
pcl_end_job(xform_raster_t   *ras,	// I - Raster information
            xform_write_cb_t cb,	// I - Write callback
            void             *ctx)	// I - Write context
{
  (void)ras;

 /*
  * Send a PCL reset sequence.
  */

  (*cb)(ctx, (const unsigned char *)"\033E", 2);
}


//
// 'pcl_end_page()' - End of PCL page.
//

static void
pcl_end_page(xform_raster_t   *ras,	// I - Raster information
	     unsigned         page,	// I - Current page
             xform_write_cb_t cb,	// I - Write callback
             void             *ctx)	// I - Write context
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


//
// 'pcl_init()' - Initialize callbacks for PCL output.
//

static void
pcl_init(xform_raster_t *ras)		// I - Raster information
{
  ras->end_job    = pcl_end_job;
  ras->end_page   = pcl_end_page;
  ras->start_job  = pcl_start_job;
  ras->start_page = pcl_start_page;
  ras->write_line = pcl_write_line;
}


//
// 'pcl_printf()' - Write a formatted string.
//

static void
pcl_printf(xform_write_cb_t cb,		// I - Write callback
           void             *ctx,	// I - Write context
	   const char       *format,	// I - Printf-style format string
	   ...)				// I - Additional arguments as needed
{
  va_list	ap;			// Argument pointer
  char		buffer[8192];		// Buffer


  va_start(ap, format);
  vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  (*cb)(ctx, (const unsigned char *)buffer, strlen(buffer));
}


//
// 'pcl_start_job()' - Start a PCL "job".
//

static void
pcl_start_job(xform_raster_t   *ras,	// I - Raster information
              xform_write_cb_t cb,	// I - Write callback
              void             *ctx)	// I - Write context
{
  (void)ras;

 /*
  * Send a PCL reset sequence.
  */

  (*cb)(ctx, (const unsigned char *)"\033E", 2);
}


//
// 'pcl_start_page()' - Start a PCL page.
//

static void
pcl_start_page(xform_raster_t   *ras,	// I - Raster information
               unsigned         page,	// I - Current page
               xform_write_cb_t cb,	// I - Write callback
               void             *ctx)	// I - Write context
{
 /*
  * Setup margins to be 1/6" top and bottom and 1/4" or .135" on the
  * left and right.
  */

  ras->top    = ras->header.HWResolution[1] / 6;
  ras->bottom = ras->header.cupsHeight - ras->header.HWResolution[1] / 6;

  if (ras->header.PageSize[1] == 842)
  {
   // A4 gets special side margins to expose an 8" print area
    ras->left  = (ras->header.cupsWidth - 8 * ras->header.HWResolution[0]) / 2;
    ras->right = ras->left + 8 * ras->header.HWResolution[0];
  }
  else
  {
   // All other sizes get 1/4" margins
    ras->left  = ras->header.HWResolution[0] / 4;
    ras->right = ras->header.cupsWidth - ras->header.HWResolution[0] / 4;
  }

  if (!ras->header.Duplex || (page & 1))
  {
   /*
    * Set the media size...
    */

    pcl_printf(cb, ctx, "\033&l12D\033&k12H");
					// Set 12 LPI, 10 CPI
    pcl_printf(cb, ctx, "\033&l0O");	// Set portrait orientation

    switch (ras->header.PageSize[1])
    {
      case 540 : // Monarch Envelope
          pcl_printf(cb, ctx, "\033&l80A");
	  break;

      case 595 : // A5
          pcl_printf(cb, ctx, "\033&l25A");
	  break;

      case 624 : // DL Envelope
          pcl_printf(cb, ctx, "\033&l90A");
	  break;

      case 649 : // C5 Envelope
          pcl_printf(cb, ctx, "\033&l91A");
	  break;

      case 684 : // COM-10 Envelope
          pcl_printf(cb, ctx, "\033&l81A");
	  break;

      case 709 : // B5 Envelope
          pcl_printf(cb, ctx, "\033&l100A");
	  break;

      case 756 : // Executive
          pcl_printf(cb, ctx, "\033&l1A");
	  break;

      case 792 : // Letter
          pcl_printf(cb, ctx, "\033&l2A");
	  break;

      case 842 : // A4
          pcl_printf(cb, ctx, "\033&l26A");
	  break;

      case 1008 : // Legal
          pcl_printf(cb, ctx, "\033&l3A");
	  break;

      case 1191 : // A3
          pcl_printf(cb, ctx, "\033&l27A");
	  break;

      case 1224 : // Tabloid
          pcl_printf(cb, ctx, "\033&l6A");
	  break;
    }

   /*
    * Set top margin and turn off perforation skip...
    */

    pcl_printf(cb, ctx, "\033&l%uE\033&l0L", 12 * ras->top / ras->header.HWResolution[1]);

    if (ras->header.Duplex)
    {
      int mode = ras->header.Duplex ? 1 + ras->header.Tumble != 0 : 0;

      pcl_printf(cb, ctx, "\033&l%dS", mode);
					// Set duplex mode
    }
  }
  else if (ras->header.Duplex)
    pcl_printf(cb, ctx, "\033&a2G");	// Print on back side

 /*
  * Set graphics mode...
  */

  pcl_printf(cb, ctx, "\033*t%uR", ras->header.HWResolution[0]);
					// Set resolution
  pcl_printf(cb, ctx, "\033*r%uS", ras->right - ras->left);
					// Set width
  pcl_printf(cb, ctx, "\033*r%uT", ras->bottom - ras->top);
					// Set height
  pcl_printf(cb, ctx, "\033&a0H\033&a%uV", 720 * ras->top / ras->header.HWResolution[1]);
					// Set position

  pcl_printf(cb, ctx, "\033*b2M");	// Use PackBits compression
  pcl_printf(cb, ctx, "\033*r1A");	// Start graphics

 /*
  * Allocate the output buffer...
  */

  ras->out_blanks  = 0;
  ras->out_length  = (ras->right - ras->left + 7) / 8;
  ras->out_buffer  = malloc(ras->out_length);
  ras->comp_buffer = malloc(2 * ras->out_length + 2);
}


//
// 'pcl_write_line()' - Write a line of raster data.
//

static void
pcl_write_line(
    xform_raster_t      *ras,		// I - Raster information
    unsigned            y,		// I - Line number
    const unsigned char *line,		// I - Pixels on line
    xform_write_cb_t    cb,		// I - Write callback
    void                *ctx)		// I - Write context
{
  unsigned	x;			// Column number
  unsigned char	bit,			// Current bit
		byte,			// Current byte
		*outptr,		// Pointer into output buffer
		*outend,		// End of output buffer
		*start,			// Start of sequence
		*compptr;		// Pointer into compression buffer
  unsigned	count;			// Count of bytes for output
  const unsigned char	*ditherline;	// Pointer into dither table


  if (line[0] == 255 && !memcmp(line, line + 1, ras->right - ras->left - 1))
  {
   /*
    * Skip blank line...
    */

    ras->out_blanks ++;
    return;
  }

 /*
  * Dither the line into the output buffer...
  */

  y &= 63;
  ditherline = ras->dither[y];

  for (x = ras->left, bit = 128, byte = 0, outptr = ras->out_buffer; x < ras->right; x ++, line ++)
  {
    if (*line <= ditherline[x & 63])
      byte |= bit;

    if (bit == 1)
    {
      *outptr++ = byte;
      byte      = 0;
      bit       = 128;
    }
    else
      bit >>= 1;
  }

  if (bit != 128)
    *outptr++ = byte;

 /*
  * Apply compression...
  */

  compptr = ras->comp_buffer;
  outend  = outptr;
  outptr  = ras->out_buffer;

  while (outptr < outend)
  {
    if ((outptr + 1) >= outend)
    {
     /*
      * Single byte on the end...
      */

      *compptr++ = 0x00;
      *compptr++ = *outptr++;
    }
    else if (outptr[0] == outptr[1])
    {
     /*
      * Repeated sequence...
      */

      outptr ++;
      count = 2;

      while (outptr < (outend - 1) &&
	     outptr[0] == outptr[1] &&
	     count < 127)
      {
	outptr ++;
	count ++;
      }

      *compptr++ = (unsigned char)(257 - count);
      *compptr++ = *outptr++;
    }
    else
    {
     /*
      * Non-repeated sequence...
      */

      start = outptr;
      outptr ++;
      count = 1;

      while (outptr < (outend - 1) &&
	     outptr[0] != outptr[1] &&
	     count < 127)
      {
	outptr ++;
	count ++;
      }

      *compptr++ = (unsigned char)(count - 1);

      memcpy(compptr, start, count);
      compptr += count;
    }
  }

 /*
  * Output the line...
  */

  if (ras->out_blanks > 0)
  {
   /*
    * Skip blank lines first...
    */

    pcl_printf(cb, ctx, "\033*b%dY", ras->out_blanks);
    ras->out_blanks = 0;
  }

  pcl_printf(cb, ctx, "\033*b%dW", (int)(compptr - ras->comp_buffer));
  (*cb)(ctx, ras->comp_buffer, (size_t)(compptr - ras->comp_buffer));
}


//
// 'pdfio_end_page()' - End a page.
//
// This function restores graphics state when ending a back side page.
//

static void
pdfio_end_page(xform_prepare_t *p,	// I - Preparation data
               pdfio_stream_t  *st)	// I - Page content stream
{
  if (p->use_duplex_xform && !(pdfioFileGetNumPages(p->pdf) & 1))
    pdfioContentRestore(st);

  pdfioStreamClose(st);
}


//
// 'pdfio_error_cb()' - Log an error from the PDFio library.
//

static bool				// O - `false` to stop
pdfio_error_cb(pdfio_file_t *pdf,	// I - PDF file (unused)
               const char   *message,	// I - Error message
               void         *cb_data)	// I - Preparation data
{
  xform_prepare_t	*p = (xform_prepare_t *)cb_data;
					// Preparation data


  if (pdf != p->pdf)
    prepare_log(p, true, "Input Document %d: %s", p->document, message);
  else
    prepare_log(p, true, "Output Document: %s", message);

  return (false);
}


//
// 'pdfio_password_cb()' - Return the password, if any, for the input document.
//

static const char *			// O - Document password
pdfio_password_cb(void       *cb_data,	// I - Document number
                  const char *filename)	// I - Filename (unused)
{
  int	document = *((int *)cb_data);	// Document number
  char	name[128];			// Environment variable name


  (void)filename;

  if (document > 1)
  {
    snprintf(name, sizeof(name), "IPP_DOCUMENT_PASSWORD%d", document);
    return (getenv(name));
  }
  else
  {
    return (getenv("IPP_DOCUMENT_PASSWORD"));
  }
}


//
// 'pdfio_start_page()' - Start a page.
//
// This function applies a transform on the back side pages.
//

static pdfio_stream_t *			// O - Page content stream
pdfio_start_page(xform_prepare_t *p,	// I - Preparation data
                 pdfio_dict_t    *dict)	// I - Page dictionary
{
  pdfio_stream_t	*st;		// Page content stream


  if ((st = pdfioFileCreatePage(p->pdf, dict)) != NULL)
  {
    if (p->use_duplex_xform && !(pdfioFileGetNumPages(p->pdf) & 1))
    {
      pdfioContentSave(st);
      pdfioContentMatrixConcat(st, p->duplex_xform);
    }
  }

  return (st);
}


//
// 'prepare_documents()' - Prepare one or more documents for printing.
//
// This function generates a single PDF file containing the union of the input
// documents and any job sheets.
//

static bool				// O - `true` on success, `false` on failure
prepare_documents(
    size_t           num_documents,	// I - Number of input documents
    xform_document_t *documents,	// I - Input documents
    ipp_options_t    *options,		// I - IPP options
    const char       *sheet_back,	// I - Back side transform
    char             *outfile,		// I - Output filename buffer
    size_t           outsize,		// I - Output filename buffer size
    unsigned         *outpages)		// O - Number of pages
{
  bool			ret = false;	// Return value
  size_t		i;		// Looping var
  xform_prepare_t	p;		// Preparation data
  xform_document_t	*d;		// Current document
  xform_page_t		*outpage;	// Current output page
  int			outdir;		// Output direction
  bool			reverse_order;	// Should output be in reverse order?
  size_t		layout;		// Layout cell
  int			document;	// Document number
  int			page;		// Current page number
  bool			duplex = !strncmp(options->sides, "two-sided-", 10);
					// Doing two-sided printing?


  // Initialize data for preparing input files for transform...
  memset(&p, 0, sizeof(p));
  p.options = options;
  p.errors  = cupsArrayNew(NULL, NULL, NULL, 0, (cups_acopy_cb_t)strdup, (cups_afree_cb_t)free);

  size_to_rect(&options->media, &p.media, &p.crop);
  prepare_number_up(&p);

  if (!strncmp(options->sides, "two-sided-", 10) && sheet_back && strcmp(sheet_back, "normal"))
  {
    // Need to do a transform on the back side of pages...
    if (!strcmp(sheet_back, "flipped"))
    {
      if (!strcmp(options->sides, "two-sided-short-edge"))
      {
	p.use_duplex_xform   = true;
        p.duplex_xform[0][0] = -1.0;
        p.duplex_xform[0][1] = 0.0;
        p.duplex_xform[1][0] = 0.0;
        p.duplex_xform[1][1] = 1.0;
        p.duplex_xform[2][0] = p.media.x2;
        p.duplex_xform[2][1] = 0.0;
      }
      else
      {
	p.use_duplex_xform   = true;
        p.duplex_xform[0][0] = 1.0;
        p.duplex_xform[0][1] = 0.0;
        p.duplex_xform[1][0] = 0.0;
        p.duplex_xform[1][1] = -1.0;
        p.duplex_xform[2][0] = 0.0;
        p.duplex_xform[2][1] = p.media.y2;
      }
    }
    else if ((!strcmp(sheet_back, "manual-tumble") && !strcmp(options->sides, "two-sided-short-edge")) || (!strcmp(sheet_back, "rotated") && !strcmp(options->sides, "two-sided-long-edge")))
    {
      p.use_duplex_xform   = true;
      p.duplex_xform[0][0] = -1.0;
      p.duplex_xform[0][1] = 0.0;
      p.duplex_xform[1][0] = 0.0;
      p.duplex_xform[1][1] = -1.0;
      p.duplex_xform[2][0] = p.media.x2;
      p.duplex_xform[2][1] = p.media.y2;
    }
  }

  if ((p.pdf = pdfioFileCreateTemporary(outfile, outsize, "1.7", &p.media, &p.media, pdfio_error_cb, &p)) == NULL)
    return (false);

  // Loop through the input documents to count pages, etc.
  for (i = num_documents, d = documents, document = 1, page = 1; i > 0; i --, d ++, document ++)
  {
    if (Verbosity)
      fprintf(stderr, "DEBUG: Preparing document %d: '%s' (%s)\n", document, d->filename, d->format);

    if (!strcmp(d->format, "application/pdf"))
    {
      // PDF file...
      d->pdf_filename = d->filename;
    }
    else if (!strcmp(d->format, "image/jpeg") || !strcmp(d->format, "image/png"))
    {
      // JPEG or PNG image...
      if (!convert_image(&p, d, document))
        goto done;
    }
    else if (!strcmp(d->format, "text/plain"))
    {
      // Plain text file...
      if (!convert_text(&p, d, document))
        goto done;
    }
    else
    {
      // PWG or Apple raster file...
      if (!convert_raster(&p, d, document))
        goto done;
    }

    if (Verbosity)
      fprintf(stderr, "DEBUG: Opening prepared document %d: '%s'.\n", document, d->pdf_filename);

    if ((d->pdf = pdfioFileOpen(d->pdf_filename, pdfio_password_cb, &document, pdfio_error_cb, &p)) == NULL)
      goto done;

    if (options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
    {
      d->first_page = 1;
      d->last_page  = (int)pdfioFileGetNumPages(d->pdf);
    }
    else
    {
      d->first_page = page;
      d->last_page  = page + (int)pdfioFileGetNumPages(d->pdf) - 1;
    }

    if (Verbosity)
      fprintf(stderr, "DEBUG: Document %d: pages %d to %d.\n", document, d->first_page, d->last_page);

    while (page <= d->last_page)
    {
      if (options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
      {
        if (ippOptionsCheckPage(options, page - d->first_page + 1))
          d->num_pages ++;
      }
      else if (ippOptionsCheckPage(options, page))
      {
        d->num_pages ++;
      }

      page ++;
    }

    if ((d->last_page & 1) && duplex && options->multiple_document_handling != IPPOPT_HANDLING_SINGLE_DOCUMENT)
    {
      d->last_page ++;
      page ++;
    }

    if ((d->num_pages & 1) && duplex && options->multiple_document_handling != IPPOPT_HANDLING_SINGLE_DOCUMENT)
      d->num_pages ++;

    p.num_inpages += d->num_pages;
  }

  // When doing N-up or booklet printing, the default is to scale to fit unless
  // fill is explicitly chosen...
  if (p.num_layout > 1 && options->print_scaling != IPPOPT_SCALING_FILL)
    options->print_scaling = IPPOPT_SCALING_FIT;

  // Prepare output pages...
  prepare_pages(&p, num_documents, documents);

  // Add job-sheets content...
  if (options->job_sheets[0] && strcmp(options->job_sheets, "none"))
    generate_job_sheets(&p);

  // Copy pages to the output file...
  reverse_order = !strcmp(options->output_bin, "face-up");
  if (options->page_delivery >= IPPOPT_DELIVERY_REVERSE_ORDER_FACE_DOWN)
    reverse_order = !reverse_order;

  if (reverse_order)
  {
    outpage = p.outpages + p.num_outpages - 1;
    outdir  = -1;
  }
  else
  {
    outpage = p.outpages;
    outdir  = 1;
  }

  if (p.num_layout == 1)
  {
    // Simple path - no layout of pages so we can just copy the pages quickly.
    if (Verbosity)
      fputs("DEBUG: Doing fast copy of pages.\n", stderr);

    for (i = p.num_outpages; i > 0; i --, outpage += outdir)
    {
      if (outpage->input[0])
        pdfioPageCopy(p.pdf, outpage->input[0]);
    }
  }
  else
  {
    // Layout path - merge page resources and do mapping of resources as needed
    if (Verbosity)
      fprintf(stderr, "DEBUG: Doing full layout of %u pages.\n", (unsigned)p.num_outpages);

    for (i = p.num_outpages; i > 0; i --, outpage += outdir)
    {
      // Create a page dictionary that merges the resources from each of the
      // input pages...
      if (Verbosity)
        fprintf(stderr, "DEBUG: Laying out page %u/%u.\n", (unsigned)(outpage - p.outpages + 1), (unsigned)p.num_outpages);

      outpage->pagedict = pdfioDictCreate(p.pdf);
      outpage->resdict  = pdfioDictCreate(p.pdf);

      pdfioDictSetRect(outpage->pagedict, "CropBox", &p.media);
      pdfioDictSetRect(outpage->pagedict, "MediaBox", &p.media);
      pdfioDictSetDict(outpage->pagedict, "Resources", outpage->resdict);
      pdfioDictSetName(outpage->pagedict, "Type", "Page");

      for (layout = 0; layout < p.num_layout; layout ++)
      {
        if (!outpage->input[layout])
          continue;

        outpage->layout  = layout;
        outpage->restype = NULL;
        pdfioDictIterateKeys(pdfioDictGetDict(pdfioObjGetDict(outpage->input[layout]), "Resources"), (pdfio_dict_cb_t)page_dict_cb, outpage);
        // TODO: Handle inherited resources from parent page objects...
      }

      // Now copy the content streams to build the composite page, using the
      // resource map for any named resources...
      outpage->output = pdfio_start_page(&p, outpage->pagedict);

      for (layout = 0; layout < p.num_layout; layout ++)
        copy_page(&p, outpage, layout);

      pdfio_end_page(&p, outpage->output);
      outpage->output = NULL;
    }
  }

  // Add final job-sheets content...
  if (options->job_sheets[0] && strcmp(options->job_sheets, "none"))
    generate_job_sheets(&p);

  // Add job-error-sheet content as needed...
  if (options->job_error_sheet.report == IPPOPT_ERROR_REPORT_ALWAYS || (options->job_error_sheet.report == IPPOPT_ERROR_REPORT_ON_ERROR && cupsArrayGetCount(p.errors) > 0))
    generate_job_error_sheet(&p);

  ret = true;

  *outpages = (unsigned)pdfioFileGetNumPages(p.pdf);

  // Finalize the output and return...
  done:

  cupsArrayDelete(p.errors);

  for (i = p.num_outpages, outpage = p.outpages; i > 0; i --, outpage ++)
  {
    if (outpage->output)
      pdfioStreamClose(outpage->output);
  }

  if (!pdfioFileClose(p.pdf))
    ret = false;

  if (!ret)
  {
    // Remove temporary file...
    unlink(outfile);
    *outfile = '\0';
  }

  // Close and delete intermediate files...
  for (i = num_documents, d = documents; i > 0; i --, d ++)
  {
    pdfioFileClose(d->pdf);
    if (d->tempfile[0])
      unlink(d->tempfile);
  }

  // Return success/fail status...
  return (ret);
}


//
// 'prepare_log()' - Log an informational or error message while preparing
//                   documents for printing.
//

static void
prepare_log(xform_prepare_t *p,		// I - Preparation data
            bool            error,	// I - `true` for error, `false` for info
	    const char      *message,	// I - Printf-style message string
	    ...)			// I - Addition arguments as needed
{
  va_list	ap;			// Argument pointer
  char		buffer[1024];		// Output buffer


  va_start(ap, message);
  vsnprintf(buffer + 1, sizeof(buffer) - 1, message, ap);
  va_end(ap);

  buffer[0] = error ? 'E' : 'I';

  cupsArrayAdd(p->errors, buffer);

  if (error)
    fprintf(stderr, "ERROR: %s\n", buffer + 1);
  else
    fprintf(stderr, "INFO: %s\n", buffer + 1);
}


//
// 'prepare_number_up()' - Prepare the layout rectangles based on the number-up and orientation-requested values.
//

static void
prepare_number_up(xform_prepare_t *p)	// I - Preparation data
{
  size_t	i,			// Looping var
		cols,			// Number of columns
		rows;			// Number of rows
  pdfio_rect_t	*r;			// Current layout rectangle...
  double	width,			// Width of layout rectangle
		height;			// Height of layout rectangle


  if (!strcmp(p->options->imposition_template, "booklet"))
  {
    // "imposition-template" = 'booklet' forces 2-up output...
    p->num_layout   = 2;
    p->layout[0]    = p->media;
    p->layout[0].y2 = p->media.y2 / 2.0;
    p->layout[1]    = p->media;
    p->layout[1].y1 = p->media.y2 / 2.0;

    if (p->options->number_up != 1)
      prepare_log(p, false, "Ignoring \"number-up\" = '%d'.", p->options->number_up);

    return;
  }
  else
  {
    p->num_layout = (size_t)p->options->number_up;
  }

  // Figure out the number of rows and columns...
  switch (p->num_layout)
  {
    default : // 1-up or unknown
	if (p->options->number_up != 1)
	  prepare_log(p, false, "Ignoring \"number-up\" = '%d'.", p->options->number_up);

        p->num_layout   = 1;
        p->layout[0]    = p->crop;
        return;

    case 2 : // 2-up
        cols = 1;
        rows = 2;
        break;
    case 4 : // 4-up
        cols = 2;
        rows = 2;
        break;
    case 6 : // 6-up
        cols = 2;
        rows = 3;
        break;
    case 9 : // 9-up
        cols = 3;
        rows = 3;
        break;
    case 12 : // 12-up
        cols = 3;
        rows = 4;
        break;
    case 16 : // 16-up
        cols = 4;
        rows = 4;
        break;
  }

  // Then arrange the page rectangles evenly across the page...
  width  = (p->crop.x2 - p->crop.x1) / cols;
  height = (p->crop.y2 - p->crop.y1) / rows;

  switch (p->options->orientation_requested)
  {
    default : // Portrait or "none"...
        for (i = 0, r = p->layout; i < p->num_layout; i ++, r ++)
        {
          r->x1 = p->crop.x1 + width * (i % cols);
          r->y1 = p->crop.y1 + height * (rows - 1 - i / cols);
          r->x2 = r->x1 + width;
          r->y2 = r->y1 + height;
        }
        break;

    case IPP_ORIENT_LANDSCAPE : // Landscape
        for (i = 0, r = p->layout; i < p->num_layout; i ++, r ++)
        {
          r->x1 = p->crop.x1 + width * (cols - 1 - i / rows);
          r->y1 = p->crop.y1 + height * (rows - 1 - (i % rows));
          r->x2 = r->x1 + width;
          r->y2 = r->y1 + height;
        }
        break;

    case IPP_ORIENT_REVERSE_PORTRAIT : // Reverse portrait
        for (i = 0, r = p->layout; i < p->num_layout; i ++, r ++)
        {
          r->x1 = p->crop.x1 + width * (cols - 1 - (i % cols));
          r->y1 = p->crop.y1 + height * (i / cols);
          r->x2 = r->x1 + width;
          r->y2 = r->y1 + height;
        }
        break;

    case IPP_ORIENT_REVERSE_LANDSCAPE : // Reverse landscape
        for (i = 0, r = p->layout; i < p->num_layout; i ++, r ++)
        {
          r->x1 = p->crop.x1 + width * (i / rows);
          r->y1 = p->crop.y1 + height * (i % rows);
          r->x2 = r->x1 + width;
          r->y2 = r->y1 + height;
        }
        break;
  }
}


//
// 'prepare_pages()' - Prepare the pages for the output document.
//

static void
prepare_pages(
    xform_prepare_t  *p,		// I - Preparation data
    size_t           num_documents,	// I - Number of documents
    xform_document_t *documents)	// I - Documents
{
  int		page;			// Current page number in output
  size_t	i,			// Looping var
		current,		// Current output page index
		layout;			// Current layout cell
  xform_page_t	*outpage;		// Current output page
  xform_document_t *d;			// Current document
  bool		use_page;		// Use this page?


  if (!strcmp(p->options->imposition_template, "booklet"))
  {
    // Booklet printing arranges input pages so that the folded output can be
    // stapled along the midline...
    p->num_outpages = (size_t)(p->num_inpages + 1) / 2;
    if (p->num_outpages & 1)
      p->num_outpages ++;

    for (current = 0, layout = 0, page = 1, i = num_documents, d = documents; i > 0; i --, d ++)
    {
      while (page <= d->last_page)
      {
	if (p->options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
	  use_page = ippOptionsCheckPage(p->options, page - d->first_page + 1);
	else
	  use_page = ippOptionsCheckPage(p->options, page);

        if (use_page)
        {
	  if (current < p->num_outpages)
	    outpage = p->outpages + current;
	  else
	    outpage = p->outpages + 2 * p->num_outpages - current - 1;

	  outpage->pdf           = p->pdf;
	  outpage->input[layout] = pdfioFileGetPage(d->pdf, (size_t)(page - d->first_page));
	  layout = 1 - layout;
	  current ++;
	}

        page ++;
      }

      if (p->options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
        page = 1;
    }
  }
  else
  {
    // Normal printing lays out N input pages on each output page...
    for (current = 0, outpage = p->outpages, layout = 0, page = 1, i = num_documents, d = documents; i > 0; i --, d ++)
    {
      while (page <= d->last_page)
      {
	if (p->options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
	  use_page = ippOptionsCheckPage(p->options, page - d->first_page + 1);
	else
	  use_page = ippOptionsCheckPage(p->options, page);

        if (use_page)
        {
          outpage->pdf           = p->pdf;
          outpage->input[layout] = pdfioFileGetPage(d->pdf, (size_t)(page - d->first_page));

          if (Verbosity)
	    fprintf(stderr, "DEBUG: Using page %d (%p) of document %d, cell=%u/%u, current=%u\n", page, (void *)outpage->input[layout], (int)(d - documents + 1), (unsigned)layout + 1, (unsigned)p->num_layout, (unsigned)current);

          layout ++;
          if (layout == p->num_layout)
          {
            current ++;
            outpage ++;
            layout = 0;
          }
        }

        page ++;
      }

      if (p->options->multiple_document_handling < IPPOPT_HANDLING_SINGLE_DOCUMENT)
      {
        page = 1;

        if (layout)
        {
	  current ++;
	  outpage ++;
	  layout = 0;
        }
      }
      else if (p->options->multiple_document_handling == IPPOPT_HANDLING_SINGLE_NEW_SHEET && (current & 1))
      {
	current ++;
	outpage ++;
	layout = 0;
      }
    }

    if (layout)
      current ++;

    p->num_outpages = current;
  }
}


//
// 'raster_end_job()' - End a raster "job".
//

static void
raster_end_job(xform_raster_t   *ras,	// I - Raster information
	       xform_write_cb_t cb,	// I - Write callback
	       void             *ctx)	// I - Write context
{
  (void)cb;
  (void)ctx;

  cupsRasterClose(ras->ras);
}


//
// 'raster_end_page()' - End of raster page.
//

static void
raster_end_page(xform_raster_t   *ras,	// I - Raster information
	        unsigned         page,	// I - Current page
		xform_write_cb_t cb,	// I - Write callback
		void             *ctx)	// I - Write context
{
  (void)page;
  (void)cb;
  (void)ctx;

  if (ras->header.cupsBitsPerPixel == 1)
  {
    free(ras->out_buffer);
    ras->out_buffer = NULL;
  }
}


//
// 'raster_init()' - Initialize callbacks for raster output.
//

static void
raster_init(xform_raster_t *ras)	// I - Raster information
{
  ras->end_job    = raster_end_job;
  ras->end_page   = raster_end_page;
  ras->start_job  = raster_start_job;
  ras->start_page = raster_start_page;
  ras->write_line = raster_write_line;
}


//
// 'raster_start_job()' - Start a raster "job".
//

static void
raster_start_job(xform_raster_t   *ras,	// I - Raster information
		 xform_write_cb_t cb,	// I - Write callback
		 void             *ctx)	// I - Write context
{
  ras->ras = cupsRasterOpenIO((cups_raster_cb_t)cb, ctx, !strcmp(ras->format, "image/pwg-raster") ? CUPS_RASTER_WRITE_PWG : CUPS_RASTER_WRITE_APPLE);
}


//
// 'raster_start_page()' - Start a raster page.
//

static void
raster_start_page(xform_raster_t   *ras,// I - Raster information
		  unsigned         page,// I - Current page
		  xform_write_cb_t cb,	// I - Write callback
		  void             *ctx)// I - Write context
{
  (void)cb;
  (void)ctx;

  ras->left   = 0;
  ras->top    = 0;
  ras->right  = ras->header.cupsWidth;
  ras->bottom = ras->header.cupsHeight;

  if (ras->header.Duplex && !(page & 1))
    cupsRasterWriteHeader(ras->ras, &ras->back_header);
  else
    cupsRasterWriteHeader(ras->ras, &ras->header);

  if (ras->header.cupsBitsPerPixel == 1 || ras->header.cupsColorSpace == CUPS_CSPACE_K)
  {
    ras->out_length = ras->header.cupsBytesPerLine;
    ras->out_buffer = malloc(ras->header.cupsBytesPerLine);
  }
}


//
// 'raster_write_line()' - Write a line of raster data.
//

static void
raster_write_line(
    xform_raster_t      *ras,		// I - Raster information
    unsigned            y,		// I - Line number
    const unsigned char *line,		// I - Pixels on line
    xform_write_cb_t    cb,		// I - Write callback
    void                *ctx)		// I - Write context
{
  (void)cb;
  (void)ctx;

  if (ras->header.cupsBitsPerPixel == 1)
  {
   /*
    * Dither the line into the output buffer...
    */

    unsigned		x;		// Column number
    unsigned char	bit,		// Current bit
			byte,		// Current byte
			*outptr;	// Pointer into output buffer
    const unsigned char	*ditherline;	// Pointer into dither table

    y &= 63;
    ditherline = ras->dither[y];

    if (ras->header.cupsColorSpace == CUPS_CSPACE_SW)
    {
      for (x = ras->left, bit = 128, byte = 0, outptr = ras->out_buffer; x < ras->right; x ++, line ++)
      {
	if (*line > ditherline[x & 63])
	  byte |= bit;

	if (bit == 1)
	{
	  *outptr++ = byte;
	  byte      = 0;
	  bit       = 128;
	}
	else
	  bit >>= 1;
      }
    }
    else
    {
      for (x = ras->left, bit = 128, byte = 0, outptr = ras->out_buffer; x < ras->right; x ++, line ++)
      {
	if (*line <= ditherline[x & 63])
	  byte |= bit;

	if (bit == 1)
	{
	  *outptr++ = byte;
	  byte      = 0;
	  bit       = 128;
	}
	else
	  bit >>= 1;
      }
    }

    if (bit != 128)
      *outptr++ = byte;

    cupsRasterWritePixels(ras->ras, ras->out_buffer, ras->header.cupsBytesPerLine);
  }
  else if (ras->header.cupsColorSpace == CUPS_CSPACE_K)
  {
    unsigned		x;		// Column number
    unsigned char	*outptr;	// Pointer into output buffer

    for (x = ras->left, outptr = ras->out_buffer; x < ras->right; x ++)
      *outptr++ = 255 - *line++;

    cupsRasterWritePixels(ras->ras, ras->out_buffer, ras->header.cupsBytesPerLine);
  }
  else
  {
    cupsRasterWritePixels(ras->ras, (unsigned char *)line, ras->header.cupsBytesPerLine);
  }
}


//
// 'resource_dict_cb()' - Merge resource dictionaries from multiple input pages.
//
// This function detects resource conflicts and maps conflicting names as
// needed.
//

static bool
resource_dict_cb(
    pdfio_dict_t *dict,			// I - Dictionary
    const char   *key,			// I - Dictionary key
    xform_page_t *outpage)		// I - Output page
{
  pdfio_array_t	*arrayval;		// Array value
  pdfio_dict_t	*dictval;		// Dictionary value
  const char	*nameval,		// Name value
		*curname;		// Current name value
  pdfio_obj_t	*objval;		// Object value
  char		mapname[256];		// Mapped resource name


  fprintf(stderr, "DEBUG: resource_dict_cb(dict=%p, key=\"%s\", outpage=%p)\n", (void *)dict, key, (void *)outpage);

  snprintf(mapname, sizeof(mapname), "%c%s", (int)('a' + outpage->layout), key);

  switch (pdfioDictGetType(dict, key))
  {
    case PDFIO_VALTYPE_ARRAY : // Array
        arrayval = pdfioDictGetArray(dict, key);
        if (pdfioDictGetArray(outpage->restype, key))
        {
	  if (!outpage->resmap[outpage->layout])
	    outpage->resmap[outpage->layout] = pdfioDictCreate(outpage->pdf);

	  pdfioDictSetName(outpage->resmap[outpage->layout], pdfioStringCreate(outpage->pdf, key), pdfioStringCreate(outpage->pdf, mapname));
	  key = mapname;
	}

        pdfioDictSetArray(outpage->restype, pdfioStringCreate(outpage->pdf, key), pdfioArrayCopy(outpage->pdf, arrayval));
        break;

    case PDFIO_VALTYPE_DICT : // Dictionary
        dictval = pdfioDictGetDict(dict, key);
        if (pdfioDictGetDict(outpage->restype, key))
        {
	  if (!outpage->resmap[outpage->layout])
	    outpage->resmap[outpage->layout] = pdfioDictCreate(outpage->pdf);

	  pdfioDictSetName(outpage->resmap[outpage->layout], pdfioStringCreate(outpage->pdf, key), pdfioStringCreate(outpage->pdf, mapname));
	  key = mapname;
	}

        pdfioDictSetDict(outpage->restype, pdfioStringCreate(outpage->pdf, key), pdfioDictCopy(outpage->pdf, dictval));
        break;

    case PDFIO_VALTYPE_NAME : // Name
        nameval = pdfioDictGetName(dict, key);
        if ((curname = pdfioDictGetName(outpage->restype, key)) != NULL)
        {
          if (!strcmp(nameval, curname))
            break;

	  if (!outpage->resmap[outpage->layout])
	    outpage->resmap[outpage->layout] = pdfioDictCreate(outpage->pdf);

	  pdfioDictSetName(outpage->resmap[outpage->layout], pdfioStringCreate(outpage->pdf, key), pdfioStringCreate(outpage->pdf, mapname));
	  key = mapname;
	}

        pdfioDictSetName(outpage->restype, pdfioStringCreate(outpage->pdf, key), pdfioStringCreate(outpage->pdf, nameval));
        break;

    case PDFIO_VALTYPE_INDIRECT : // Object reference
        objval = pdfioDictGetObj(dict, key);
        if (pdfioDictGetObj(outpage->restype, key))
        {
	  if (!outpage->resmap[outpage->layout])
	    outpage->resmap[outpage->layout] = pdfioDictCreate(outpage->pdf);

	  pdfioDictSetName(outpage->resmap[outpage->layout], pdfioStringCreate(outpage->pdf, key), pdfioStringCreate(outpage->pdf, mapname));
	  key = mapname;
	}

        pdfioDictSetObj(outpage->restype, pdfioStringCreate(outpage->pdf, key), pdfioObjCopy(outpage->pdf, objval));
        break;

    default :
        break;
  }

  return (true);
}


//
// 'size_to_rect()' - Convert `cups_size_t` to `pdfio_rect_t` for media and crop boxes.
//

static void
size_to_rect(cups_size_t  *size,	// I - CUPS media size information
             pdfio_rect_t *media,	// O - PDF MediaBox value
             pdfio_rect_t *crop)	// O - PDF CropBox value
{
  // cups_size_t uses hundredths of millimeters, pdf_rect_t uses points...
  media->x1 = 0.0;
  media->y1 = 0.0;
  media->x2 = 72.0 * size->width / 2540.0;
  media->y2 = 72.0 * size->length / 2540.0;

  crop->x1  = 72.0 * size->left / 2540.0;
  crop->y1  = 72.0 * size->bottom / 2540.0;
  crop->x2  = 72.0 * (size->width - size->right) / 2540.0;
  crop->y2  = 72.0 * (size->length - size->top) / 2540.0;
}


//
// 'usage()' - Show program usage.
//

static void
usage(int status)			// I - Exit status
{
  puts("Usage: ipptransform [options] filename\n");
  puts("Options:");
  puts("  --help");
  puts("  -d device-uri");
  puts("  -f output-filename");
  puts("  -i input/format");
  puts("  -m output/format");
  puts("  -o \"name=value [... name=value]\"");
  puts("  -r resolution[,...,resolution]");
  puts("  -s {flipped|manual-tumble|normal|rotated}");
  puts("  -t type[,...,type]");
  puts("  -v\n");
  puts("Device URIs: socket://address[:port], ipp://address[:port]/resource, ipps://address[:port]/resource");
  puts("Input Formats: application/pdf, image/jpeg, image/png, image/pwg-raster, text/plain");
  puts("Output Formats: application/pdf, application/vnd.hp-pcl, image/pwg-raster, image/urf");
  puts("Options: copies, force-front-side, image-orientation, imposition-template, insert-sheet, job-error-sheet, job-pages-per-set, job-sheet-message, job-sheets, job-sheets-col, media, media-col, multiple-document-handling, number-up, orientation-requested, overrides, page-delivery, page-ranges, print-color-mode, print-quality, print-scaling, printer-resolution, separator-sheets, sides, x-image-position, x-image-shift, x-side1-image-shift, x-side2-image-position, y-image-position, y-image-shift, y-side1-image-shift, y-side2-image-position");
  puts("Resolutions: NNNdpi or NNNxNNNdpi");
#ifdef HAVE_COREGRAPHICS
  puts("Types: adobe-rgb_8, adobe-rgb_16, black_1, black_8, cmyk_8, sgray_1, sgray_8, srgb_8");
#else
  puts("Types: black_1, black_8, sgray_1, sgray_8, srgb_8");
#endif // HAVE_COREGRAPHICS

  exit(status);
}


//
// 'write_fd()' - Write to a file/socket.
//

static ssize_t				// O - Number of bytes written or -1 on error
write_fd(int                 *fd,	// I - File descriptor
         const unsigned char *buffer,	// I - Buffer
         size_t              bytes)	// I - Number of bytes to write
{
  ssize_t	temp,			// Temporary byte count
		total = 0;		// Total bytes written


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



#ifdef HAVE_COREGRAPHICS
//
// 'xform_document()' - Transform a file for printing.
//

static bool				// O - `true` on success, `false` on error
xform_document(
    const char       *filename,		// I - File to transform
    unsigned         pages,		// I - Number of pages
    ipp_options_t    *options,		// I - IPP options
    const char       *outformat,	// I - Output format (MIME media type)
    const char       *resolutions,	// I - Supported resolutions
    const char       *sheet_back,	// I - Back side transform
    const char       *types,		// I - Supported types
    xform_write_cb_t cb,		// I - Write callback
    void             *ctx)		// I - Write context
{
  CFURLRef		url;		// CFURL object for PDF filename
  CGPDFDocumentRef	document = NULL;// Input document
  CGPDFPageRef		pdf_page;	// Page in PDF file
  xform_raster_t	ras;		// Raster info
  size_t		max_raster;	// Maximum raster memory to use
  const char		*max_raster_env;// IPPTRANSFORM_MAX_RASTER env var
  size_t		bpc;		// Bits per color
  CGColorSpaceRef	cs;		// Quartz color space
  CGContextRef		context;	// Quartz bitmap context
  CGBitmapInfo		info;		// Bitmap flags
  size_t		band_size;	// Size of band line
  double		xscale, yscale;	// Scaling factor
  CGAffineTransform 	transform;	// Transform for page
  CGRect		dest;		// Destination rectangle
  bool			color = true;	// Does the PDF have color?
  unsigned		copy;		// Current copy
  unsigned		page;		// Current page
  unsigned		media_sheets = 0,
			impressions = 0;// Page/sheet counters


  // Open the file...
  if ((url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)filename, (CFIndex)strlen(filename), false)) == NULL)
  {
    fputs("ERROR: Unable to create CFURL for file.\n", stderr);
    return (false);
  }

  // Open the PDF...
  document = CGPDFDocumentCreateWithURL(url);
  CFRelease(url);

  if (!document)
  {
    fputs("ERROR: Unable to create CFPDFDocument for file.\n", stderr);
    return (false);
  }

  // Setup the raster context...
  if (!xform_setup(&ras, options, outformat, resolutions, sheet_back, types, color, pages))
  {
    CGPDFDocumentRelease(document);

    return (false);
  }

  if (ras.header.cupsBitsPerPixel <= 8)
  {
    // Grayscale output...
    ras.band_bpp = 1;
    info         = kCGImageAlphaNone;
    cs           = CGColorSpaceCreateWithName(ras.header.cupsColorSpace == CUPS_CSPACE_SW ? kCGColorSpaceGenericGrayGamma2_2 : kCGColorSpaceLinearGray);
    bpc          = 8;
  }
  else if (ras.header.cupsBitsPerPixel == 24)
  {
    // Color (sRGB or AdobeRGB) output...
    ras.band_bpp = 4;
    info         = kCGImageAlphaNoneSkipLast;
    cs           = CGColorSpaceCreateWithName(ras.header.cupsColorSpace == CUPS_CSPACE_SRGB ? kCGColorSpaceSRGB : kCGColorSpaceAdobeRGB1998);
    bpc          = 8;
  }
  else if (ras.header.cupsBitsPerPixel == 32)
  {
    // Color (CMYK) output...
    ras.band_bpp = 4;
    info         = kCGImageAlphaNone;
    cs           = CGColorSpaceCreateWithName(kCGColorSpaceGenericCMYK);
    bpc          = 8;
  }
  else
  {
    // Color (AdobeRGB) output...
    ras.band_bpp = 8;
    info         = kCGImageAlphaNoneSkipLast;
    cs           = CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
    bpc          = 16;
  }

  max_raster     = XFORM_MAX_RASTER;
  max_raster_env = getenv("IPPTRANSFORM_MAX_RASTER");
  if (max_raster_env && strtol(max_raster_env, NULL, 10) > 0)
    max_raster = (size_t)strtol(max_raster_env, NULL, 10);

  band_size = ras.header.cupsWidth * ras.band_bpp;
  if ((ras.band_height = (unsigned)(max_raster / band_size)) < 1)
    ras.band_height = 1;
  else if (ras.band_height > ras.header.cupsHeight)
    ras.band_height = ras.header.cupsHeight;

  ras.band_buffer = malloc(ras.band_height * band_size);
  context         = CGBitmapContextCreate(ras.band_buffer, ras.header.cupsWidth, ras.band_height, bpc, band_size, cs, info);

  CGColorSpaceRelease(cs);

  // Don't anti-alias or interpolate when creating raster data
  CGContextSetAllowsAntialiasing(context, 0);
  CGContextSetInterpolationQuality(context, kCGInterpolationNone);

  xscale = ras.header.HWResolution[0] / 72.0;
  yscale = ras.header.HWResolution[1] / 72.0;

  if (Verbosity > 1)
    fprintf(stderr, "DEBUG: xscale=%g, yscale=%g\n", xscale, yscale);
  CGContextScaleCTM(context, xscale, yscale);

  if (Verbosity > 1)
    fprintf(stderr, "DEBUG: Band height=%u, page height=%u, page translate 0.0,%g\n", ras.band_height, ras.header.cupsHeight, -1.0 * (ras.header.cupsHeight - ras.band_height) / yscale);
  CGContextTranslateCTM(context, 0.0, -1.0 * (ras.header.cupsHeight - ras.band_height) / yscale);

  dest.origin.x    = dest.origin.y = 0.0;
  dest.size.width  = ras.header.cupsWidth * 72.0 / ras.header.HWResolution[0];
  dest.size.height = ras.header.cupsHeight * 72.0 / ras.header.HWResolution[1];

  // Start the conversion...
  fprintf(stderr, "ATTR: job-impressions=%d\n", pages);
  fprintf(stderr, "ATTR: job-pages=%d\n", pages);

  if (ras.header.Duplex)
    fprintf(stderr, "ATTR: job-media-sheets=%d\n", (pages + 1) / 2);
  else
    fprintf(stderr, "ATTR: job-media-sheets=%d\n", pages);

  if (Verbosity > 1)
    fprintf(stderr, "DEBUG: cupsPageSize=[%g %g]\n", ras.header.cupsPageSize[0], ras.header.cupsPageSize[1]);

  (ras.start_job)(&ras, cb, ctx);

  // Render pages in the PDF...
  for (copy = 0; copy < options->copies; copy ++)
  {
    // Write a separator sheet as needed...
    switch (options->separator_type)
    {
      case IPPOPT_SEPTYPE_NONE :
      case IPPOPT_SEPTYPE_END_SHEET :
          break;

      case IPPOPT_SEPTYPE_SLIP_SHEETS :
          if (copy == 0)
            break;

      case IPPOPT_SEPTYPE_START_SHEET :
      case IPPOPT_SEPTYPE_BOTH_SHEETS :
          xform_separator(&ras, cb, ctx);
          break;
    }

    // Render pages in the PDF...
    for (page = 1; page <= pages; page ++)
    {
      unsigned		y,		// Current line
			band_starty = 0,// Start line of band
			band_endy = 0;	// End line of band
      unsigned char	*lineptr;	// Pointer to line

      pdf_page  = CGPDFDocumentGetPage(document, page);
      transform = CGPDFPageGetDrawingTransform(pdf_page, kCGPDFCropBox,dest, 0, true);

      if (Verbosity > 1)
	fprintf(stderr, "DEBUG: Printing copy %d/%d, page %d/%d, transform=[%g %g %g %g %g %g]\n", copy + 1, options->copies, page, pages, transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty);

      (ras.start_page)(&ras, page, cb, ctx);

      for (y = ras.top; y < ras.bottom; y ++)
      {
	if (y >= band_endy)
	{
	  // Draw the next band of raster data...
	  band_starty = y;
	  band_endy   = y + ras.band_height;
	  if (band_endy > ras.bottom)
	    band_endy = ras.bottom;

	  if (Verbosity > 1)
	    fprintf(stderr, "DEBUG: Drawing band from %u to %u.\n", band_starty, band_endy);

	  CGContextSaveGState(context);
	    if (ras.header.cupsNumColors == 1)
	      CGContextSetGrayFillColor(context, 1., 1.);
	    else
	      CGContextSetRGBFillColor(context, 1., 1., 1., 1.);

	    CGContextSetCTM(context, CGAffineTransformIdentity);
	    CGContextFillRect(context, CGRectMake(0., 0., ras.header.cupsWidth, ras.band_height));
	  CGContextRestoreGState(context);

	  CGContextSaveGState(context);
	    if (Verbosity > 1)
	      fprintf(stderr, "DEBUG: Band translate 0.0,%g\n", y / yscale);
	    CGContextTranslateCTM(context, 0.0, y / yscale);
	    CGContextConcatCTM(context, transform);

	    CGContextClipToRect(context, CGPDFPageGetBoxRect(pdf_page, kCGPDFCropBox));
	    CGContextDrawPDFPage(context, pdf_page);
	  CGContextRestoreGState(context);
	}

        // Prepare and write a line...
	lineptr = ras.band_buffer + (y - band_starty) * band_size + ras.left * ras.band_bpp;
	if (ras.header.cupsBitsPerPixel == 24)
	  pack_rgba(lineptr, ras.right - ras.left);
	else if (ras.header.cupsBitsPerPixel == 48)
	  pack_rgba16(lineptr, ras.right - ras.left);

	(ras.write_line)(&ras, y, lineptr, cb, ctx);
      }

      (ras.end_page)(&ras, page, cb, ctx);

      // Log progress...
      impressions ++;
      fprintf(stderr, "ATTR: job-impressions-completed=%u\n", impressions);
      if (!ras.header.Duplex || !(page & 1))
      {
	media_sheets ++;
	fprintf(stderr, "ATTR: job-media-sheets-completed=%u\n", media_sheets);
      }
    }

    if (options->copies > 1 && (pages & 1) && ras.header.Duplex)
    {
      // Duplex printing, add a blank back side image...
      unsigned	y;		// Current line

      if (Verbosity > 1)
	fprintf(stderr, "DEBUG: Printing blank page %u for duplex.\n", pages + 1);

      memset(ras.band_buffer, 255, ras.header.cupsBytesPerLine);

      (ras.start_page)(&ras, page, cb, ctx);

      for (y = ras.top; y < ras.bottom; y ++)
	(ras.write_line)(&ras, y, ras.band_buffer, cb, ctx);

      (ras.end_page)(&ras, page, cb, ctx);

      impressions ++;
      fprintf(stderr, "ATTR: job-impressions-completed=%u\n", impressions);
      if (!ras.header.Duplex || !(page & 1))
      {
	media_sheets ++;
	fprintf(stderr, "ATTR: job-media-sheets-completed=%u\n", media_sheets);
      }
    }

    // Write a separator sheet as needed...
    switch (options->separator_type)
    {
      case IPPOPT_SEPTYPE_NONE :
      case IPPOPT_SEPTYPE_START_SHEET :
      case IPPOPT_SEPTYPE_SLIP_SHEETS :
	  break;

      case IPPOPT_SEPTYPE_END_SHEET :
      case IPPOPT_SEPTYPE_BOTH_SHEETS :
          xform_separator(&ras, cb, ctx);
          break;
    }
  }

  CGPDFDocumentRelease(document);
  (ras.end_job)(&ras, cb, ctx);

  // Clean up...
  CGContextRelease(context);

  free(ras.band_buffer);
  ras.band_buffer = NULL;

  return (true);
}


#else
//
// 'xform_document()' - Transform a file for printing.
//

static bool				// O - `true` on success, `false` on error
xform_document(
    const char       *filename,		// I - File to transform
    unsigned         pages,		// I - Number of pages
    ipp_options_t    *options,		// I - IPP options
    const char       *outformat,	// I - Output format (MIME media type)
    const char       *resolutions,	// I - Supported resolutions
    const char       *sheet_back,	// I - Back side transform
    const char       *types,		// I - Supported types
    xform_write_cb_t cb,		// I - Write callback
    void             *ctx)		// I - Write context
{
  xform_raster_t ras;			// Raster information
  unsigned	copy,			// Current copy
		page = 0,		// Current page
		media_sheets = 0,
		impressions = 0;	// Page/sheet counters
  char		command[1024];		// pdftoppm command
  FILE		*fp;			// Pipe for output
  char		header[256];		// Header from file
  unsigned char	*line;			// Pixel line from file
  size_t	linesize;		// Size of a line...


  // Setup the raster headers...
  if (!xform_setup(&ras, options, outformat, resolutions, sheet_back, types, true, pages))
    return (false);

  if (ras.header.cupsBitsPerPixel <= 8)
    linesize = ras.header.cupsWidth;
  else
    linesize = 3 * ras.header.cupsWidth;

  if ((line = malloc(linesize)) == NULL)
  {
    fprintf(stderr, "ERROR: Unable to allocate %u bytes for line.\n", (unsigned)linesize);
    return (false);
  }

  fprintf(stderr, "ATTR: job-impressions=%d\n", pages);
  fprintf(stderr, "ATTR: job-pages=%d\n", pages);

  if (ras.header.Duplex)
    fprintf(stderr, "ATTR: job-media-sheets=%d\n", (pages + 1) / 2);
  else
    fprintf(stderr, "ATTR: job-media-sheets=%d\n", pages);

  if (Verbosity > 1)
    fprintf(stderr, "DEBUG: cupsPageSize=[%g %g]\n", ras.header.cupsPageSize[0], ras.header.cupsPageSize[1]);

  (ras.start_job)(&ras, cb, ctx);

  for (copy = 0; copy < options->copies; copy ++)
  {
    // Write a separator sheet as needed...
    switch (options->separator_type)
    {
      case IPPOPT_SEPTYPE_NONE :
      case IPPOPT_SEPTYPE_END_SHEET :
          break;

      case IPPOPT_SEPTYPE_SLIP_SHEETS :
          if (copy == 0)
            break;

      case IPPOPT_SEPTYPE_START_SHEET :
      case IPPOPT_SEPTYPE_BOTH_SHEETS :
          xform_separator(&ras, cb, ctx);
          break;
    }

    // Run the pdftoppm command:
    //
    //   pdftoppm [-mono] [-gray] -aa no -r resolution filename -
    snprintf(command, sizeof(command), "pdftoppm %s -aa no -r %u '%s' -", ras.header.cupsBitsPerPixel <= 8 ? "-gray" : "", ras.header.HWResolution[0], filename);
    fprintf(stderr, "DEBUG: Running \"%s\"\n", command);
#if _WIN32
    if ((fp = popen(command, "rb")) == NULL)
#else
    if ((fp = popen(command, "r")) == NULL)
#endif // _WIN32
    {
      fprintf(stderr, "ERROR: Unable to run pdftoppm command: %s\n", strerror(errno));
      free(line);
      return (false);
    }

    // Read lines from the file...
    while (fgets(header, sizeof(header), fp))
    {
      // Got the P4/5/6 header...
      unsigned	y;			// Current Y position
      unsigned	width, height;		// Width and height from image...

      if (strcmp(header, "P4\n") && strcmp(header, "P5\n") && strcmp(header, "P6\n"))
      {
        fprintf(stderr, "ERROR: Bad page header - <%02X%02X%02X%02X%02X%02X%02X%02X>", header[0] & 255, header[1] & 255, header[2] & 255, header[3] & 255, header[4] & 255, header[5] & 255, header[6] & 255, header[7] & 255);
        break;
      }

      // Now get the bitmap dimensions...
      if (!fgets(header, sizeof(header), fp))
        break;

      if (sscanf(header, "%u%u", &width, &height) != 2)
      {
        fprintf(stderr, "ERROR: Bad page dimensions - <%02X%02X%02X%02X%02X%02X%02X%02X>", header[0] & 255, header[1] & 255, header[2] & 255, header[3] & 255, header[4] & 255, header[5] & 255, header[6] & 255, header[7] & 255);
        break;
      }
      else if (width != ras.header.cupsWidth || height != ras.header.cupsHeight)
      {
        fprintf(stderr, "ERROR: Unexpected page dimensions - %ux%u instead of %ux%u.\n", width, height, ras.header.cupsWidth, ras.header.cupsHeight);
        break;
      }

      // Skip max value line...
      if (!fgets(header, sizeof(header), fp))
        break;

      // Send the page to the driver...
      page ++;

      (ras.start_page)(&ras, page, cb, ctx);

      for (y = 0; y < height; y ++)
      {
        if (fread(line, linesize, 1, fp))
          (ras.write_line)(&ras, y, line, cb, ctx);
      }

      (ras.end_page)(&ras, page, cb, ctx);

      // Log progress...
      impressions ++;
      fprintf(stderr, "ATTR: job-impressions-completed=%u\n", impressions);
      if (!ras.header.Duplex || !(page & 1))
      {
	media_sheets ++;
	fprintf(stderr, "ATTR: job-media-sheets-completed=%u\n", media_sheets);
      }
    }

    // Close things out...
    pclose(fp);

    // Write a separator sheet as needed...
    switch (options->separator_type)
    {
      case IPPOPT_SEPTYPE_NONE :
      case IPPOPT_SEPTYPE_START_SHEET :
      case IPPOPT_SEPTYPE_SLIP_SHEETS :
	  break;

      case IPPOPT_SEPTYPE_END_SHEET :
      case IPPOPT_SEPTYPE_BOTH_SHEETS :
          xform_separator(&ras, cb, ctx);
          break;
    }
  }

  (ras.end_job)(&ras, cb, ctx);

  free(line);

  return (true);
}
#endif // HAVE_COREGRAPHICS


//
// 'xform_separator()' - Write a separator sheet.
//

static bool				// O - `true` on success, `false` on failure
xform_separator(xform_raster_t   *ras,	// I - Raster information
                xform_write_cb_t cb,	// I - Write callback
                void             *ctx)	// I - Callback data
{
  bool		ret = false;		// Return value
  unsigned	i,			// Current page
		count,			// Number of pages
		y;			// Current line on page
  unsigned char	*line;			// Line for page(s)
  size_t	linesize;		// Size of a line...


  // Allocate memory for line on separator page...
  if (ras->sep_header.cupsBitsPerPixel <= 8)
    linesize = ras->sep_header.cupsWidth;
  else
    linesize = 3 * ras->sep_header.cupsWidth;

  if ((line = malloc(linesize)) == NULL)
    goto done;

  // Clear to white...
  switch (ras->sep_header.cupsColorSpace)
  {
    default :
        goto done;

    case CUPS_CSPACE_W :
    case CUPS_CSPACE_RGB :
    case CUPS_CSPACE_SW :
    case CUPS_CSPACE_SRGB :
    case CUPS_CSPACE_ADOBERGB :
        memset(line, 0xff, linesize);
        break;

    case CUPS_CSPACE_K :
    case CUPS_CSPACE_CMYK :
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
        memset(line, 0, linesize);
        break;
  }

  // Produce 1 or 2 blank pages...
  count = ras->sep_header.Duplex ? 2 : 1;

  for (i = 0; i < count; i ++)
  {
    (ras->start_page)(ras, i + 1, cb, ctx);

    for (y = 0; y < ras->sep_header.cupsHeight; y ++)
      (ras->write_line)(ras, y, line, cb, ctx);

    (ras->end_page)(ras, i + 1, cb, ctx);
  }

  ret = true;

  done:

  free(line);

  return (ret);
}


//
// 'xform_setup()' - Setup a raster context for printing.
//

static bool				// O - `true` on success, `false` on error
xform_setup(xform_raster_t *ras,	// I - Raster information
            ipp_options_t  *options,	// I - IPP options
            const char     *format,	// I - Output format (MIME media type)
	    const char     *resolutions,// I - Supported resolutions
	    const char     *sheet_back,	// I - Back side transform
	    const char     *types,	// I - Supported types
	    bool           color,	// I - Document contains color?
            unsigned       pages)	// I - Number of pages
{
  const char	*sides,			// Final "sides" value
		*type = NULL;		// Raster type to use
  cups_array_t	*res_array,		// Resolutions in array
		*type_array;		// Types in array


  // Initialize raster information...
  memset(ras, 0, sizeof(xform_raster_t));

  ras->format = format;

  if (!strcmp(format, "application/vnd.hp-pcl"))
    pcl_init(ras);
  else
    raster_init(ras);

  // Figure out the proper resolution, etc.
  if (!options->printer_resolution[0])
  {
    // Choose a supported resolution from the list...
    const char	*printer_resolution;	// Printer resolution string
    int		xdpi, ydpi;		// X/Y resolution values (DPI)

    res_array = cupsArrayNewStrings(resolutions, ',');

    switch (options->print_quality)
    {
      case IPP_QUALITY_DRAFT :
	  printer_resolution = cupsArrayGetElement(res_array, 0);
	  break;

      default :
      case IPP_QUALITY_NORMAL :
	  printer_resolution = cupsArrayGetElement(res_array, cupsArrayGetCount(res_array) / 2);
	  break;

      case IPP_QUALITY_HIGH :
	  printer_resolution = cupsArrayGetElement(res_array, cupsArrayGetCount(res_array) - 1);
	  break;
    }

    // Parse the "printer-resolution" value...
    if (sscanf(printer_resolution, "%dx%ddpi", &xdpi, &ydpi) != 2)
    {
      if (sscanf(printer_resolution, "%ddpi", &xdpi) == 1)
      {
	ydpi = xdpi;
      }
      else
      {
	fprintf(stderr, "ERROR: Bad resolution value '%s'.\n", printer_resolution);
	return (false);
      }
    }

    options->printer_resolution[0] = xdpi;
    options->printer_resolution[1] = ydpi;

    cupsArrayDelete(res_array);
  }

  // Now figure out the color space to use...
  if (!strcmp(options->print_color_mode, "monochrome") || !strcmp(options->print_color_mode, "process-monochrome") || !strcmp(options->print_color_mode, "auto-monochrome"))
  {
    color = false;
  }
  else if (!strcmp(options->print_color_mode, "bi-level") || !strcmp(options->print_color_mode, "process-bi-level"))
  {
    color = false;
    options->print_quality = IPP_QUALITY_DRAFT;
  }

  type_array = cupsArrayNewStrings(types, ',');

  if (color)
  {
    if (options->print_quality == IPP_QUALITY_HIGH)
    {
#ifdef HAVE_COREGRAPHICS
      if (cupsArrayFind(type_array, "adobe-rgb_16"))
	type = "adobe-rgb_16";
      else if (cupsArrayFind(type_array, "adobe-rgb_8"))
	type = "adobe-rgb_8";
#endif // HAVE_COREGRAPHICS
    }

    if (!type && cupsArrayFind(type_array, "srgb_8"))
      type = "srgb_8";
    if (!type && cupsArrayFind(type_array, "cmyk_8"))
      type = "cmyk_8";
  }

  if (!type)
  {
    if (options->print_quality == IPP_QUALITY_DRAFT)
    {
      if (cupsArrayFind(type_array, "black_1"))
	type = "black_1";
      else if (cupsArrayFind(type_array, "sgray_1"))
	type = "sgray_1";
    }
    else
    {
      if (cupsArrayFind(type_array, "black_8"))
	type = "black_8";
      else if (cupsArrayFind(type_array, "sgray_8"))
	type = "sgray_8";
    }
  }

  if (!type)
  {
    // No type yet, find any of the supported formats...
    if (cupsArrayFind(type_array, "black_8"))
      type = "black_8";
    else if (cupsArrayFind(type_array, "sgray_8"))
      type = "sgray_8";
    else if (cupsArrayFind(type_array, "black_1"))
      type = "black_1";
    else if (cupsArrayFind(type_array, "sgray_1"))
      type = "sgray_1";
    else if (cupsArrayFind(type_array, "srgb_8"))
      type = "srgb_8";
#ifdef HAVE_COREGRAPHICS
    else if (cupsArrayFind(type_array, "adobe-rgb_8"))
      type = "adobe-rgb_8";
    else if (cupsArrayFind(type_array, "adobe-rgb_16"))
      type = "adobe-rgb_16";
#endif // HAVE_COREGRAPHICS
    else if (cupsArrayFind(type_array, "cmyk_8"))
      type = "cmyk_8";
  }

  cupsArrayDelete(type_array);

  if (!type)
  {
    fputs("ERROR: No supported raster types are available.\n", stderr);
    return (false);
  }

  // Initialize the raster header...
  if (pages == 1)
    sides = "one-sided";
  else
    sides = options->sides;

  if (options->copies > 1 && (pages & 1) && strcmp(sides, "one-sided"))
    pages ++;

  if (!cupsRasterInitHeader(&ras->header, &options->media, options->print_content_optimize, options->print_quality, options->print_rendering_intent, options->orientation_requested, sides, type, options->printer_resolution[0], options->printer_resolution[1], NULL))
  {
    fprintf(stderr, "ERROR: Unable to initialize raster context: %s\n", cupsRasterErrorString());
    return (false);
  }

  if (options->separator_type != IPPOPT_SEPTYPE_NONE)
  {
    if (!cupsRasterInitHeader(&ras->sep_header, &options->separator_media, options->print_content_optimize, options->print_quality, options->print_rendering_intent, options->orientation_requested, sides, type, options->printer_resolution[0], options->printer_resolution[1], NULL))
    {
      fprintf(stderr, "ERROR: Unable to initialize separator raster context: %s\n", cupsRasterErrorString());
      return (false);
    }
  }

  if (pages > 1)
  {
    if (!cupsRasterInitHeader(&ras->back_header, &options->media, options->print_content_optimize, options->print_quality, options->print_rendering_intent, options->orientation_requested, sides, type, options->printer_resolution[0], options->printer_resolution[1], sheet_back))
    {
      fprintf(stderr, "ERROR: Unable to initialize back side raster context: %s\n", cupsRasterErrorString());
      return (false);
    }
  }

  if (ras->header.cupsBitsPerPixel == 1)
  {
    if (!strcmp(options->print_color_mode, "bi-level") || !strcmp(options->print_color_mode, "process-bi-level"))
      memset(ras->dither, 127, sizeof(ras->dither));
    else
      memcpy(ras->dither, threshold, sizeof(ras->dither));
  }

  ras->header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount]      = (unsigned)options->copies * pages;
  ras->back_header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = (unsigned)options->copies * pages;

  if (Verbosity)
  {
    fprintf(stderr, "DEBUG: cupsColorSpace=%u\n", ras->header.cupsColorSpace);
    fprintf(stderr, "DEBUG: cupsBitsPerColor=%u\n", ras->header.cupsBitsPerColor);
    fprintf(stderr, "DEBUG: cupsBitsPerPixel=%u\n", ras->header.cupsBitsPerPixel);
    fprintf(stderr, "DEBUG: cupsNumColors=%u\n", ras->header.cupsNumColors);
    fprintf(stderr, "DEBUG: cupsWidth=%u\n", ras->header.cupsWidth);
    fprintf(stderr, "DEBUG: cupsHeight=%u\n", ras->header.cupsHeight);
  }

  return (true);
}

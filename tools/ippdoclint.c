/*
 * ippdoclint utility for checking common print file formats.
 *
 * Copyright 2018 by the IEEE-ISTO Printer Working Group.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include <config.h>
#include <stdio.h>
#include <cups/cups.h>
#include <cups/raster.h>
#include <cups/string-private.h>
#include <netinet/in.h>
#include <mupdf/fitz.h>
#include <stdlib.h>
#include <pthread.h>
#include <jpeglib.h>

/*
 * Local globals...
 */

static int	Verbosity = 0;		/* Log level */


/*
 * Local functions...
 */

static int	lint_jpeg(const char *filename, int num_options, cups_option_t *options);
static int	lint_pdf(const char *filename, int num_options, cups_option_t *options);
static int	lint_raster(const char *filename, int num_options, cups_option_t *options);
static int	load_env_options(cups_option_t **options);
static void	usage(int status) __attribute__((noreturn));


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
      else if (!strcmp(opt, ".ras"))
        content_type = "application/vnd.cups-raster";
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
    return (lint_jpeg(filename, num_options, options));
  }
  else if (!strcmp(content_type, "application/pdf"))
  {
    return (lint_pdf(filename, num_options, options));
  }
  else if (!strcmp(content_type, "application/vnd.cups-raster") || !strcmp(content_type, "image/pwg-raster") || !strcmp(content_type, "image/urf"))
  {
    return (lint_raster(filename, num_options, options));
  }
  else
  {
    fprintf(stderr, "ERROR: Unsupported format \"%s\" for \"%s\".\n", content_type, filename);
    usage(1);
  }
}

struct jpeg_lint_error_mgr {
    struct jpeg_error_mgr pub;    /* "public" fields */
    jmp_buf setjmp_buffer;        /* for return to caller */
};
typedef struct jpeg_lint_error_mgr *jpeg_lint_error_ptr;

void jpeg_lint_error_exit (j_common_ptr cinfo) {
  jpeg_lint_error_ptr error = (jpeg_lint_error_ptr) cinfo->err;
  (*cinfo->err->output_message) (cinfo);
  longjmp(error->setjmp_buffer, 1);
}

/*
 * 'lint_jpeg()' - Check a JPEG file.
 */
static int				/* O - 0 if OK, 1 if not OK */
lint_jpeg(const char    *filename,	/* I - File to check */
          int           num_options,	/* I - Number of options */
          cups_option_t *options)	/* I - Options */
{
 /*
  * TODO: Check that the file opens, write STATE messages for
  * document-format-error and document-unprintable-error, and write ATTR lines
  * for the following Job attributes:
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
  */

  /*
   * This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
  struct jpeg_decompress_struct cinfo;
  struct jpeg_lint_error_mgr jerr;

  FILE *infile;                 /* source file */
  JSAMPARRAY buffer;            /* Output row buffer */
  int row_stride;               /* physical row width in output buffer */

  if ((infile = fopen(filename, "rb")) == NULL) {
    fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
    return(1);
  }

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpeg_lint_error_exit;

  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    return(1);
  }

  fprintf(stderr, "DEBUG: Setting up...\n");
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, infile);

  // TODO: Print header info to stderr
  (void) jpeg_read_header(&cinfo, TRUE);

  fprintf(stderr, "DEBUG: Decompressing the file...\n");
  (void) jpeg_start_decompress(&cinfo);

  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray) ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  fprintf(stderr, "DEBUG: Scanning rows in JPEG...\n");
  while (cinfo.output_scanline < cinfo.output_height) {
    jpeg_read_scanlines(&cinfo, buffer, 1);
  }
  fprintf(stderr, "DEBUG: Scan complete\n");

  fprintf(stderr, "DEBUG: Cleaning up...\n");
  (void) jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(infile);

  if(jerr.pub.num_warnings != 0) {
    // TODO See if the error messages corresponding to the non-zero warnings are shown
    fprintf(stderr, "ERROR: Corrupt data found\n");
    return(1);
  }
  else {
    fprintf(stderr, "DEBUG: File lint complete, no errors found\n");
  }

  (void)num_options;
  (void)options;

  return(0);
}


static int
mupdf_exit(fz_context *context,
  fz_document *document)
{
  if(document)
    fz_drop_document(context, document);
  if(context)
    fz_drop_context(context);
}

typedef struct _thread_data {
  fz_context *context;
  int pagenumber;
  fz_display_list *list;
  fz_rect bbox;
  fz_pixmap *pix;
} thread_data;

void *
renderer(void *data)
{
  int pagenumber = ((thread_data *) data)->pagenumber;
  fz_context *context = ((thread_data *) data)->context;
  fz_display_list *list = ((thread_data *) data)->list;
  fz_rect bbox = ((thread_data *) data)->bbox;
  fz_pixmap *pix = ((thread_data *) data)->pix;
  fz_device *dev;

  //fprintf(stderr, "DEBUG: Thread at page %d loading\n", pagenumber);

  context = fz_clone_context(context);

  //fprintf(stderr, "DEBUG: Thread at page %d rendering\n", pagenumber);
  dev = fz_new_draw_device(context, &fz_identity, pix);
  fz_run_display_list(context, list, dev, &fz_identity, &bbox, NULL);
  fz_close_device(context, dev);
  fz_drop_device(context, dev);

  fz_drop_context(context);

  //fprintf(stderr, "DEBUG: Thread at page %d done\n", pagenumber);

  return data;
}

void lock_mutex(void *user, int lock)
{
  pthread_mutex_t *mutex = (pthread_mutex_t *) user;

  if (pthread_mutex_lock(&mutex[lock]) != 0) {
    fprintf(stderr, "ERROR: Failed to lock mutex\n");
    abort();
  }
}

void unlock_mutex(void *user, int lock)
{
  pthread_mutex_t *mutex = (pthread_mutex_t *) user;

  if (pthread_mutex_unlock(&mutex[lock]) != 0){
    fprintf(stderr, "ERROR: Failed to unlock mutex\n");
    abort();
  }
}

/*
 * 'lint_pdf()' - Check a PDF file.
 */

static int				/* O - 0 if OK, 1 if not OK */
lint_pdf(const char    *filename,	/* I - File to check */
	 int           num_options,	/* I - Number of options */
	 cups_option_t *options)	/* I - Options */
{
 /*
  * TODO: Check that the file opens, write STATE messages for
  * document-format-error and document-unprintable-error, and write ATTR lines
  * for the following Job attributes:
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
  */

  fz_context *context = NULL;
  fz_document *document = NULL;
  int page_count;
  pthread_t *thread = NULL;
  fz_locks_context locks;
  pthread_mutex_t mutex[FZ_LOCK_MAX];
  int num_threads;
  int i;

  /* Initialize FZ_LOCK_MAX number of non-recursive mutexes */
  for (i = 0; i < FZ_LOCK_MAX; i++)
  {
    if (pthread_mutex_init(&mutex[i], NULL) != 0){
      fprintf(stderr, "ERROR: Failed to initialize mmutex\n");
      return(1);
    }
  }

  locks.user = mutex;
  locks.lock = lock_mutex;
  locks.unlock = unlock_mutex;

  /* Create a context to hold the exception stack and various caches. */
  context = fz_new_context(NULL, &locks, FZ_STORE_UNLIMITED);
  if (!context)
  {
    fprintf(stderr, "ERROR: Failed to create a mupdf context\n");
    mupdf_exit(context, document);
    return(1);
  }
  fprintf(stderr, "DEBUG: Created a mupdf context\n");

  /* Register the default file types to handle. */
  fz_try(context)
    fz_register_document_handlers(context);
  fz_catch(context)
  {
    fprintf(stderr, "ERROR: Failed to register document handlers: %s\n", fz_caught_message(context));
    mupdf_exit(context, document);
    return(1);
  }
  fprintf(stderr, "DEBUG: Registered mupdf document handlers\n");

  /* Open the document. */
  fz_try(context)
    document = fz_open_document(context, filename);
  fz_catch(context)
  {
    fprintf(stderr, "ERROR: Failed to open the document: %s\n", fz_caught_message(context));
    mupdf_exit(context, document);
    return(1);
  }
  fprintf(stderr, "DEBUG: Opened the document using mupdf\n");

  /* Metadata retrieval */

  char buffer[100]; // To temporarily store the data
  int size; // To temporarily store the size of the result

  size = fz_lookup_metadata(context, document, "format", buffer, 100);
  fprintf(stderr, "DEBUG: Format : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "encryption", buffer, 100);
  fprintf(stderr, "DEBUG: Encryption : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Title", buffer, 100);
  fprintf(stderr, "DEBUG: Title : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Author", buffer, 100);
  fprintf(stderr, "DEBUG: Author : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Subject", buffer, 100);
  fprintf(stderr, "DEBUG: Subject : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Keywords", buffer, 100);
  fprintf(stderr, "DEBUG: Keywords : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Creator", buffer, 100);
  fprintf(stderr, "DEBUG: Creator : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  size = fz_lookup_metadata(context, document, "info:Producer", buffer, 100);
  fprintf(stderr, "DEBUG: Producer : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  // [TODO] Make dates human readable
  size = fz_lookup_metadata(context, document, "info:CreationDate", buffer, 100);
  fprintf(stderr, "DEBUG: Creation Date : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  // [TODO] Make dates human readable
  size = fz_lookup_metadata(context, document, "info:ModDate", buffer, 100);
  fprintf(stderr, "DEBUG: Modification Date : %s\n", (size != -1 && buffer[0] != '\0') ? buffer : "Not available");

  char *colorspace = fz_colorspace_name(context, fz_document_output_intent(context, document));
  fprintf(stderr, "DEBUG: Output intent colorspace : %s\n", colorspace[0]!='\0' ? colorspace : "Not available" );

  

  /* Count the number of pages. */
  fz_try(context)
    page_count = fz_count_pages(context, document);
  fz_catch(context)
  {
    fprintf(stderr, "ERROR: Failed to count the number of pages: %s\n", fz_caught_message(context));
    mupdf_exit(context, document);
    return(1);
  }
  if(page_count <= 0){
    fprintf(stderr, "ERROR: Corrupt PDF file. Number of pages %d\n", page_count);
    mupdf_exit(context, document);
    return(1);
  }
  fprintf(stderr, "DEBUG: The document has %d pages\n", page_count);

  num_threads = page_count; // [TODO] Put a cap on max number of threads
  fprintf(stderr, "DEBUG: Spawning %d threads, one per page\n", num_threads);

  thread = malloc(num_threads * sizeof (pthread_t));

  for (i = 0; i < num_threads; i++)
  {
    fz_page *page;
    fz_rect bbox;
    fz_irect rbox;
    fz_display_list *list;
    fz_device *dev;
    fz_pixmap *pix;
    thread_data *data;

    page = fz_load_page(context, document, i);
    fz_bound_page(context, page, &bbox);
    list = fz_new_display_list(context, &bbox);

    dev = fz_new_list_device(context, list);
    fz_run_page(context, page, dev, &fz_identity, NULL);
    fz_close_device(context, dev);
    fz_drop_device(context, dev);

    fz_drop_page(context, page);

    pix = fz_new_pixmap_with_bbox(context, fz_device_rgb(context), fz_round_rect(&rbox, &bbox), NULL, 0);
    fz_clear_pixmap_with_value(context, pix, 0xff);

    data = malloc(sizeof (thread_data));
    data->pagenumber = i + 1;
    data->context = context;
    data->list = list;
    data->bbox = bbox;
    data->pix = pix;

    if (pthread_create(&thread[i], NULL, renderer, data) != 0){
      fprintf(stderr, "ERROR: Failed to create a pthread\n");
      return(1);
    }
  }

  fprintf(stderr, "DEBUG: Joining %d threads\n", num_threads);
  for (i = num_threads - 1; i >= 0; i--)
  {
    thread_data *data;

    if (pthread_join(thread[i], (void **) &data) != 0){
      fprintf(stderr, "ERROR: Failed to join threads\n");
      return(1);
    }

    fz_drop_pixmap(context, data->pix);
    fz_drop_display_list(context, data->list);
    free(data);
  }

  fprintf(stderr, "DEBUG: PDF linting completed without any errors\n");
  fflush(NULL);

  free(thread);
  mupdf_exit(context, document);

  (void)num_options;
  (void)options;

  return(0);
}

char when_enum[5][20] = {
  "Never",
  "AfterDocument",
  "AfterJob",
  "AfterSet",
  "AfterPage"
};

char media_position_enum[50][20] = {
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

char orientation_enum[4][20] = {
  "Portrait",
  "Landscape",
  "ReversePortrait",
  "ReverseLandscape"
};

char print_quality_enum[4][10] = {
  "Default",
  "Draft",
  "Normal",
  "High"
};

static int
parse_pwg_header(cups_page_header2_t *header)
{
  if(strncmp(header->MediaClass, "PwgRaster", 64)){
    fprintf(stderr, "ERROR: PwgRaster value in header is incorrect\n");
    return(1);
  }
  fprintf(stderr, "DEBUG: Header value PwgRaster is correct\n");
  
  if (header->MediaColor[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for MediaColor\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for MediaColor", header->MediaColor);
  
  if (header->MediaType[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for MediaType\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for MediaType", header->MediaType);

  if (header->OutputType[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for PrintContentOptimize\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for PrintContentOptimize", header->OutputType);

  if (header->AdvanceMedia != 0 || header->Collate != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[256-267] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[256-267] field is zero as expected\n");

  if (header->CutMedia < 0 || header->CutMedia > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for CutMedia\n");
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of CutMedia is %d(%s)\n", header->CutMedia, when_enum[header->CutMedia]);

  if(header->Duplex == 0)
    fprintf(stderr, "DEBUG: Duplex mode off\n");
  else if(header->Duplex == 1)
    fprintf(stderr, "DEBUG: Duplex mode on\n");
  else
    fprintf(stderr, "DEBUG: Incorrect Duplex value\n");

  header->HWResolution[0] = ntohl(header->HWResolution[0]);
  header->HWResolution[1] = ntohl(header->HWResolution[1]);
  fprintf(stderr, "DEBUG: Using cross-feed resolution of %u and feed resolution of %u\n", header->HWResolution[0], header->HWResolution[1]);

  for(int i=0; i<4; i++){
    header->ImagingBoundingBox[i] = ntohl(header->ImagingBoundingBox[i]);
    if(header->ImagingBoundingBox[i]!=0){
      // Non-critical
      fprintf(stderr, "WARNING: Non-zero values present in Reserved[284-299] area\n");
      break;
    }
  }
  fprintf(stderr, "DEBUG: Reserved[284-299] field is zero as expected\n");
  
  if(header->InsertSheet == 0)
    fprintf(stderr, "DEBUG: InsertSheet set to false\n");
  else if(header->InsertSheet == 1)
    fprintf(stderr, "DEBUG: InsertSheet set to true\n");
  else
    fprintf(stderr, "DEBUG: Incorrect InsertSheet value\n");

  if (header->Jog < 0 || header->Jog > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for Jog %d\n", header->Jog);
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of Jog is %d(%s)\n", header->Jog, when_enum[header->Jog]);
  
  if(header->LeadingEdge == 0)
    fprintf(stderr, "DEBUG: LeadingEdge set to ShortEdgeFirst\n");
  else if(header->LeadingEdge == 1)
    fprintf(stderr, "DEBUG: LeadingEdge set to LongEdgeFirst\n");
  else
    fprintf(stderr, "DEBUG: Incorrect LeadingEdge value\n");

  if (header->Margins[0] != 0 || header->Margins[1] != 0 || header->ManualFeed != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[312-323] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[312-323] field is zero as expected\n");

  if (header->MediaPosition < 0 || header->MediaPosition > 49) {
    fprintf(stderr, "ERROR: Incorrect value present for MediaPosition\n");
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of MediaPosition is %d(%s)\n", header->MediaPosition, media_position_enum[header->MediaPosition]);

  if (header->MediaWeight==0)
    fprintf(stderr, "DEBUG: Using default value for MediaWeight\n");
  else 
    fprintf(stderr, "DEBUG: Using value %d for MediaWeight", header->MediaWeight);

  if (header->MirrorPrint != 0 || header->NegativePrint != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[332-339] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[332-339] field is zero as expected\n");

  header->NumCopies = ntohl(header->NumCopies);
  if (header->NumCopies==0)
    fprintf(stderr, "DEBUG: Using default value for NumCopies\n");
  else 
    fprintf(stderr, "DEBUG: Using value %d for NumCopies\n", header->NumCopies);

  if (header->Orientation < 0 || header->Orientation > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for Orientation %d\n", header->Orientation);
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of Orientation is %d(%s)\n", header->Orientation, orientation_enum[header->Orientation]);
  
  if (header->OutputFaceUp != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[348-351] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[348-351] field is zero as expected\n");

  header->PageSize[0] = ntohl(header->PageSize[0]);
  header->PageSize[1] = ntohl(header->PageSize[1]);
  fprintf(stderr, "DEBUG: Page size is %d x %d\n", header->PageSize[0], header->PageSize[1]);

  if (header->Separations != 0 || header->TraySwitch != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[360-367] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[360-367] field is zero as expected\n");

  if(header->Tumble == 0)
    fprintf(stderr, "DEBUG: Tumble set to false\n");
  else if(header->Tumble == 1)
    fprintf(stderr, "DEBUG: Tumble set to true\n");
  else
    fprintf(stderr, "DEBUG: Incorrect Tumble value\n");

  header->cupsWidth = ntohl(header->cupsWidth);
  header->cupsHeight = ntohl(header->cupsHeight);
  fprintf(stderr, "DEBUG: Page width is %d and height is %d\n", header->cupsWidth, header->cupsHeight);

  if (header->cupsMediaType != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[380-383] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[380-383] field is zero as expected\n");

  header->cupsBitsPerColor = ntohl(header->cupsBitsPerColor);
  switch (header->cupsBitsPerColor) {
    case 1: break;
    case 8: break;
    case 16: break;
    default:
      fprintf(stderr, "ERROR: Incorrect BitsPerColor value present %d\n", header->cupsBitsPerColor);
      return(1);
  }
  fprintf(stderr, "DEBUG: BitsPerColor value is %d\n", header->cupsBitsPerColor);

  header->cupsBitsPerPixel = ntohl(header->cupsBitsPerPixel);
  switch (header->cupsBitsPerPixel) { // [TODO] Much more checks needed
    case 1: break;
    case 8: break;
    case 16: break;
    case 24: break;
    case 32: break;
    case 40: break;
    case 48: break;
    case 56: break;
    case 64: break;
    case 72: break;
    case 80: break;
    case 88: break;
    case 96: break;
    case 104: break;
    case 112: break;
    case 120: break;
    case 128: break;
    case 144: break;
    case 160: break;
    case 176: break;
    case 192: break;
    case 208: break;
    case 224: break;
    case 240: break;
    default:
      fprintf(stderr, "ERROR: Incorrect BitsPerPixel value present %d\n", header->cupsBitsPerPixel);
      return(1);
  }
  fprintf(stderr, "DEBUG: BitsPerPixel value is %d\n", header->cupsBitsPerPixel);

  header->cupsBytesPerLine = ntohl(header->cupsBytesPerLine);
  if (header->cupsBytesPerLine==(header->cupsBitsPerPixel * header->cupsWidth + 7)/8)
    fprintf(stderr, "DEBUG: BytesPerLine value is correct %d\n", header->cupsBytesPerLine);
  else {
    fprintf(stderr, "ERROR: BytesPerLine value is incorrect %d\n", header->cupsBytesPerLine);
    return(1);
  }

  if (header->cupsColorOrder==0)
    fprintf(stderr, "DEBUG: ColorOrder value is correct %d\n", header->cupsColorOrder);
  else {
    fprintf(stderr, "ERROR: ColorOrder value is incorrect %d\n", header->cupsColorOrder);
    return(1);
  }

  header->cupsColorSpace = ntohl(header->cupsColorSpace);
  switch(header->cupsColorSpace) { // [TODO] Much more checks needed
    case 1: break;
    case 3: break;
    case 6: break;
    case 18: break;
    case 19: break;
    case 20: break;
    case 48: break;
    case 49: break;
    case 50: break;
    case 51: break;
    case 52: break;
    case 53: break;
    case 54: break;
    case 55: break;
    case 56: break;
    case 57: break;
    case 58: break;
    case 59: break;
    case 60: break;
    case 61: break;
    case 62: break;
    default:
      fprintf(stderr, "ERROR: Incorrect ColorSpace value present %d\n", header->cupsColorSpace);
      return(1);
  }
  fprintf(stderr, "DEBUG: ColorSpace value is %d\n", header->cupsColorSpace);

  if (header->cupsCompression != 0 || header->cupsRowCount != 0 || header->cupsRowFeed != 0 || header->cupsRowStep != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[404-419] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[404-419] field is zero as expected\n");

  header->cupsNumColors = ntohl(header->cupsNumColors);
  switch(header->cupsNumColors) { // [TODO] Much more checks needed
    case 1: break;
    case 2: break;
    case 3: break;
    case 4: break;
    case 5: break;
    case 6: break;
    case 7: break;
    case 8: break;
    case 9: break;
    case 10: break;
    case 11: break;
    case 12: break;
    case 13: break;
    case 14: break;
    case 15: break;
    default:
      fprintf(stderr, "ERROR: Incorrect NumColors value present %d\n", header->cupsNumColors);
      return(1);
  }
  fprintf(stderr, "DEBUG: NumColors value is %d\n", header->cupsNumColors);

  if (header->cupsBorderlessScalingFactor != 0 || header->cupsPageSize[0] != 0 || header->cupsPageSize[1] != 0 || 
      header->cupsImagingBBox[0] != 0 || header->cupsImagingBBox[1] != 0 || header->cupsImagingBBox[2] != 0 || header->cupsImagingBBox[3] != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[424-451] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[424-451] field is zero as expected\n");

  header->cupsInteger[0] = ntohl(header->cupsInteger[0]);
  if(header->cupsInteger[0]==0)
    fprintf(stderr, "DEBUG: TotalPageCount is not known when the file is produced\n");
  else if(header->cupsInteger[0]>0) 
    fprintf(stderr, "DEBUG: TotalPageCount is %d\n", header->cupsInteger[0]);
  else {
    fprintf(stderr, "ERROR: TotalPageCount is incorrect %d\n", header->cupsInteger[0]);
    return(1);
  }

  header->cupsInteger[1] = (int32_t)(header->cupsInteger[1]);
  if(header->cupsInteger[1]==1 || header->cupsInteger[1]==-1)
    fprintf(stderr, "DEBUG: CrossFeedTransform value is %d\n", header->cupsInteger[1]);
  else {
    // Non-critical
    fprintf(stderr, "WARNING: CrossFeedTransform is incorrect %d\n", header->cupsInteger[1]);
  }

  header->cupsInteger[2] = (int32_t)(header->cupsInteger[2]);
  if(header->cupsInteger[2]==1 || header->cupsInteger[2]==-1)
    fprintf(stderr, "DEBUG: FeedTransform value is %d\n", header->cupsInteger[2]);
  else {
    fprintf(stderr, "WARNING: FeedTransform is incorrect %d\n", header->cupsInteger[2]);
    //return(1);
  }

  for(int i=0; i<4; i++)
    header->cupsInteger[3+i] = ntohl(header->cupsInteger[3+i]);
  fprintf(stderr, "DEBUG: ImageBoxLeft value is %d\n", header->cupsInteger[3]);
  fprintf(stderr, "DEBUG: ImageBoxTop value is %d\n", header->cupsInteger[4]);
  fprintf(stderr, "DEBUG: ImageBoxBottom value is %d\n", header->cupsInteger[5]);
  fprintf(stderr, "DEBUG: ImageBoxRight value is %d\n", header->cupsInteger[6]);

  header->cupsInteger[7] = ntohl(header->cupsInteger[7]);
  if(header->cupsInteger[7] >=0 && header->cupsInteger[7] < (1 << 25))
    fprintf(stderr, "DEBUG: AlternatePrimary value is %d\n", header->cupsInteger[7]);
  else
    fprintf(stderr, "DEBUG: AlternatePrimary value is incorrect %d\n", header->cupsInteger[7]);

  header->cupsInteger[8] = ntohl(header->cupsInteger[8]);
  if(header->cupsInteger[8] == 0)
    fprintf(stderr, "DEBUG: PrintQuality value is %d(%s)\n", header->cupsInteger[8], print_quality_enum[header->cupsInteger[8]]);
  else if(header->cupsInteger[8] >=3 && header->cupsInteger[8] < 6)
    fprintf(stderr, "DEBUG: PrintQuality value is %d(%s)\n", header->cupsInteger[8], print_quality_enum[header->cupsInteger[8]-2]);
  else
    fprintf(stderr, "DEBUG: PrintQuality value is incorrect %d\n", header->cupsInteger[8]);

  for(int i=9; i<14; i++)
    if(header->cupsInteger[i]!=0){
      fprintf(stderr, "ERROR: Non-zero values present in Reserved[488-507] area\n");
      return(1);
    }
  fprintf(stderr, "DEBUG: Reserved[488-507] field is zero as expected\n");

  for(int i=0; i<2; i++)
    header->cupsInteger[14+i] = ntohl(header->cupsInteger[14+i]);
  fprintf(stderr, "DEBUG: VendorIdentifier value is %d\n", header->cupsInteger[14]);
  fprintf(stderr, "DEBUG: VendorLength value is %d\n", header->cupsInteger[15]);

  if(header->cupsInteger[14]==0 || header->cupsInteger[15]==0)
    fprintf(stderr, "DEBUG: No vendor information present\n");
  else
    fprintf(stderr, "DEBUG: VendorData is %s\n", header->cupsReal);

  for(int i=0; i<64; i++){
    if(header->cupsMarkerType[i]!=0){
      fprintf(stderr, "ERROR: Non-zero values present in Reserved[1604-1667] area\n");
      return(1);
    }
  }
  fprintf(stderr, "DEBUG: Reserved[1604-1667] field is zero as expected\n");
 
  if(header->cupsRenderingIntent[0] == '\0')
    fprintf(stderr, "DEBUG: Using default value for RenderingIntent\n");
  else
    fprintf(stderr, "DEBUG: RenderingIntent is %s\n", header->cupsRenderingIntent);
  
  fprintf(stderr, "DEBUG: PageSizeName is %s\n", header->cupsPageSizeName);

  return(0);
}

static int
traverse_pwg_bitmap(FILE *file, 
  cups_page_header2_t *header)
{
  fprintf(stderr, "DEBUG: Started checking bitmap for ColorSpace %d\n", header->cupsColorSpace);
  int height_count = 0;
  int width_count = 0;
  uint8_t *buffer;
  uint8_t *buffer2;
  while(1){
    width_count = 0;
    buffer = malloc(1);
    fread(buffer, 1, 1, file);
    height_count += *buffer + 1;
    if(height_count > header->cupsHeight) {
      if((header->cupsBitsPerPixel == 1 && abs(height_count - *buffer - 1 - header->cupsHeight) < 8) || height_count - *buffer - 1 == header->cupsHeight)
        fprintf(stderr, "DEBUG: Traversed bitmap and found no errors\n");
      else
        fprintf(stderr, "ERROR: Bitmap height mismatch with height in the header-> Expected height: %d. Bitmap height: %d\n", header->cupsHeight, height_count - *buffer - 1);
      fseek(file, -1, SEEK_CUR);
      break;
    }
    while(width_count < header->cupsWidth){
      fread(buffer, 1, 1, file);
      int unit_size = header->cupsBitsPerPixel == 1 ? 1 : (header->cupsBitsPerPixel / 8);
      if(*buffer > 127){ // Non-repeating colors
        buffer2 = malloc(unit_size * (257 - *buffer));
        fread(buffer2, unit_size, (257 - *buffer), file);
        width_count += header->cupsBitsPerPixel == 1 ? (257 - *buffer) * 8 : (257 - *buffer);
      }
      else{ // Repeating colors
        buffer2 = malloc(unit_size * 1);
        fread(buffer2, unit_size, 1, file);
        width_count += header->cupsBitsPerPixel == 1 ? (*buffer + 1) * 8 : (*buffer + 1);
      }
      free(buffer2);
    }
    if(width_count != header->cupsWidth){
      if(header->cupsBitsPerPixel == 1 && abs(width_count - header->cupsWidth) < 8)
        continue;
      fprintf(stderr, "ERROR: Bitmap width didn't match specified Width value in the header-> Expected: %d, Found: %d\n", header->cupsWidth, width_count);
      return(1);
    }
    free(buffer);
  }

  return(0);
}

/*
 * 'lint_raster()' - Check an Apple/CUPS/PWG Raster file.
 */

static int				/* O - 0 if OK, 1 if not OK */
lint_raster(const char    *filename,	/* I - File to check */
	    int           num_options,	/* I - Number of options */
	    cups_option_t *options)	/* I - Options */
{
 /*
  * TODO: Check that the file opens, write STATE messages for
  * document-format-error and document-unprintable-error, and write ATTR lines
  * for the following Job attributes:
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
  */

  FILE *file = fopen(filename, "rb");
  rewind(file);
  
  char sync_word[4];
  fread(sync_word, 4, 1, file);

  if(strncmp(sync_word, "RaS2", 4)){
    fprintf(stderr, "ERROR: Synchronization word mismatch\n");
    return (1);
  }
  fprintf(stderr, "DEBUG: Synchronization word is correct\n");

  cups_page_header2_t header;
  int total_page_count = 0;
  if(!total_page_count){
    fread(&header, 4, 449, file);
    int ret = parse_pwg_header(&header);
    if(ret)
      return(1);
    ret = traverse_pwg_bitmap(file, &header);
    if(ret)
      return(1);
    total_page_count = header.cupsInteger[0];
    for(int i=1; i<total_page_count; i++){
      fread(&header, 4, 449, file);
      ret = parse_pwg_header(&header);
      if(ret)
        return(1);
      ret = traverse_pwg_bitmap(file, &header);
      if(ret)
        return(1);
    }
  }
  
  (void)num_options;
  (void)options;

  fclose(file);
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

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

  (void)filename;
  (void)num_options;
  (void)options;

  return (1);
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

  (void)filename;
  (void)num_options;
  (void)options;

  return (1);
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
  fscanf(file, "%4s", sync_word);
  
  if(strcmp(sync_word, "RaS2")){
    fprintf(stderr, "ERROR: Synchronization word mismatch\n");
    return (1);
  }
  fprintf(stderr, "DEBUG: Synchronization word is correct\n");
  
  cups_page_header2_t header;
  fread(&header, 1, 1796, file);
  
  if(strncmp(header.MediaClass, "PwgRaster", 64)){
    fprintf(stderr, "ERROR: PwgRaster value in header is incorrect\n");
    return (1);
  }
  fprintf(stderr, "DEBUG: Header value PwgRaster is correct\n");
  
  if (header.MediaColor[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for MediaColor\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for MediaColor", header.MediaColor);
  
  if (header.MediaType[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for MediaType\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for MediaType", header.MediaType);

  if (header.OutputType[0]=='\0')
    fprintf(stderr, "DEBUG: Using default value for PrintContentOptimize\n");
  else 
    fprintf(stderr, "DEBUG: Using value %s for PrintContentOptimize", header.OutputType);

  if (header.AdvanceMedia != 0 || header.Collate != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[256-267] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[256-267] field is zero as expected\n");

  if (header.CutMedia < 0 || header.CutMedia > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for CutMedia\n");
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of CutMedia is %d(%s)\n", header.CutMedia, when_enum[header.CutMedia]);

  if(header.Duplex == 0)
    fprintf(stderr, "DEBUG: Duplex mode off\n");
  else if(header.Duplex == 1)
    fprintf(stderr, "DEBUG: Duplex mode on\n");
  else
    fprintf(stderr, "DEBUG: Incorrect Duplex value\n");

  /* [TODO]: Not working properly */
  fprintf(stderr, "DEBUG: Using cross-feed resolution of %u and feed resolution of %u\n", header.HWResolution[0], header.HWResolution[1]);

  /* [TODO]: Not working properly */
  for(int i=0; i<4; i++){
    if(header.ImagingBoundingBox[i]!=0){
      fprintf(stderr, "ERROR: Non-zero values present in Reserved[284-299] area\n");
      //return(1);
    }
  }
  fprintf(stderr, "DEBUG: Reserved[284-299] field is zero as expected\n");
  
  if(header.InsertSheet == 0)
    fprintf(stderr, "DEBUG: InsertSheet set to false\n");
  else if(header.InsertSheet == 1)
    fprintf(stderr, "DEBUG: InsertSheet set to true\n");
  else
    fprintf(stderr, "DEBUG: Incorrect InsertSheet value\n");

  if (header.Jog < 0 || header.Jog > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for Jog %d\n", header.Jog);
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of Jog is %d(%s)\n", header.Jog, when_enum[header.Jog]);
  
  if(header.LeadingEdge == 0)
    fprintf(stderr, "DEBUG: LeadingEdge set to ShortEdgeFirst\n");
  else if(header.LeadingEdge == 1)
    fprintf(stderr, "DEBUG: LeadingEdge set to LongEdgeFirst\n");
  else
    fprintf(stderr, "DEBUG: Incorrect LeadingEdge value\n");

  if (header.Margins[0] != 0 || header.Margins[1] != 0 || header.ManualFeed != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[312-323] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[312-323] field is zero as expected\n");

  if (header.MediaPosition < 0 || header.MediaPosition > 49) {
    fprintf(stderr, "ERROR: Incorrect value present for MediaPosition\n");
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of MediaPosition is %d(%s)\n", header.MediaPosition, media_position_enum[header.MediaPosition]);

  if (header.MediaWeight==0)
    fprintf(stderr, "DEBUG: Using default value for MediaWeight\n");
  else 
    fprintf(stderr, "DEBUG: Using value %d for MediaWeight", header.MediaWeight);

  if (header.MirrorPrint != 0 || header.NegativePrint != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[332-339] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[332-339] field is zero as expected\n");

  /* [TODO]: Not working properly */
  if (header.NumCopies==0)
    fprintf(stderr, "DEBUG: Using default value for NumCopies\n");
  else 
    fprintf(stderr, "DEBUG: Using value %d for NumCopies\n", header.NumCopies);

  if (header.Orientation < 0 || header.Orientation > 4) {
    fprintf(stderr, "ERROR: Incorrect value present for Orientation %d\n", header.Orientation);
    return(1);
  }
  fprintf(stderr, "DEBUG: Value of Orientation is %d(%s)\n", header.Orientation, orientation_enum[header.Orientation]);
  
  if (header.OutputFaceUp != 0){
    fprintf(stderr, "ERROR: Non-zero values present in Reserved[348-351] area\n");
    return(1);
  }
  else
    fprintf(stderr, "DEBUG: Reserved[348-351] field is zero as expected\n");

  /* [TODO]: Not working properly */ 
  fprintf(stderr, "DEBUG: Page size is %d x %d\n", header.PageSize[0], header.PageSize[1]);

  (void)num_options;
  (void)options;

  fclose(file);
  return (1);
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

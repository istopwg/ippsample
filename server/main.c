/*
 * Main entry for IPP Infrastructure Printer sample implementation.
 *
 * Copyright 2010-2016 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#define _MAIN_C_
#include "ippserver.h"


/*
 * Local functions...
 */

static void		usage(int status) __attribute__((noreturn));


/*
 * 'main()' - Main entry to the sample infrastructure server.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  char		*opt,			/* Current option character */
		*authtype = NULL,	/* Type of authentication */
		*confdir = NULL,	/* Configuration directory */
                *command = NULL,	/* Command to run with job files */
		*device_uri = NULL,	/* Device URI */
		*name = NULL,		/* Printer name */
		*location = (char *)"",	/* Location of printer */
		*make = (char *)"Test",	/* Manufacturer */
		*model = (char *)"Printer",
					/* Model */
		*icon = (char *)"printer.png",
					/* Icon file */
		*formats = (char *)"application/pdf,image/jpeg,image/pwg-raster";
	      				/* Supported formats */
  int		duplex = 0,		/* Duplex mode */
		ppm = 10,		/* Pages per minute for mono */
		ppm_color = 0,		/* Pages per minute for color */
		pin = 0;		/* PIN printing mode? */
  char		*proxy_user = NULL;	/* Proxy username */
  server_printer_t *printer;		/* Printer object */
  ipp_t		*attrs = NULL;		/* Extra printer attributes */


 /*
  * Parse command-line arguments...
  */

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
          case '2' : /* -2 (enable 2-sided printing) */
              duplex = 1;
              break;

          case 'C' : /* -C config-directory */
              i ++;
              if (i >= argc)
                usage(1);

              confdir = argv[i];
              break;

#ifdef HAVE_SSL
	  case 'K' : /* -K keypath */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      KeychainPath = strdup(argv[i]);
	      break;
#endif /* HAVE_SSL */

	  case 'M' : /* -M manufacturer */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      make = argv[i];
	      break;

          case 'P' : /* -P (PIN printing mode) */
              pin = 1;
              break;

	  case 'a' : /* -a attributes-file */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      attrs = serverLoadAttributes(argv[i], &authtype, &command, &device_uri, &make, &model, &proxy_user);
	      break;

          case 'c' : /* -c command */
              i ++;
	      if (i >= argc)
	        usage(1);

	      command = argv[i];
	      break;

	  case 'd' : /* -d data-directory */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      DataDirectory = strdup(argv[i]);
	      break;

	  case 'f' : /* -f type/subtype[,...] */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      formats = argv[i];
	      break;

          case 'h' : /* -h (show help) */
	      usage(0);

	  case 'k' : /* -k (keep files) */
	      KeepFiles = 1;
	      break;

	  case 'i' : /* -i icon.png */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      icon = argv[i];
	      break;

	  case 'l' : /* -l location */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      location = argv[i];
	      break;

	  case 'm' : /* -m model */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      model = argv[i];
	      break;

	  case 'n' : /* -n hostname */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      ServerName = strdup(argv[i]);
	      break;

	  case 'p' : /* -p port */
	      i ++;
	      if (i >= argc || !isdigit(argv[i][0] & 255))
	        usage(1);

	      DefaultPort = atoi(argv[i]);
	      break;

	  case 'r' : /* -r subtype */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      DNSSDSubType = strdup(argv[i]);
	      break;

          case 's' : /* -s speed[,color-speed] */
              i ++;
              if (i >= argc)
                usage(1);
              if (sscanf(argv[i], "%d,%d", &ppm, &ppm_color) < 1)
                usage(1);
              break;

          case 'u' : /* -u user:pass */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      proxy_user = argv[i];
	      break;

	  case 'v' : /* -v (be verbose) */
	      LogLevel ++;
	      break;

          default : /* Unknown */
	      fprintf(stderr, "ippserver: Unknown option \"-%c\".\n", *opt);
	      usage(1);
	}
      }
    }
    else if (!name)
    {
      name = argv[i];
    }
    else
    {
      fprintf(stderr, "ippserver: Unexpected command-line argument \"%s\"\n", argv[i]);
      usage(1);
    }
  }

  if (!DNSSDSubType)
    DNSSDSubType = strdup("_print");

  if (!name && !confdir)
    usage(1);
  else if (confdir)
  {
   /*
    * Load the configuration from the specified directory...
    */

    if (!serverLoadConfiguration(confdir))
      return (1);
  }
  else
  {
   /*
    * Create a single printer (backwards-compatibility mode)...
    */

    serverLog(SERVER_LOGLEVEL_INFO, "Using default configuration with a single printer.");

    if (!serverFinalizeConfiguration())
      return (1);

    if ((printer = serverCreatePrinter("/ipp/print", name, location, make, model, icon, formats, ppm, ppm_color, duplex, pin, attrs, command, device_uri, proxy_user)) == NULL)
      return (1);

    Printers = cupsArrayNew(NULL, NULL);
    cupsArrayAdd(Printers, printer);
  }

 /*
  * Enter the server main loop...
  */

  serverRun();

  return (0);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(int status)			/* O - Exit status */
{
  if (!status)
  {
    puts(CUPS_SVERSION " - Copyright 2010-2015 by Apple Inc. All rights reserved.");
    puts("");
  }

  puts("Usage: ippserver [options] \"name\"");
  puts("");
  puts("Options:");
//  puts("-2                      Supports 2-sided printing (default=1-sided)");
  puts("-C config-directory     Load settings and printers from the specified directory.");
  puts("-M manufacturer         Manufacturer name (default=Test)");
  puts("-P                      PIN printing mode");
  puts("-a attributes-file      Load printer attributes from file");
  puts("-c command              Run command for every print job");
  printf("-d data-directory       Data/spool directory "
         "(default=$TMPDIR/ippserver.%d)\n", (int)getpid());
  puts("-f type/subtype[,...]   List of supported types "
       "(default=application/pdf,image/jpeg)");
  puts("-h                      Show program help");
  puts("-i iconfile.png         PNG icon file (default=printer.png)");
  puts("-k                      Keep job spool files");
  puts("-l location             Location of printer (default=empty string)");
  puts("-m model                Model name (default=Printer)");
  puts("-n hostname             Hostname for printer");
  puts("-p port                 Port number (default=auto)");
  puts("-r subtype              Bonjour service subtype (default=_print)");
  puts("-s speed[,color-speed]  Speed in pages per minute (default=10,0)");
  puts("-v[v]                   Be (very) verbose");

  exit(status);
}

/*
 * Main entry for IPP Infrastructure Printer sample implementation.
 *
 * Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#define _MAIN_C_
#include "ippserver.h"


/*
 * Local functions...
 */

static void		usage(int status) _CUPS_NORETURN;


/*
 * 'main()' - Main entry to the sample infrastructure server.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  char		*opt,			/* Current option character */
		*confdir = NULL,	/* Configuration directory */
		*name = NULL;		/* Printer name */
  server_pinfo_t pinfo;			/* Printer information */
  server_printer_t *printer;		/* Printer object */


 /*
  * Parse command-line arguments...
  */

  memset(&pinfo, 0, sizeof(pinfo));
  pinfo.print_group = SERVER_GROUP_NONE;
  pinfo.proxy_group = SERVER_GROUP_NONE;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      usage(0);
    }
    else if (!strcmp(argv[i], "--relaxed"))
    {
      RelaxedConformance = 1;
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(CUPS_SVERSION);
    }
    else if (!strncmp(argv[i], "--", 2))
    {
      fprintf(stderr, "ippserver: Unknown option \"%s\".\n", argv[i]);
      usage(1);
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
          case '2' : /* -2 (enable 2-sided printing) */
              pinfo.duplex = 1;
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


	      pinfo.make = argv[i];
	      break;

          case 'P' : /* -P (PIN printing mode) */
              pinfo.pin = 1;
              break;

	  case 'a' : /* -a attributes-file */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      if (!serverLoadAttributes(argv[i], &pinfo))
	        return (1);
	      break;

          case 'c' : /* -c command */
              i ++;
	      if (i >= argc)
	        usage(1);

	      pinfo.command = argv[i];
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

	      pinfo.document_formats = argv[i];
	      break;

          case 'h' : /* -h (show help) */
	      usage(0);

	  case 'i' : /* -i icon.png */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      pinfo.icon = argv[i];
	      break;

	  case 'k' : /* -k (keep files) */
	      KeepFiles = 1;
	      break;

	  case 'l' : /* -l location */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      pinfo.location = argv[i];
	      break;

	  case 'm' : /* -m model */
	      i ++;
	      if (i >= argc)
	        usage(1);

	      pinfo.model = argv[i];
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
              if (sscanf(argv[i], "%d,%d", &pinfo.ppm, &pinfo.ppm_color) < 1)
                usage(1);
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

  if (confdir && (name || pinfo.make || pinfo.model || pinfo.location || pinfo.attrs || pinfo.command || pinfo.icon || pinfo.document_formats || pinfo.duplex || pinfo.pin || pinfo.ppm || pinfo.ppm_color))
  {
    fputs("ippserver: Cannot specify configuration directory with printer options (-2, -M, -P, -a, -c, -f, -i, -l, -m, -s)\n", stderr);
    usage(1);
  }

  if (!name && !confdir)
    usage(1);
  else if (confdir)
  {
   /*
    * Load the configuration from the specified directory...
    */

    if (!serverCreateSystem(confdir))
      return (1);
  }
  else
  {
   /*
    * Create a single printer (backwards-compatibility mode)...
    */

    serverLog(SERVER_LOGLEVEL_INFO, "Using default configuration with a single printer.");

    if (!pinfo.document_formats)
      pinfo.document_formats = "application/pdf,image/jpeg,image/pwg-raster";
    if (!pinfo.location)
      pinfo.location = "";
    if (!pinfo.make)
      pinfo.make = "Test";
    if (!pinfo.model)
      pinfo.model = "Printer";

    if (!serverCreateSystem(NULL))
      return (1);

    if ((printer = serverCreatePrinter("/ipp/print", name, &pinfo, 1)) == NULL)
      return (1);

    printer->state        = IPP_PSTATE_IDLE;
    printer->is_accepting = 1;

    serverAddPrinter(printer);
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
    puts(CUPS_SVERSION);
    puts("Copyright (c) 2014-2018 by the IEEE-ISTO Printer Working Group.");
    puts("Copyright (c) 2010-2018 by Apple Inc.");
    puts("");
  }

  puts("Usage: ippserver [options] \"name\"");
  puts("");
  puts("Options:");
  puts("--help                  Show program help.");
  puts("--relaxed               Run in relaxed conformance mode.");
  puts("--version               Show program version.");
  puts("-2                      Supports 2-sided printing (default=1-sided)");
  puts("-C config-directory     Load settings and printers from the specified directory.");
#ifdef HAVE_SSL
  puts("-K keypath              Specifies the location of certificates and keys");
#endif /* HAVE_SSL */
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

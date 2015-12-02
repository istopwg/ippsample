/*
 * Main entry for IPP Infrastructure Printer sample implementation.
 *
 * Copyright 2010-2015 by Apple Inc.
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
  const char	*opt,			/* Current option character */
		*attrfile = NULL,	/* Attributes file */
		*command = NULL,	/* Command to run with job files */
		*servername = NULL,	/* Server host name */
		*name = NULL,		/* Printer name */
		*location = "",		/* Location of printer */
		*make = "Test",		/* Manufacturer */
		*model = "Printer",	/* Model */
		*icon = "printer.png",	/* Icon file */
		*formats = "application/pdf,image/jpeg,image/pwg-raster";
	      				/* Supported formats */
#ifdef HAVE_SSL
  const char	*keypath = NULL;	/* Keychain path */
#endif /* HAVE_SSL */
  const char	*subtype = "_print";	/* Bonjour service subtype */
  int		port = 0,		/* Port number (0 = auto) */
		pin = 0;		/* PIN printing mode? */
  char		directory[1024] = "",	/* Spool directory */
		hostname[1024],		/* Auto-detected hostname */
		proxy_user[256] = "",	/* Proxy username */
		*proxy_pass = NULL;	/* Proxy password */
  server_printer_t *printer;		/* Printer object */


 /*
  * Parse command-line arguments...
  */

  for (i = 1; i < argc; i ++)
    if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
	{
#ifdef HAVE_SSL
	  case 'K' : /* -K keypath */
	      i ++;
	      if (i >= argc)
	        usage(1);
	      keypath = argv[i];
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

	      attrfile = argv[i];
	      break;

          case 'c' : /* -c command */
              i ++;
	      if (i >= argc)
	        usage(1);

	      command = argv[i];
	      break;

	  case 'd' : /* -d spool-directory */
	      i ++;
	      if (i >= argc)
	        usage(1);
	      strlcpy(directory, argv[i], sizeof(directory));
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
	      servername = argv[i];
	      break;

	  case 'p' : /* -p port */
	      i ++;
	      if (i >= argc || !isdigit(argv[i][0] & 255))
	        usage(1);
	      port = atoi(argv[i]);
	      break;

	  case 'r' : /* -r subtype */
	      i ++;
	      if (i >= argc)
	        usage(1);
	      subtype = argv[i];
	      break;

          case 'u' : /* -u user:pass */
	      i ++;
	      if (i >= argc)
	        usage(1);
	      strlcpy(proxy_user, argv[i], sizeof(proxy_user));
	      if ((proxy_pass = strchr(proxy_user, ':')) != NULL)
	        *proxy_pass++ = '\0';
	      break;

	  case 'v' : /* -v (be verbose) */
	      Verbosity ++;
	      break;

          default : /* Unknown */
	      fprintf(stderr, "Unknown option \"-%c\".\n", *opt);
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
      fprintf(stderr, "Unexpected command-line argument \"%s\"\n", argv[i]);
      usage(1);
    }

  if (!name)
    usage(1);

 /*
  * Apply defaults as needed...
  */

  if (!servername)
    servername = httpGetHostname(NULL, hostname, sizeof(hostname));

  if (!port)
  {
#ifdef WIN32
   /*
    * Windows is almost always used as a single user system, so use a default port
    * number of 8631.
    */

    port = 8631;

#else
   /*
    * Use 8000 + UID mod 1000 for the default port number...
    */

    port = 8000 + ((int)getuid() % 1000);
#endif /* WIN32 */

    fprintf(stderr, "Listening on port %d.\n", port);
  }

  if (!directory[0])
  {
    const char *tmpdir;			/* Temporary directory */

#ifdef WIN32
    if ((tmpdir = getenv("TEMP")) == NULL)
      tmpdir = "C:/TEMP";
#elif defined(__APPLE__)
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/private/tmp";
#else
    if ((tmpdir = getenv("TMPDIR")) == NULL)
      tmpdir = "/tmp";
#endif /* WIN32 */

    snprintf(directory, sizeof(directory), "%s/ippserver.%d", tmpdir, (int)getpid());

    if (mkdir(directory, 0755) && errno != EEXIST)
    {
      fprintf(stderr, "Unable to create spool directory \"%s\": %s\n",
	      directory, strerror(errno));
      usage(1);
    }

    if (Verbosity)
      fprintf(stderr, "Using spool directory \"%s\".\n", directory);
  }

  if (!proxy_user[0])
  {
    strlcpy(proxy_user, "test", sizeof(proxy_user));

    if (Verbosity)
      fputs("Using proxy username \"test\".\n", stderr);
  }

  if (!proxy_pass)
  {
    proxy_pass = "test123";

    if (Verbosity)
      fputs("Using proxy password \"test123\".\n", stderr);
  }

#ifdef HAVE_SSL
  cupsSetServerCredentials(keypath, servername, 1);
#endif /* HAVE_SSL */

 /*
  * Initialize Bonjour...
  */

  serverDNSSDInit();

 /*
  * Create the printer...
  */

  if ((printer = serverCreatePrinter(servername, port, name, directory, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, proxy_user, proxy_pass)) == NULL)
    return (1);

 /*
  * Run the print service...
  */

  serverRunPrinter(printer);

 /*
  * Destroy the printer and exit...
  */

  serverDeletePrinter(printer);

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
    puts(CUPS_SVERSION " - Copyright 2010-2014 by Apple Inc. All rights reserved.");
    puts("");
  }

  puts("Usage: ippinfra [options] \"name\"");
  puts("");
  puts("Options:");
  printf("-d spool-directory      Spool directory "
         "(default=/tmp/ippserver.%d)\n", (int)getpid());
  puts("-h                      Show program help");
  puts("-k                      Keep job spool files");
  puts("-n hostname             Hostname for printer");
  puts("-p port                 Port number (default=auto)");
  puts("-u user:pass            Set proxy username and password");
  puts("-v[vvv]                 Be (very) verbose");

  exit(status);
}

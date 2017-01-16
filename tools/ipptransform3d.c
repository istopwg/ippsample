/*
 * ipptransform3d utility for converting 3MF and STL files to G-code.
 *
 * Copyright 2016 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#include <config.h>
#include <cups/cups.h>
#include <cups/array-private.h>
#include <cups/string-private.h>
#include <cups/thread-private.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#ifdef __APPLE__
#  include <IOKit/serial/ioss.h>
#endif /* __APPLE__ */

#ifndef WIN32
#  include <spawn.h>
#  include <poll.h>
#endif /* !WIN32 */


/*
 * Local types...
 */

typedef struct gcode_buffer_s		/**** Buffer for G-code status lines ****/
{
  char	buffer[8192],			/* Buffer */
	*bufptr;			/* Pointer info buffer */
  size_t bytes;				/* Bytes in buffer */
} gcode_buffer_t;


/*
 * Local globals...
 */

static int	Verbosity = 0;		/* Log level */


/*
 * Local functions...
 */

static int	gcode_fill(gcode_buffer_t *buf, int device_fd, int wait_secs);
static char	*gcode_gets(gcode_buffer_t *buf);
static int	gcode_puts(gcode_buffer_t *buf, int device_fd, char *line, int linenum);
static int	load_env_options(cups_option_t **options);
static int	open_device(const char *device_uri);
static void	usage(int status) __attribute__((noreturn));
static int	xform_document(const char *filename, const char *outformat, int num_options, cups_option_t *options, gcode_buffer_t *buf, int device_fd);


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
  int		fd = 1;			/* Output file/socket */
  int		status = 0;		/* Exit status */
  gcode_buffer_t buffer;		/* G-code response buffer */


 /*
  * Process the command-line...
  */

  num_options  = load_env_options(&options);
  content_type = getenv("CONTENT_TYPE");
  device_uri   = getenv("DEVICE_URI");
  output_type  = getenv("OUTPUT_TYPE");

  if ((opt = getenv("SERVER_LOGLEVEL")) != NULL)
  {
    if (!strcmp(opt, "debug"))
      Verbosity = 2;
    else if (!strcmp(opt, "info"))
      Verbosity = 1;
  }

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-' && argv[i][1] != '-')
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
    else if (!strcmp(argv[i], "--help"))
      usage(0);
    else if (!strncmp(argv[i], "--", 2))
    {
      fprintf(stderr, "ERROR: Unknown option '%s'.\n", argv[i]);
      usage(1);
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
      if (!strcmp(opt, ".3mf"))
        content_type = "model/3mf";
      else if (!strcmp(opt, ".stl"))
        content_type = "application/sla";
    }
  }

  if (!content_type)
  {
    fprintf(stderr, "ERROR: Unknown format for \"%s\", please specify with '-i' option.\n", filename);
    usage(1);
  }
  else if (strcmp(content_type, "application/sla") && strcmp(content_type, "model/3mf"))
  {
    fprintf(stderr, "ERROR: Unsupported format \"%s\" for \"%s\".\n", content_type, filename);
    usage(1);
  }

  if (!output_type)
  {
    fputs("ERROR: Unknown output format, please specify with '-m' option.\n", stderr);
    usage(1);
  }
  else if (strcmp(output_type, "application/g-code") && strncmp(output_type, "application/g-code;", 19))
  {
    fprintf(stderr, "ERROR: Unsupported output format \"%s\".\n", output_type);
    usage(1);
  }

 /*
  * If the device URI is specified, open the connection...
  */

  if (device_uri)
  {
    if (strncmp(device_uri, "usbserial:///dev/", 17))
    {
      fprintf(stderr, "ERROR: Unsupported device URI \"%s\".\n", device_uri);
      usage(1);
    }

    fd = open_device(device_uri);

   /*
    * Initialize the G-code response buffer and wait for the printer to send
    * us its firmware information, etc.
    */

    memset(&buffer, 0, sizeof(buffer));
    buffer.bufptr = buffer.buffer;

    while (gcode_fill(&buffer, fd, 15))
    {
      const char	*info;		/* Information from printer */

      while ((info = gcode_gets(&buffer)) != NULL)
        fprintf(stderr, "DEBUG: %s\n", info);
    }
  }

 /*
  * Do transform...
  */

  status = xform_document(filename, output_type, num_options, options, &buffer, fd);

  gcode_puts(&buffer, fd, "", 1);

  if (fd != 1)
    close(fd);

  return (status);
}


/*
 * 'gcode_fill()' - Fill the G-code buffer with more data...
 */

static int				/* O - 1 on success, 0 on failure */
gcode_fill(gcode_buffer_t *buf,		/* I - Buffer */
           int            device_fd,	/* I - Device file descriptor */
	   int            wait_secs)	/* I - Timeout in seconds */
{
  ssize_t	bytes;			/* Bytes read */


  if (wait_secs > 0)
  {
   /*
    * Wait for data ready...
    */

    fd_set	input;			/* select() mask */
    struct timeval timeout;		/* Timeout */

    FD_ZERO(&input);
    FD_SET(device_fd, &input);

    timeout.tv_sec  = wait_secs;
    timeout.tv_usec = 0;

    while (select(device_fd + 1, &input, NULL, NULL, &timeout) < 0)
    {
      if (errno != EINTR)
        return (0);
    }

    if (!FD_ISSET(device_fd, &input))
      return (0);
  }

  if (buf->bufptr > buf->buffer)
  {
   /*
    * Compact remaining bytes in buffer...
    */

    bytes = buf->bufptr - buf->buffer;
    if (bytes < buf->bytes)
      memmove(buf->buffer, buf->bufptr, buf->bytes - (size_t)bytes);
    buf->bufptr = buf->buffer;
    buf->bytes  -= (size_t)bytes;
  }

 /*
  * Read more bytes into the buffer...
  */

  while ((bytes = read(device_fd, buf->buffer + buf->bytes, sizeof(buf->buffer) - buf->bytes - 1)) < 0)
  {
    if (errno != EINTR)
      return (0);
  }

  buf->bytes += (size_t)bytes;
  buf->buffer[buf->bytes] = '\0';

  return (1);
}


/*
 * 'gcode_gets()' - Get a line from the G-code buffer.
 */

static char *				/* O - Line or NULL if no full line */
gcode_gets(gcode_buffer_t *buf)		/* I - Buffer */
{
  char *start = buf->bufptr;
  char *end = strchr(start, '\n');

  if (end)
  {
    *end++ = '\0';
    buf->bufptr = end;

    return (start);
  }
  else if (start == buf->buffer && buf->bytes == (sizeof(buf->buffer) - 1))
  {
    buf->bufptr = buf->buffer + buf->bytes;

    return (buf->buffer);
  }
  else
    return (NULL);
}


/*
 * 'gcode_puts()' - Write a line of G-code, complete with line number and checksum.
 */

static int				/* O - Next line number */
gcode_puts(gcode_buffer_t *buf,		/* I - G-code buffer */
           int            device_fd,	/* I - Device file */
	   char           *line,	/* I - Line from G-code file */
	   int            linenum)	/* I - Line number in G-code file */
{
  char		buffer[8192],		/* Output buffer */
        	*ptr;			/* Pointer into line/buffer */
  unsigned char	checksum;		/* XOR checksum */
  size_t	len;			/* Length of output buffer remaining */
  ssize_t	bytes;			/* Bytes written */
  int		ok = 0;			/* Line written OK? */


 /*
  * First eliminate any trailing comments and whitespace...
  */

  if ((ptr = strchr(line, ';')) != NULL)
    *ptr = '\0';

  ptr = line + strlen(line) - 1;
  while (ptr >= line && isspace(*ptr & 255))
    *ptr-- = '\0';

  if (!line[0])
    return (linenum);			/* Nothing left... */

 /*
  * Then compute a simple XOR checksum and format the output line...
  */

  snprintf(buffer, sizeof(buffer), "N%d %s", linenum, line);

  for (ptr = buffer, checksum = 0; *ptr; ptr ++)
    checksum ^= (unsigned char)*ptr;

  snprintf(buffer, sizeof(buffer), "N%d %s*%d\n", linenum ++, line, checksum);

 /*
  * Finally, write the line to the output device and wait for an OK...
  */

  do
  {
    char *resp;				/* Response from printer */

    for (ptr = buffer, len = strlen(buffer); len > 0;)
    {
      if ((bytes = write(device_fd, ptr, len)) < 0)
      {
	if (errno != EAGAIN && errno != EINTR && errno != ENOTTY)
	  return (-1);
      }
      else
      {
	len -= (size_t)bytes;
	ptr += bytes;
      }
    }

    tcdrain(device_fd);

    do
    {
      while ((resp = gcode_gets(buf)) == NULL)
      {
        if (!gcode_fill(buf, device_fd, 30))
	{
	  fputs("DEBUG: No response from printer.\n", stderr);
	  return (-1);
	}
      }

      if (!resp)
      {
	fputs("DEBUG: Unable to read response from printer.\n", stderr);
	return (-1);
      }
    }
    while (strcmp(resp, "ok") && strncmp(resp, "Resend:", 7));

    if (!strncmp(resp, "Resend:", 7))
    {
      if (atoi(resp + 7) != (linenum - 1))
      {
        fprintf(stderr, "DEBUG: Printer asked us to resend a previous line (%s, on line %d)\n", resp + 7, linenum);
	return (-1);
      }

      ok = 0;
    }
    else
      ok = 1;
  }
  while (!ok);

  return (linenum);
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
 * 'open_device()' - Open a serial port device...
 */

static int				/* O - File descriptor or -1 on error */
open_device(const char *device_uri)	/* I - Device URI */
{
  char		filename[1024],		/* Device filename */
		*options;		/* Pointer to options */
  int		device_fd,		/* Serial device */
                device_state;		/* Serial control lines */
  struct termios opts;			/* Serial port options */
  int		baud = 250000;		/* Baud rate */



 /*
  * Open the serial port device...
  */

  if (strncmp(device_uri, "usbserial:///dev/", 17))
    return (-1);

  strlcpy(filename, device_uri + 12, sizeof(filename));
  if ((options = strchr(filename, '?')) != NULL)
    *options++ = '\0';

  do
  {
    if ((device_fd = open(filename, O_RDWR | O_NOCTTY | O_EXCL | O_NDELAY)) == -1)
    {
      if (errno == EBUSY)
      {
        fputs("INFO: Printer busy; will retry in 30 seconds.\n", stderr);
	sleep(30);
      }
      else
      {
        fprintf(stderr, "ERROR: Unable to open device file \"%s\": %s\n", filename, strerror(errno));
	return (-1);
      }
    }
  }
  while (device_fd < 0);

 /*
  * Set any options provided...
  */

  tcgetattr(device_fd, &opts);

  cfmakeraw(&opts);

  opts.c_cflag |= CREAD | CLOCAL;	/* Enable reader */

  opts.c_cflag &= (unsigned)~CRTSCTS;	/* No RTS/CTS flow control */

  opts.c_cflag &= (unsigned)~CSIZE;	/* 8-bits */
  opts.c_cflag |= CS8;

  opts.c_cflag &= (unsigned)~PARENB;	/* No parity */

  opts.c_cflag &= (unsigned)~CSTOPB;	/* 1 stop bit */

  if (options && !strncasecmp(options, "baud=", 5))
  {
   /*
    * Set the baud rate...
    */

    baud = atoi(options + 5);
  }

 /*
  * Set serial port settings and then toggle DTR...
  */

#ifdef __APPLE__			/* USB serial doesn't follow POSIX, grrr... */
  cfsetispeed(&opts, B9600);
  cfsetospeed(&opts, B9600);

  tcsetattr(device_fd, TCSANOW, &opts);
  ioctl(device_fd, IOSSIOSPEED, &baud);

#elif defined(__linux)			/* Linux needs to use non-POSIX termios2 ioctl, grrr... */
  opts.c_cflag &= ~CBAUD;
  opts.c_cflag != BOTHER;
  opts.c_ospeed = opts.c_ispeed = baud;

  ioctl(device_fd, TCSETS2, &opts);

#else					/* Other platforms default to POSIX termios */
  /* TODO: Add support for older platforms where B9600 != 9600, etc. */
  cfsetispeed(&opts, baud);
  cfsetospeed(&opts, baud);

  tcsetattr(device_fd, TCSANOW, &opts);
#endif /* __APPLE__ */

  fcntl(device_fd, F_SETFL, 0);

  ioctl(device_fd, TIOCMGET, &device_state);
  device_state |= TIOCM_DTR;
  ioctl(device_fd, TIOCMSET, &device_state);
  usleep(100000);
  device_state &= ~TIOCM_DTR;
  ioctl(device_fd, TIOCMSET, &device_state);

  return (device_fd);
}


/*
 * 'usage()' - Show program usage.
 */

static void
usage(int status)			/* I - Exit status */
{
  puts("Usage: ipptransform [options] filename\n");
  puts("Options:");
  puts("  --help");
  puts("  -d device-uri");
  puts("  -i input/format");
  puts("  -m output/format");
  puts("  -o \"name=value [... name=value]\"");
  puts("  -v\n");
  puts("Device URIs: usbserial:///dev/...");
  puts("Input Formats: application/sla, model/3mf");
  puts("Output Formats: application/g-code;flavor=FOO");
  puts("Options: materials-col, print-accuracy, print-quality, print-rafts, print-supports, printer-bed-temperature");

  exit(status);
}


/*
 * 'xform_document()' - Transform and print a document.
 */

static int				/* O - 0 on success, 1 on failure */
xform_document(
    const char     *filename,		/* I - Input file */
    const char     *outformat,		/* I - Output format */
    int            num_options,		/* I - Number of options */
    cups_option_t  *options,		/* I - Options */
    gcode_buffer_t *buf,		/* I - G-code response buffer */
    int            device_fd)		/* I - Device file */
{
#ifdef WIN32
  return (0);

#else
  const char	*val;			/* Option value */
  int		pid,			/* Process ID */
		status;			/* Exit status */
  const char	*myargv[100];		/* Command-line arguments */
  int		myargc;			/* Number of arguments */
  posix_spawn_file_actions_t actions;	/* Spawn file actions */
  int		mystdout[2] = {-1, -1};	/* Pipe for stdout */
  struct pollfd	polldata[2];		/* Poll data */
  int		pollcount;		/* Number of pipes to poll */
  char		data[32768],		/* Data from stdout */
		*dataptr,		/* Pointer to end of data */
                *ptr,			/* Pointer into data */
		*end;			/* End of data */
  ssize_t	bytes;			/* Bytes read */
  int		linenum = 1;		/* G-code line number */
  int		bed = 0,		/* printer-bed-temperature value */
		material;		/* material-temperature value */


  (void)outformat; /* TODO: support different gcode flavors */

 /*
  * Start with gcode commands to set the extruder and print bed temperatures...
  */

  if ((val = cupsGetOption("printer-bed-temperature", num_options, options)) != NULL)
    bed = atoi(val);
  else if ((val = getenv("PRINTER_BED_TEMPERATURE_DEFAULT")) != NULL)
    bed = atoi(val);

  if (bed)
  {
    snprintf(data, sizeof(data), "M190 S%d", bed);
    linenum = gcode_puts(buf, device_fd, data, linenum);
  }

  if ((val = cupsGetOption("materials-col", num_options, options)) != NULL)
    val = getenv("PRINTER_MATERIALS_COL_DEFAULT");

  if (val && (ptr = strstr(val, "material-temperature=")) != NULL)
  {
    /* TODO: Support multiple materials */
    material = atoi(ptr + 21);
    snprintf(data, sizeof(data), "M109 S%d", material);
    linenum = gcode_puts(buf, device_fd, data, linenum);
  }

  linenum = gcode_puts(buf, device_fd, "G21", linenum); /* Metric */
  linenum = gcode_puts(buf, device_fd, "G90", linenum); /* Absolute positioning */
  linenum = gcode_puts(buf, device_fd, "M82", linenum); /* Absolute extruder mode */
  linenum = gcode_puts(buf, device_fd, "M107", linenum); /* Fans off */
  linenum = gcode_puts(buf, device_fd, "G28 X0 Y0 Z0", linenum); /* Home */
  linenum = gcode_puts(buf, device_fd, "G1 Z15", linenum); /* Get ready to prime the extruder */
  linenum = gcode_puts(buf, device_fd, "G92 E0", linenum); /* Clear the extruded length */
  linenum = gcode_puts(buf, device_fd, "G1 F200 E3", linenum); /* Extrude 3mm */
  linenum = gcode_puts(buf, device_fd, "G92 E0", linenum); /* Clear the extruded length */
  linenum = gcode_puts(buf, device_fd, "M117 Printing with ippserver...", linenum);

 /*
  * Setup the Cura command-line arguments...
  */

  myargv[0] = CURAENGINE;
  myargc    = 1;

  myargv[myargc++] = "-vv";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "gcodeFlavor=1";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "startCode=";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "fixHorrible=1";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "fanFullOnLayerNr=1";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "skirtMinLength=150000";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "skirtLineCount=1";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "skirtDistance=3000";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "multiVolumeOverlap=150";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "filamentDiameter=2850";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "posx=115000";
  myargv[myargc++] = "-s";
  myargv[myargc++] = "posy=112500";

  if ((val = cupsGetOption("print-quality", num_options, options)) != NULL)
  {
    switch (atoi(val))
    {
      case 4 :
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "insetXSpeed=60";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "inset0Speed=60";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "extrusionWidth=500";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "upSkinCount=3";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerSpeed=30";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "minimalLayerTime=3";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "infillSpeed=60";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerThickness=300";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layerThickness=200";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "printSpeed=60";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layer0extrusionWidth=500";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "sparseInfillLineDistance=5000";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "downSkinCount=3";
	  break;

      case 5 :
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "insetXSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "inset0Speed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "extrusionWidth=400";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "upSkinCount=10";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerSpeed=15";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "minimalLayerTime=5";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "infillSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerThickness=300";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layerThickness=60";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "printSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layer0extrusionWidth=400";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "sparseInfillLineDistance=2000";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "downSkinCount=10";
	  break;

      default :
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "insetXSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "inset0Speed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "extrusionWidth=400";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "upSkinCount=6";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerSpeed=20";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "minimalLayerTime=5";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "infillSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "initialLayerThickness=300";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layerThickness=100";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "endCode=M25";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "printSpeed=50";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "layer0extrusionWidth=400";
	  myargv[myargc++] = "-s";
	  myargv[myargc++] = "sparseInfillLineDistance=2000";
	  myargv[myargc++] = "-s downSkinCount=6";
	  break;
    }
  }

  if ((val = cupsGetOption("print-rafts", num_options, options)) != NULL && strcmp(val, "none"))
  {
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftSurfaceLineSpacing=400";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftInterfaceLineSpacing=800";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftSurfaceSpeed=20";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftBaseSpeed=20";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftFanSpeed=0";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftSurfaceThickness=270";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftBaseThickness=300";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftMargin=5000";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftAirGap=0";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftInterfaceThickness=270";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftSurfaceLayers=2";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftSurfaceLinewidth=400";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftInterfaceLinewidth=400";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftBaseLinewidth=1000";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "raftAirGapLayer0=220";
  }

  if ((val = cupsGetOption("print-supports", num_options, options)) != NULL && strcmp(val, "none"))
  {
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportAngle=60";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportXYDistance=700";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportZDistance=150";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportEverywhere=0";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportLineDistance=3333";
    myargv[myargc++] = "-s";
    myargv[myargc++] = "supportType=0";
  }

  myargv[myargc++] = (char *)filename;
  myargv[myargc  ] = NULL;

  if (pipe(mystdout))
  {
    fprintf(stderr, "ERROR: Unable to create pipe for stdout: %s\n", strerror(errno));
    return (1);
  }

  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  if (mystdout[1] < 0)
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  else
    posix_spawn_file_actions_adddup2(&actions, mystdout[1], 1);

  if (posix_spawn(&pid, myargv[0], &actions, NULL, (char **)myargv, environ))
  {
    fprintf(stderr, "ERROR: Unable to start CuraEngine command: %s", strerror(errno));

    posix_spawn_file_actions_destroy(&actions);

    return (1);
  }

  fprintf(stderr, "DEBUG: Started CuraEngine command, pid=%d\n", pid);

 /*
  * Free memory used for command...
  */

  posix_spawn_file_actions_destroy(&actions);

 /*
  * Read from the stdout and stderr pipes until EOF...
  */

  close(mystdout[1]);

  pollcount = 0;
  polldata[pollcount].fd     = mystdout[0];
  polldata[pollcount].events = POLLIN;
  pollcount ++;

  polldata[pollcount].fd     = device_fd;
  polldata[pollcount].events = POLLIN;
  pollcount ++;

  dataptr = data;

  while (poll(polldata, (nfds_t)pollcount, -1))
  {
    if (polldata[1].revents & POLLIN)
    {
     /*
      * Read status info back (eventually do something with it...)
      */

      if (gcode_fill(buf, device_fd, 0))
      {
	while ((ptr = gcode_gets(buf)) != NULL)
	  fprintf(stderr, "DEBUG: %s\n", ptr);
      }
    }

    if (polldata[0].revents & POLLIN)
    {
     /*
      * Read G-code...
      */

      if ((bytes = read(mystdout[0], dataptr, sizeof(data) - (size_t)(dataptr - data + 1))) > 0)
      {
        dataptr += bytes;
	*dataptr = '\0';

        for (end = data; (ptr = strchr(end, '\n')) != NULL; end = ptr)
	{
	 /*
	  * Send whole lines to the printer...
	  */

          *ptr++ = '\0';

	  if ((linenum = gcode_puts(buf, device_fd, end, linenum)) < 0)
	  {
	    perror("ERROR: Unable to write print data");
	    break;
	  }
	}

	if (end > data)
	{
	 /*
	  * Copy remainder to beginning of buffer...
	  */

	  dataptr -= (end - data);
	  if (dataptr > data)
	    memmove(data, end, (size_t)(dataptr - data));
	}
      }
    }
  }

  close(mystdout[0]);

 /*
  * Wait for child to complete...
  */

#  ifdef HAVE_WAITPID
  while (waitpid(pid, &status, 0) < 0);
#  else
  while (wait(&status) < 0);
#  endif /* HAVE_WAITPID */

  return (status);
#endif /* WIN32 */
}

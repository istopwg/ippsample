/*
 * Logging support for sample IPP server implementation.
 *
 * Copyright © 2014-2021 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"
#include <stdarg.h>


/*
 * Local globals...
 */

static cups_mutex_t	log_mutex = CUPS_MUTEX_INITIALIZER;
static int		log_fd = -1;


/*
 * Local functions...
 */

static ssize_t	safe_vsnprintf(char *buffer, size_t bufsize, const char *format, va_list ap);
static void	server_log_to_file(server_loglevel_t level, const char *format, va_list ap);


/*
 * 'serverLog()' - Log a message.
 */

void
serverLog(server_loglevel_t level,	/* I - Log level */
          const char        *format,	/* I - Printf-style format string */
          ...)				/* I - Additional arguments as needed */
{
  va_list	ap;			/* Pointer to arguments */


  if (level > LogLevel)
    return;

  va_start(ap, format);
  server_log_to_file(level, format, ap);
  va_end(ap);
}


/*
 * 'serverLogAttributes()' - Log attributes in a request or response.
 */

void
serverLogAttributes(
    server_client_t *client,		/* I - Client */
    const char      *title,		/* I - Title */
    ipp_t           *ipp,		/* I - Request/response */
    int             type)		/* I - 0 = object, 1 = request, 2 = response */
{
  ipp_tag_t		group_tag;	/* Current group */
  ipp_attribute_t	*attr;		/* Current attribute */
  char			buffer[8192];	/* String buffer for value */
  int			major, minor;	/* Version */


  if (LogLevel < SERVER_LOGLEVEL_DEBUG)
    return;

  major = ippGetVersion(ipp, &minor);
  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s version=%d.%d", title, major, minor);
  if (type == 1)
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s operation-id=%s(%04x)", title, ippOpString(ippGetOperation(ipp)), ippGetOperation(ipp));
  else if (type == 2)
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s status-code=%s(%04x)", title, ippErrorString(ippGetStatusCode(ipp)), ippGetStatusCode(ipp));
  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s request-id=%d", title, ippGetRequestId(ipp));

  for (attr = ippGetFirstAttribute(ipp), group_tag = IPP_TAG_ZERO; attr; attr = ippGetNextAttribute(ipp))
  {
    if (ippGetGroupTag(attr) != group_tag)
    {
      group_tag = ippGetGroupTag(attr);
      if (group_tag != IPP_TAG_ZERO)
        serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s %s", title, ippTagString(group_tag));
    }

    if (ippGetName(attr))
    {
      ippAttributeString(attr, buffer, sizeof(buffer));
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s %s (%s%s) %s", title, ippGetName(attr), ippGetCount(attr) > 1 ? "1setOf " : "", ippTagString(ippGetValueTag(attr)), buffer);
    }
  }
}


/*
 * 'serverLogClient()' - Log a client message.
 */

void
serverLogClient(
     server_loglevel_t level,		/* I - Log level */
     server_client_t   *client,		/* I - Client */
     const char        *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  char		temp[1024];		/* Temporary format string */
  va_list	ap;			/* Pointer to arguments */


  if (level > LogLevel)
    return;

  va_start(ap, format);
  if (client)
  {
    if (httpIsEncrypted(client->http))
      snprintf(temp, sizeof(temp), "[Client %dE] %s", client->number, format);
    else
      snprintf(temp, sizeof(temp), "[Client %d] %s", client->number, format);
    server_log_to_file(level, temp, ap);
  }
  else
    server_log_to_file(level, format, ap);
  va_end(ap);
}


/*
 * 'serverLogJob()' - Log a job message.
 */

void
serverLogJob(
     server_loglevel_t level,		/* I - Log level */
     server_job_t      *job,		/* I - Job */
     const char        *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  char		temp[1024];		/* Temporary format string */
  va_list	ap;			/* Pointer to arguments */


  if (level > LogLevel)
    return;

  va_start(ap, format);
  snprintf(temp, sizeof(temp), "[Job %d] %s", job->id, format);
  server_log_to_file(level, temp, ap);
  va_end(ap);
}


/*
 * 'serverLogPrinter()' - Log a printer message.
 */

void
serverLogPrinter(
     server_loglevel_t level,		/* I - Log level */
     server_printer_t  *printer,	/* I - Printer */
     const char        *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  char		temp[1024],		/* Temporary format string */
		*tempptr,		/* Pointer into temporary string */
		*nameptr;		/* Pointer into printer name */
  va_list	ap;			/* Pointer to arguments */


  if (level > LogLevel)
    return;

 /*
  * Prefix the message with "[Printer foo]", making sure to not insert any
  * printf format specifiers.
  */

  cupsCopyString(temp, "[Printer ", sizeof(temp));
  for (tempptr = temp + 9, nameptr = printer->name; *nameptr && tempptr < (temp + 200); tempptr ++)
  {
    if (*nameptr == '%')
      *tempptr++ = '%';
    *tempptr = *nameptr++;
  }
  *tempptr++ = ']';
  *tempptr++ = ' ';
  cupsCopyString(tempptr, format, sizeof(temp) - (size_t)(tempptr - temp));

  va_start(ap, format);
  server_log_to_file(level, temp, ap);
  va_end(ap);
}


/*
 * 'serverTimeString()' - Return the local time in hours, minutes, and seconds.
 */

char *					/* O - Time string */
serverTimeString(time_t tv,		/* I - Time value */
                 char   *buffer,	/* I - Buffer */
	         size_t bufsize)	/* I - Size of buffer */
{
  struct tm	date;			/* Local date and time */

  localtime_r(&tv, &date);

  strftime(buffer, bufsize, "%X", &date);

  return (buffer);
}


/*
 * 'safe_vsnprintf()' - Format a string into a fixed size buffer, quoting special characters.
 */

static ssize_t				/* O - Number of bytes formatted */
safe_vsnprintf(
    char       *buffer,			/* O - Output buffer */
    size_t     bufsize,			/* O - Size of output buffer */
    const char *format,			/* I - printf-style format string */
    va_list    ap)			/* I - Pointer to additional arguments */
{
  char		*bufptr,		/* Pointer to position in buffer */
		*bufend,		/* Pointer to end of buffer */
		size,			/* Size character (h, l, L) */
		type;			/* Format type character */
  int		width,			/* Width of field */
		prec;			/* Number of characters of precision */
  char		tformat[100],		/* Temporary format string for snprintf() */
		*tptr,			/* Pointer into temporary format */
		temp[1024];		/* Buffer for formatted numbers */
  char		*s;			/* Pointer to string */
  ssize_t	bytes;			/* Total number of bytes needed */


  if (!buffer || bufsize < 2 || !format)
    return (-1);

 /*
  * Loop through the format string, formatting as needed...
  */

  bufptr = buffer;
  bufend = buffer + bufsize - 1;
  bytes  = 0;

  while (*format)
  {
    if (*format == '%')
    {
      tptr = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        if (bufptr < bufend)
	  *bufptr++ = *format;
        bytes ++;
        format ++;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        *tptr++ = *format++;

      if (*format == '*')
      {
       /*
        * Get width from argument...
	*/

	format ++;
	width = va_arg(ap, int);

	snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", width);
	tptr += strlen(tptr);
      }
      else
      {
	width = 0;

	while (isdigit(*format & 255))
	{
	  if (tptr < (tformat + sizeof(tformat) - 1))
	    *tptr++ = *format;

	  width = width * 10 + *format++ - '0';
	}
      }

      if (*format == '.')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        format ++;

        if (*format == '*')
	{
         /*
	  * Get precision from argument...
	  */

	  format ++;
	  prec = va_arg(ap, int);

	  snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", prec);
	  tptr += strlen(tptr);
	}
	else
	{
	  prec = 0;

	  while (isdigit(*format & 255))
	  {
	    if (tptr < (tformat + sizeof(tformat) - 1))
	      *tptr++ = *format;

	    prec = prec * 10 + *format++ - '0';
	  }
	}
      }

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';

	if (tptr < (tformat + sizeof(tformat) - 2))
	{
	  *tptr++ = 'l';
	  *tptr++ = 'l';
	}

	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        size = *format++;
      }
      else
        size = 0;

      if (!*format)
        break;

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';

      switch (type)
      {
	case 'E' : /* Floating point formats */
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, double));

            bytes += (int)strlen(temp);

            if (bufptr)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

        case 'B' : /* Integer formats */
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long long));
	    else
#  endif /* HAVE_LONG_LONG */
            if (size == 'l')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long));
	    else
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, int));

            bytes += (int)strlen(temp);

	    if (bufptr)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

	case 'p' : /* Pointer value */
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, void *));

            bytes += (int)strlen(temp);

	    if (bufptr)
	    {
	      cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr));
	      bufptr += strlen(bufptr);
	    }
	    break;

        case 'c' : /* Character or character array */
	    bytes += width;

	    if (bufptr)
	    {
	      if (width <= 1)
	        *bufptr++ = (char)va_arg(ap, int);
	      else
	      {
		if ((bufptr + width) > bufend)
		  width = (int)(bufend - bufptr);

		memcpy(bufptr, va_arg(ap, char *), (size_t)width);
		bufptr += width;
	      }
	    }
	    break;

	case 's' : /* String */
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

           /*
	    * Copy the C string, replacing control chars and \ with
	    * C character escapes...
	    */

            for (bufend --; *s && bufptr < bufend; s ++)
	    {
	      if (*s == '\n')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = 'n';
		bytes += 2;
	      }
	      else if (*s == '\r')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = 'r';
		bytes += 2;
	      }
	      else if (*s == '\t')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = 't';
		bytes += 2;
	      }
	      else if (*s == '\\')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = '\\';
		bytes += 2;
	      }
	      else if (*s == '\'')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = '\'';
		bytes += 2;
	      }
	      else if (*s == '\"')
	      {
	        *bufptr++ = '\\';
		*bufptr++ = '\"';
		bytes += 2;
	      }
	      else if ((*s & 255) < ' ')
	      {
	        if ((bufptr + 2) >= bufend)
	          break;

	        *bufptr++ = '\\';
		*bufptr++ = '0';
		*bufptr++ = '0' + *s / 8;
		*bufptr++ = '0' + (*s & 7);
		bytes += 4;
	      }
	      else
	      {
	        *bufptr++ = *s;
		bytes ++;
	      }
            }

            bufend ++;
	    break;

	case 'n' : /* Output number of chars so far */
	    *(va_arg(ap, int *)) = (int)bytes;
	    break;
      }
    }
    else
    {
      bytes ++;

      if (bufptr < bufend)
        *bufptr++ = *format;

      format ++;
    }
  }

 /*
  * Nul-terminate the string and return the number of characters needed.
  */

  *bufptr = '\0';

  return (bytes);
}


/*
 * 'server_log_to_file()' - Log a formatted message to a file.
 */

static void
server_log_to_file(
    server_loglevel_t level,		/* I - Log level */
    const char        *format,		/* I - Printf-style format string */
    va_list           ap)		/* I - Pointer to additional arguments */
{
  char		buffer[8192],		/* Message buffer */
		*bufptr;		/* Pointer into buffer */
  ssize_t	bytes;			/* Number of bytes in message */
  struct timeval curtime;		/* Current time */
  struct tm	curdate;		/* Current date and time */
  static const char * const pris[] =	/* Log priority strings */
  {
    "<63>",				/* Error message */
    "<66>",				/* Informational message */
    "<67>"				/* Debugging message */
  };


#ifdef _WIN32
  _cups_gettimeofday(&curtime, NULL);
  time_t tv_sec = (time_t)curtime.tv_sec;
  gmtime_s(&curdate, &tv_sec);
#else
  gettimeofday(&curtime, NULL);
  gmtime_r(&curtime.tv_sec, &curdate);
#endif /* _WIN32 */

  if (LogFile)
  {
   /*
    * When logging to a file, use the syslog format...
    */

    snprintf(buffer, sizeof(buffer), "%s1 %04d-%02d-%02dT%02d:%02d:%02d.%03dZ %s ippserver %d -  ", pris[level], curdate.tm_year + 1900, curdate.tm_mon + 1, curdate.tm_mday, curdate.tm_hour, curdate.tm_min, curdate.tm_sec, (int)curtime.tv_usec / 1000, ServerName, getpid());
  }
  else
  {
   /*
    * Otherwise just include the date and time for convenience...
    */

    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ  ", curdate.tm_year + 1900, curdate.tm_mon + 1, curdate.tm_mday, curdate.tm_hour, curdate.tm_min, curdate.tm_sec, (int)curtime.tv_usec / 1000);
  }

  bufptr = buffer + strlen(buffer);

  if ((bytes = safe_vsnprintf(bufptr, sizeof(buffer) - (size_t)(bufptr - buffer + 1), format, ap)) > 0)
  {
    bufptr += bytes;
    if (bufptr > (buffer + sizeof(buffer) - 1))
      bufptr = buffer + sizeof(buffer) - 1;

    if (bufptr > buffer && bufptr[-1] != '\n')
      *bufptr++ = '\n';

    if (log_fd < 0)
    {
      cupsMutexLock(&log_mutex);
      if (log_fd < 0)
      {
        if (LogFile)
        {
          if ((log_fd = open(LogFile, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0644)) < 0)
          {
            fprintf(stderr, "Unable to open log file \"%s\": %s\n", LogFile, strerror(errno));
            log_fd = 2;
          }
        }
        else
          log_fd = 2;
      }
      cupsMutexUnlock(&log_mutex);
    }

    write(log_fd, buffer, (size_t)(bufptr - buffer));
  }
}

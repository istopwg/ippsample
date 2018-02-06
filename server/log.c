/*
 * Logging support for sample IPP server implementation.
 *
 * Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group
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

static _cups_mutex_t	log_mutex = _CUPS_MUTEX_INITIALIZER;
static int		log_fd = -1;


/*
 * Local functions...
 */

static void server_log_to_file(server_loglevel_t level, const char *format, va_list ap);


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

  for (attr = ippFirstAttribute(ipp), group_tag = IPP_TAG_ZERO;
       attr;
       attr = ippNextAttribute(ipp))
  {
    if (ippGetGroupTag(attr) != group_tag)
    {
      group_tag = ippGetGroupTag(attr);
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
#ifdef HAVE_SSL
    if (httpIsEncrypted(client->http))
      snprintf(temp, sizeof(temp), "[Client %dE] %s", client->number, format);
    else
#endif /* HAVE_SSL */
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
  char		temp[1024];		/* Temporary format string */
  va_list	ap;			/* Pointer to arguments */


  if (level > LogLevel)
    return;

  va_start(ap, format);
  snprintf(temp, sizeof(temp), "[Printer %s] %s", printer->name, format);
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
  struct tm	*curtime = localtime(&tv);
					/* Local time */

  strftime(buffer, bufsize, "%X", curtime);
  return (buffer);
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
  struct tm	*curdate;		/* Current date and time */
  static const char * const pris[] =	/* Log priority strings */
  {
    "<63>",				/* Error message */
    "<66>",				/* Informational message */
    "<67>"				/* Debugging message */
  };


  gettimeofday(&curtime, NULL);
  curdate = gmtime(&curtime.tv_sec);

  if (LogFile)
  {
   /*
    * When logging to a file, use the syslog format...
    */

    snprintf(buffer, sizeof(buffer), "%s1 %04d-%02d-%02dT%02d:%02d:%02d.%03dZ %s ippserver %d -  ", pris[level], curdate->tm_year + 1900, curdate->tm_mon + 1, curdate->tm_mday, curdate->tm_hour, curdate->tm_min, curdate->tm_sec, curtime.tv_usec / 1000, ServerName, getpid());
  }
  else
  {
   /*
    * Otherwise just include the date and time for convenience...
    */

    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ  ", curdate->tm_year + 1900, curdate->tm_mon + 1, curdate->tm_mday, curdate->tm_hour, curdate->tm_min, curdate->tm_sec, curtime.tv_usec / 1000);
  }

  bufptr = buffer + strlen(buffer);

  if ((bytes = _cups_safe_vsnprintf(bufptr, sizeof(buffer) - (size_t)(bufptr - buffer + 1), format, ap)) > 0)
  {
    bufptr += bytes;
    if (bufptr > (buffer + sizeof(buffer) - 1))
      bufptr = buffer + sizeof(buffer) - 1;

    if (bufptr > buffer && bufptr[-1] != '\n')
      *bufptr++ = '\n';

    if (log_fd < 0)
    {
      _cupsMutexLock(&log_mutex);
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
      _cupsMutexUnlock(&log_mutex);
    }

    write(log_fd, buffer, (size_t)(bufptr - buffer));
  }
}

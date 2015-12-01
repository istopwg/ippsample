/*
 * Logging support for sample IPP server implementation.
 *
 * Copyright 2015 by Apple Inc.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * This file is subject to the Apple OS-Developed Software exception.
 */

#include "ippserver.h"
#include <stdarg.h>


/*
 * 'serverLog()' - Log a message.
 */

void
serverLog(int        level,		/* I - Log level */
          const char *format,		/* I - Printf-style format string */
          ...)				/* I - Additional arguments as needed */
{
  (void)level;
  (void)format;
}


/*
 * 'serverLogAttributes()' - Log attributes in a request or response.
 */

void
serverLogAttributes(const char *title,	/* I - Title */
                    ipp_t      *ipp,	/* I - Request/response */
                    int        type)	/* I - 0 = object, 1 = request, 2 = response */
{
  ipp_tag_t		group_tag;	/* Current group */
  ipp_attribute_t	*attr;		/* Current attribute */
  char			buffer[2048];	/* String buffer for value */
  int			major, minor;	/* Version */


  if (Verbosity <= 1)
    return;

  fprintf(stderr, "%s:\n", title);
  major = ippGetVersion(ipp, &minor);
  fprintf(stderr, "  version=%d.%d\n", major, minor);
  if (type == 1)
    fprintf(stderr, "  operation-id=%s(%04x)\n",
            ippOpString(ippGetOperation(ipp)), ippGetOperation(ipp));
  else if (type == 2)
    fprintf(stderr, "  status-code=%s(%04x)\n",
            ippErrorString(ippGetStatusCode(ipp)), ippGetStatusCode(ipp));
  fprintf(stderr, "  request-id=%d\n\n", ippGetRequestId(ipp));

  for (attr = ippFirstAttribute(ipp), group_tag = IPP_TAG_ZERO;
       attr;
       attr = ippNextAttribute(ipp))
  {
    if (ippGetGroupTag(attr) != group_tag)
    {
      group_tag = ippGetGroupTag(attr);
      fprintf(stderr, "  %s\n", ippTagString(group_tag));
    }

    if (ippGetName(attr))
    {
      ippAttributeString(attr, buffer, sizeof(buffer));
      fprintf(stderr, "    %s (%s%s) %s\n", ippGetName(attr),
	      ippGetCount(attr) > 1 ? "1setOf " : "",
	      ippTagString(ippGetValueTag(attr)), buffer);
    }
  }
}


/*
 * 'serverLogClient()' - Log a client message.
 */

void
serverLogClient(
     int             level,		/* I - Log level */
     server_client_t *client,		/* I - Client */
     const char      *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  (void)level;
  (void)client;
  (void)format;
}


/*
 * 'serverLogJob()' - Log a job message.
 */

void
serverLogJob(
     int          level,		/* I - Log level */
     server_job_t *job,			/* I - Job */
     const char   *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  (void)level;
  (void)job;
  (void)format;
}


/*
 * 'serverLogPrinter()' - Log a printer message.
 */

void
serverLogPrinter(
     int              level,		/* I - Log level */
     server_printer_t *printer,		/* I - Printer */
     const char       *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  (void)level;
  (void)printer;
  (void)format;
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

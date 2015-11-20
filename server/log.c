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
serverLogClient(
     int              level,		/* I - Log level */
     server_printer_t *printer,		/* I - Printer */
     const char       *format,		/* I - Printf-style format string */
     ...)				/* I - Additional arguments as needed */
{
  (void)level;
  (void)printer;
  (void)format;
}

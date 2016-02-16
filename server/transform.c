/*
 * Transform code for sample IPP server implementation.
 *
 * Copyright 2015-2016 by Apple Inc.
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


/*
 * Local functions...
 */

static void		process_attr_message(server_job_t *job, char *message);
static void		process_state_message(server_job_t *job, char *message);


/*
 * 'serverTransformJob()' - Generate printer-ready document data for a Job.
 */

int					/* O - 0 on success, non-zero on error */
serverTransformJob(
    server_job_t       *job,		/* I - Job to transform */
    const char         *command,	/* I - Command to run */
    const char         *format,		/* I - Destination MIME media type */
    server_transform_t mode)		/* I - Transform mode */
{
  int 		pid,			/* Process ID */
                status = 0;		/* Exit status */
  time_t	start,			/* Start time */
                end;			/* End time */
  char		*myargv[3],		/* Command-line arguments */
		*myenvp[200];		/* Environment variables */
  int		myenvc;			/* Number of environment variables */
  ipp_attribute_t *attr;		/* Job attribute */
  char		val[1280],		/* IPP_NAME=value */
                *valptr;		/* Pointer into string */
#ifndef WIN32
  int		mystdout[2],		/* Pipe for stdout */
		mystderr[2];		/* Pipe for stderr */
  char		line[2048],		/* Line from stderr */
                *ptr,			/* Pointer into line */
                *endptr;		/* End of line */
  ssize_t	bytes;			/* Bytes read */
#endif /* !WIN32 */


  // TODO: implement mode
  // TODO: implement timing report
  (void)mode;
  (void)mystdout;
  (void)end;

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Running command \"%s %s\".", command, job->filename);
  time(&start);

 /*
  * Setup the command-line arguments...
  */

  myargv[0] = (char *)command;
  myargv[1] = job->filename;
  myargv[2] = NULL;

 /*
  * Copy the current environment, then add environment variables for every
  * Job attribute and select Printer attributes...
  */

  for (myenvc = 0; environ[myenvc] && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); myenvc ++)
    myenvp[myenvc] = strdup(environ[myenvc]);

  if (myenvc > (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 32))
  {
    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Too many environment variables to transform job.");
    goto transform_failure;
  }

  if (asprintf(myenvp + myenvc, "CONTENT_TYPE=%s", job->format) > 0)
    myenvc ++;

  if (job->printer->device_uri && asprintf(myenvp + myenvc, "DEVICE_URI=%s", job->printer->device_uri) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->attrs, "document-name", IPP_TAG_NAME)) != NULL && asprintf(myenvp + myenvc, "DOCUMENT_NAME=%s", ippGetString(attr, 0, NULL)) > 0)
    myenvc ++;

  /* TODO: OUTPUT_ORDER */

  if (asprintf(myenvp + myenvc, "OUTPUT_TYPE=%s", format) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->attrs, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL && ippAttributeString(attr, val, sizeof(val)) > 0 && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_RESOLUTION_SUPPORTED=%s", val) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->attrs, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) != NULL && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_SHEET_BACK=%s", ippGetString(attr, 0, NULL)) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->attrs, "pwg-raster-document-type-supported", IPP_TAG_RESOLUTION)) != NULL && ippAttributeString(attr, val, sizeof(val)) > 0 && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_TYPE_SUPPORTED=%s", val) > 0)
    myenvc ++;

  for (attr = ippFirstAttribute(job->attrs); attr && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); attr = ippNextAttribute(job->attrs))
  {
   /*
    * Convert "attribute-name" to "IPP_ATTRIBUTE_NAME=" and then add the
    * value(s) from the attribute.
    */

    const char *name = ippGetName(attr);
    if (!name)
      continue;

    valptr = val;
    *valptr++ = 'I';
    *valptr++ = 'P';
    *valptr++ = 'P';
    *valptr++ = '_';
    while (*name && valptr < (val + sizeof(val) - 2))
    {
      if (*name == '-')
        *valptr++ = '_';
      else
        *valptr++ = (char)toupper(*name & 255);

      name ++;
    }
    *valptr++ = '=';
    ippAttributeString(attr, valptr, sizeof(val) - (size_t)(valptr - val));

    myenvp[myenvc++] = strdup(val);
  }
  myenvp[myenvc] = NULL;

 /*
  * Now run the program...
  */

#ifdef WIN32
  status = _spawnvpe(_P_WAIT, command, myargv, myenvp);

#else
  if (pipe(mystderr))
  {
    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to create pipe for stderr: %s", strerror(errno));
    goto transform_failure;
  }

  if ((pid = fork()) == 0)
  {
   /*
    * Child comes here...
    */

    close(2);
    dup2(mystderr[1], 2);
    close(mystderr[0]);
    close(mystderr[1]);

    execve(command, myargv, myenvp);
    exit(errno);
  }
  else if (pid < 0)
  {
   /*
    * Unable to fork process...
    */

    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to start job processing command: %s", strerror(errno));

    close(mystderr[0]);
    close(mystderr[1]);

    goto transform_failure;
  }
  else
  {
   /*
    * Free memory used for environment...
    */

    while (myenvc > 0)
      free(myenvp[-- myenvc]);

   /*
    * Read from the stderr pipe until EOF...
    */

    close(mystderr[1]);

    endptr = line;
    while ((bytes = read(mystderr[0], endptr, sizeof(line) - (size_t)(endptr - line) - 1)) > 0)
    {
      endptr += bytes;
      *endptr = '\0';

      while ((ptr = strchr(line, '\n')) != NULL)
      {
        *ptr++ = '\0';

        if (!strncmp(line, "STATE:", 6))
        {
         /*
          * Process printer-state-reasons keywords.
          */

          process_state_message(job, line);
        }
        else if (!strncmp(line, "ATTR:", 5))
        {
         /*
          * Process printer attribute update.
          */

          process_attr_message(job, line);
        }
        else
          serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "%s: %s", command, line);

        bytes = ptr - line;
        if (ptr < endptr)
          memmove(line, ptr, (size_t)(endptr - ptr));
        endptr -= bytes;
        *endptr = '\0';
      }
    }

    close(mystderr[0]);

    if (endptr > line)
    {
     /*
      * Write the final output that wasn't terminated by a newline...
      */

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "%s: %s", command, line);
    }

   /*
    * Wait for child to complete...
    */

#  ifdef HAVE_WAITPID
    while (waitpid(pid, &status, 0) < 0);
#  else
    while (wait(&status) < 0);
#  endif /* HAVE_WAITPID */
  }
#endif /* WIN32 */

  return (status);

 /*
  * This is where we go for hard failures...
  */

  transform_failure:

  while (myenvc > 0)
    free(myenvp[-- myenvc]);

  return (-1);
}


/*
 * 'process_attr_message()' - Process an ATTR: message from a command.
 */

static void
process_attr_message(
    server_job_t *job,			/* I - Job */
    char         *message)		/* I - Message */
{
  (void)job;
  (void)message;
}


/*
 * 'process_state_message()' - Process a STATE: message from a command.
 */

static void
process_state_message(
    server_job_t *job,			/* I - Job */
    char         *message)		/* I - Message */
{
  int		i;			/* Looping var */
  server_preason_t state_reasons,	/* printer-state-reasons values */
		bit;			/* Current reason bit */
  char		*ptr,			/* Pointer into message */
		*next;			/* Next keyword in message */
  int		remove;			/* Non-zero if we are removing keywords */


 /*
  * Skip leading "STATE:" and any whitespace...
  */

  for (message += 6; *message; message ++)
    if (*message != ' ' && *message != '\t')
      break;

 /*
  * Support the following forms of message:
  *
  * "keyword[,keyword,...]" to set the printer-state-reasons value(s).
  *
  * "-keyword[,keyword,...]" to remove keywords.
  *
  * "+keyword[,keyword,...]" to add keywords.
  *
  * Keywords may or may not have a suffix (-report, -warning, -error) per
  * RFC 2911.
  */

  if (*message == '-')
  {
    remove        = 1;
    state_reasons = job->printer->state_reasons;
    message ++;
  }
  else if (*message == '+')
  {
    remove        = 0;
    state_reasons = job->printer->state_reasons;
    message ++;
  }
  else
  {
    remove        = 0;
    state_reasons = SERVER_PREASON_NONE;
  }

  while (*message)
  {
    if ((next = strchr(message, ',')) != NULL)
      *next++ = '\0';

    if ((ptr = strstr(message, "-error")) != NULL)
      *ptr = '\0';
    else if ((ptr = strstr(message, "-report")) != NULL)
      *ptr = '\0';
    else if ((ptr = strstr(message, "-warning")) != NULL)
      *ptr = '\0';

    for (i = 0, bit = 1; i < (int)(sizeof(server_preasons) / sizeof(server_preasons[0])); i ++, bit *= 2)
    {
      if (!strcmp(message, server_preasons[i]))
      {
        if (remove)
	  state_reasons &= ~bit;
	else
	  state_reasons |= bit;
      }
    }

    if (next)
      message = next;
    else
      break;
  }

  job->printer->state_reasons = state_reasons;
}

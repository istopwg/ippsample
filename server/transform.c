/*
 * Transform code for sample IPP server implementation.
 *
 * Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2015-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"

#ifdef _WIN32
#  include <sys/timeb.h>
#else
#  include <signal.h>
#  include <spawn.h>
#endif /* _WIN32 */


/*
 * Local functions...
 */

#ifdef _WIN32
static int	asprintf(char **s, const char *format, ...);
#endif /* _WIN32 */
static void	process_attr_message(server_job_t *job, char *message, server_transform_t mode);
static void	process_state_message(server_job_t *job, char *message);
static double	time_seconds(void);


/*
 * 'serverStopJob()' - Stop processing/transforming a job.
 */

void
serverStopJob(server_job_t *job)	/* I - Job to stop */
{
  if (job->state != IPP_JSTATE_PROCESSING)
    return;

  _cupsRWLockWrite(&job->rwlock);

  job->state         = IPP_JSTATE_STOPPED;
  job->state_reasons |= SERVER_JREASON_JOB_STOPPED;

#ifndef _WIN32 /* TODO: Figure out a way to kill a spawned process on Windows */
  if (job->transform_pid)
    kill(job->transform_pid, SIGTERM);
#endif /* !_WIN32 */
  _cupsRWUnlock(&job->rwlock);

  serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED, "Job stopped.");
}


/*
 * 'serverTransformJob()' - Generate printer-ready document data for a Job.
 */

int					/* O - 0 on success, non-zero on error */
serverTransformJob(
    server_client_t    *client,		/* I - Client connection (if any) */
    server_job_t       *job,		/* I - Job to transform */
    const char         *command,	/* I - Command to run */
    const char         *format,		/* I - Destination MIME media type */
    server_transform_t mode)		/* I - Transform mode */
{
  int		i;			/* Looping var */
  int 		pid,			/* Process ID */
                status = 0;		/* Exit status */
  double	start,			/* Start time */
                end;			/* End time */
  char		*myargv[3],		/* Command-line arguments */
		*myenvp[200];		/* Environment variables */
  int		myenvc;			/* Number of environment variables */
  ipp_attribute_t *attr;		/* Job attribute */
  char		val[1280],		/* IPP_NAME=value */
                *valptr,		/* Pointer into string */
                fullcommand[1024];	/* Full command path */
#ifndef _WIN32
  posix_spawn_file_actions_t actions;	/* Spawn file actions */
  int		mystdout[2] = {-1, -1},	/* Pipe for stdout */
		mystderr[2] = {-1, -1};	/* Pipe for stderr */
  struct pollfd	polldata[2];		/* Poll data */
  int		pollcount,		/* Number of pipes to poll */
                pollret;                /* Return value from poll() */
  char		data[32768],		/* Data from stdout */
		line[2048],		/* Line from stderr */
                *ptr,			/* Pointer into line */
                *endptr;		/* End of line */
  ssize_t	bytes;			/* Bytes read */
  size_t	total = 0;		/* Total bytes read */
#endif /* !_WIN32 */


  if (command[0] != '/')
  {
    snprintf(fullcommand, sizeof(fullcommand), "%s/%s", BinDir, command);
    command = fullcommand;
  }

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Running command \"%s %s\".", command, job->filename);
  start = time_seconds();

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

  if (job->printer->pinfo.device_uri && asprintf(myenvp + myenvc, "DEVICE_URI=%s", job->printer->pinfo.device_uri) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->attrs, "document-name", IPP_TAG_NAME)) != NULL && asprintf(myenvp + myenvc, "DOCUMENT_NAME=%s", ippGetString(attr, 0, NULL)) > 0)
    myenvc ++;

  /* TODO: OUTPUT_ORDER, defaults (Issue #94) */

  if (format && asprintf(myenvp + myenvc, "OUTPUT_TYPE=%s", format) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "copies-default", IPP_TAG_INTEGER)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "copies-default", IPP_TAG_INTEGER);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_COPIES_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "finishings-default", IPP_TAG_ENUM)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "finishings-default", IPP_TAG_ENUM);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_FINISHINGS_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "finishings-col-default", IPP_TAG_BEGIN_COLLECTION)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "finishings-col-default", IPP_TAG_BEGIN_COLLECTION);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_FINISHINGS_COL_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "materials-col-default", IPP_TAG_BEGIN_COLLECTION)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "materials-col-default", IPP_TAG_BEGIN_COLLECTION);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_MATERIALS_COL_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "media-default", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "media-default", IPP_TAG_KEYWORD);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_MEDIA_DEFAULT=%s", ippGetString(attr, 0, NULL)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "media-col-default", IPP_TAG_BEGIN_COLLECTION)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "media-col-default", IPP_TAG_BEGIN_COLLECTION);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_MEDIA_COL_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "number-up-default", IPP_TAG_INTEGER)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "number-up-default", IPP_TAG_INTEGER);
  if (attr)
  {
    ippAttributeString(attr, val, sizeof(val));

    if (asprintf(myenvp + myenvc, "PRINTER_NUMBER_UP_DEFAULT=%s", val))
      myenvc ++;
  }

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "platform-temperature-default", IPP_TAG_INTEGER)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "platform-temperature-default", IPP_TAG_INTEGER);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_PLATFORM_TEMPERATURE_DEFAULT=%d", ippGetInteger(attr, 0)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "print-base-default", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "print-base-default", IPP_TAG_KEYWORD);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_PRINT_BASE_DEFAULT=%s", ippGetString(attr, 0, NULL)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "print-quality-default", IPP_TAG_ENUM)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "print-quality-default", IPP_TAG_ENUM);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_PRINT_QUALITY_DEFAULT=%d", ippGetInteger(attr, 0)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "print-color-mode-default", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "print-color-mode-default", IPP_TAG_KEYWORD);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_PRINT_COLOR_MODE_DEFAULT=%s", ippGetString(attr, 0, NULL)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "print-supports-default", IPP_TAG_INTEGER)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "print-supports-default", IPP_TAG_INTEGER);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_PRINT_SUPPORTS_DEFAULT=%s", ippGetString(attr, 0, NULL)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "sides-default", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "sides-default", IPP_TAG_KEYWORD);
  if (attr && asprintf(myenvp + myenvc, "PRINTER_SIDES_DEFAULT=%s", ippGetString(attr, 0, NULL)))
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION);
  if (attr && ippAttributeString(attr, val, sizeof(val)) > 0 && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_RESOLUTION_SUPPORTED=%s", val) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "pwg-raster-document-sheet-back", IPP_TAG_KEYWORD);
  if (attr && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_SHEET_BACK=%s", ippGetString(attr, 0, NULL)) > 0)
    myenvc ++;

  if ((attr = ippFindAttribute(job->printer->dev_attrs, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) == NULL)
    attr = ippFindAttribute(job->printer->pinfo.attrs, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD);
  if (attr && ippAttributeString(attr, val, sizeof(val)) > 0 && asprintf(myenvp + myenvc, "PWG_RASTER_DOCUMENT_TYPE_SUPPORTED=%s", val) > 0)
    myenvc ++;

  if (LogLevel == SERVER_LOGLEVEL_INFO)
    myenvp[myenvc ++] = strdup("SERVER_LOGLEVEL=info");
  else if (LogLevel == SERVER_LOGLEVEL_DEBUG)
    myenvp[myenvc ++] = strdup("SERVER_LOGLEVEL=debug");
  else
    myenvp[myenvc ++] = strdup("SERVER_LOGLEVEL=error");

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

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Transform environment:");
  for (i = 0; i < myenvc; i ++)
    serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "%s", myenvp[i]);

 /*
  * Now run the program...
  */

#ifdef _WIN32
  status = _spawnvpe(_P_WAIT, command, myargv, myenvp);

#else
  if (mode == SERVER_TRANSFORM_TO_CLIENT)
  {
    if (pipe(mystdout))
    {
      serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to create pipe for stdout: %s", strerror(errno));
      goto transform_failure;
    }
  }
  else
  {
    mystdout[0] = -1;

    if (mode == SERVER_TRANSFORM_TO_FILE)
    {
      serverCreateJobFilename(job, format, line, sizeof(line));
      mystdout[1] = open(line, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0666);
    }
    else
      mystdout[1] = open("/dev/null", O_WRONLY);

    if (mystdout[1] < 0)
    {
      serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to open file for stdout: %s", strerror(errno));
      goto transform_failure;
    }
  }

  if (pipe(mystderr))
  {
    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to create pipe for stderr: %s", strerror(errno));
    goto transform_failure;
  }

  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  if (mystdout[1] < 0)
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  else
    posix_spawn_file_actions_adddup2(&actions, mystdout[1], 1);

  if (mystderr[1] < 0)
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
  else
    posix_spawn_file_actions_adddup2(&actions, mystderr[1], 2);

  if (posix_spawn(&pid, command, &actions, NULL, myargv, myenvp))
  {
    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Unable to start job processing command: %s", strerror(errno));

    posix_spawn_file_actions_destroy(&actions);

    goto transform_failure;
  }

  job->transform_pid = pid;

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Started job processing command, pid=%d", pid);

 /*
  * Free memory used for command...
  */

  posix_spawn_file_actions_destroy(&actions);

  while (myenvc > 0)
    free(myenvp[-- myenvc]);

 /*
  * Read from the stdout and stderr pipes until EOF...
  */

  close(mystdout[1]);
  close(mystderr[1]);

  endptr = line;

  pollcount = 0;
  polldata[pollcount].fd     = mystderr[0];
  polldata[pollcount].events = POLLIN;
  pollcount ++;

  if (mystdout[0] >= 0)
  {
    polldata[pollcount].fd     = mystdout[0];
    polldata[pollcount].events = POLLIN;
    pollcount ++;
  }

  while ((pollret = poll(polldata, (nfds_t)pollcount, -1)) > 0)
  {
    if (polldata[0].revents & POLLIN)
    {
      if ((bytes = read(mystderr[0], endptr, sizeof(line) - (size_t)(endptr - line) - 1)) > 0)
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
	    * Process job/printer attribute update.
	    */

	    process_attr_message(job, line, mode);
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
    }
    else if (pollcount > 1 && polldata[1].revents & POLLIN)
    {
      if ((bytes = read(mystdout[0], data, sizeof(data))) > 0)
      {
	httpWrite2(client->http, data, (size_t)bytes);
	total += (size_t)bytes;
      }
    }

    if (polldata[0].revents & POLLHUP)
      break;
  }

  if (mystdout[0] >= 0)
  {
    close(mystdout[0]);

    serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Total transformed output is %ld bytes.", (long)total);
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

  job->transform_pid = 0;
#endif /* _WIN32 */

  end = time_seconds();
  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Total transform time is %.3f seconds.", end - start);

#ifdef _WIN32
  if (status)
    serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Transform command exited with status %d.", status);

#else
  if (status)
  {
    if (WIFEXITED(status))
      serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Transform command exited with status %d.", WEXITSTATUS(status));
    else if (WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM)
      serverLogJob(SERVER_LOGLEVEL_ERROR, job, "Transform command crashed on signal %d.", WTERMSIG(status));
  }
#endif /* _WIN32 */

  return (status);

 /*
  * This is where we go for hard failures...
  */

  transform_failure:

  #ifndef _WIN32
  if (mystdout[0] >= 0)
    close(mystdout[0]);
  if (mystdout[1] >= 0)
    close(mystdout[1]);

  if (mystderr[0] >= 0)
    close(mystderr[0]);
  if (mystderr[1] >= 0)
    close(mystderr[1]);
#endif /* !_WIN32 */

  while (myenvc > 0)
    free(myenvp[-- myenvc]);

  return (-1);
}


#ifdef _WIN32
/*
 * 'asprintf()' - Format and allocate a string.
 */

static int				/* O - Number of characters */
asprintf(char       **s,		/* O - Allocated string or `NULL` on error */
         const char *format,		/* I - printf-style format string */
	 ...)				/* I - Additional arguments as needed */
{
  int		bytes;			/* Number of characters */
  char		buffer[8192];		/* Temporary buffer */
  va_list	ap;			/* Pointer to arguments */


  va_start(ap, format);
  bytes = vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  if (bytes < 0)
    *s = NULL;
  else
    *s = strdup(buffer);

  return (bytes);
}
#endif /* _WIN32 */


/*
 * 'process_attr_message()' - Process an ATTR: message from a command.
 */

static void
process_attr_message(
    server_job_t       *job,		/* I - Job */
    char               *message,	/* I - Message */
    server_transform_t mode)		/* I - Transform mode */
{
  int		i,			/* Looping var */
		num_options = 0;	/* Number of name=value pairs */
  cups_option_t	*options = NULL,	/* name=value pairs from message */
		*option;		/* Current option */
  ipp_attribute_t *attr;		/* Current attribute */


 /*
  * Grab attributes from the message line...
  */

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "%s", message);

  num_options = cupsParseOptions(message + 5, num_options, &options);

  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "num_options=%d", num_options);

 /*
  * Loop through the options and record them in the printer or job objects...
  */

  for (i = num_options, option = options; i > 0; i --, option ++)
  {
    serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "options[%d].name=\"%s\", .value=\"%s\"", num_options - i, option->name, option->value);

    if (!strcmp(option->name, "job-impressions"))
    {
     /*
      * Update job-impressions attribute...
      */

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Setting Job Status attribute \"%s\" to \"%s\".", option->name, option->value);

      _cupsRWLockWrite(&job->rwlock);

      job->impressions = atoi(option->value);

      _cupsRWUnlock(&job->rwlock);
    }
    else if (mode == SERVER_TRANSFORM_COMMAND && !strcmp(option->name, "job-impressions-completed"))
    {
     /*
      * Update job-impressions-completed attribute...
      */

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Setting Job Status attribute \"%s\" to \"%s\".", option->name, option->value);

      _cupsRWLockWrite(&job->rwlock);

      job->impcompleted = atoi(option->value);

      _cupsRWUnlock(&job->rwlock);
    }
    else if (!strcmp(option->name, "job-impressions-col") || !strcmp(option->name, "job-media-sheets") || !strcmp(option->name, "job-media-sheets-col") ||
        (mode == SERVER_TRANSFORM_COMMAND && (!strcmp(option->name, "job-impressions-completed-col") || !strcmp(option->name, "job-media-sheets-completed") || !strcmp(option->name, "job-media-sheets-completed-col"))))
    {
     /*
      * Update Job Status attribute...
      */

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Setting Job Status attribute \"%s\" to \"%s\".", option->name, option->value);

      _cupsRWLockWrite(&job->rwlock);

      if ((attr = ippFindAttribute(job->attrs, option->name, IPP_TAG_ZERO)) != NULL)
        ippDeleteAttribute(job->attrs, attr);

      cupsEncodeOption(job->attrs, IPP_TAG_JOB, option->name, option->value);

      _cupsRWUnlock(&job->rwlock);
    }
    else if (!strncmp(option->name, "marker-", 7) || !strcmp(option->name, "printer-alert") || !strcmp(option->name, "printer-supply") || !strcmp(option->name, "printer-supply-description"))
    {
     /*
      * Update Printer Status attribute...
      */

      serverLogPrinter(SERVER_LOGLEVEL_DEBUG, job->printer, "Setting Printer Status attribute \"%s\" to \"%s\".", option->name, option->value);

      _cupsRWLockWrite(&job->printer->rwlock);

      if ((attr = ippFindAttribute(job->printer->pinfo.attrs, option->name, IPP_TAG_ZERO)) != NULL)
        ippDeleteAttribute(job->printer->pinfo.attrs, attr);

      cupsEncodeOption(job->printer->pinfo.attrs, IPP_TAG_PRINTER, option->name, option->value);

      _cupsRWUnlock(&job->printer->rwlock);
    }
    else
    {
     /*
      * Something else that isn't currently supported...
      */

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Ignoring attribute \"%s\" with value \"%s\".", option->name, option->value);
    }
  }

  cupsFreeOptions(num_options, options);
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
  * RFC 8011.
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


/*
 * 'time_seconds()' - Return the current time in fractional seconds.
 */

static double				/* O - Time in seconds */
time_seconds(void)
{
#ifdef _WIN32
  struct _timeb curtime;		/* Current time */


  _ftime(&curtime);

  return ((double)curtime.time + 0.001 * curtime.millitm);

#else
  struct timeval curtime;		/* Current time */


  gettimeofday(&curtime, NULL);

  return ((double)curtime.tv_sec + 0.000001 * curtime.tv_usec);
#endif /* _WIN32 */
}

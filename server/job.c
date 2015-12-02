/*
 * Job object code for sample IPP server implementation.
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

#include "ippserver.h"


/*
 * Local functions...
 */

static void		process_attr_message(server_job_t *job, char *message);
static void		process_state_message(server_job_t *job, char *message);


/*
 * 'serverCheckJobs()' - Check for new jobs to process.
 */

void
serverCheckJobs(server_printer_t *printer)	/* I - Printer */
{
  server_job_t	*job;			/* Current job */


  if (printer->processing_job)
    return;

  _cupsRWLockWrite(&(printer->rwlock));
  for (job = (server_job_t *)cupsArrayFirst(printer->active_jobs);
       job;
       job = (server_job_t *)cupsArrayNext(printer->active_jobs))
  {
    if (job->state == IPP_JSTATE_PENDING)
    {
      if (!_cupsThreadCreate((_cups_thread_func_t)serverProcessJob, job))
      {
        job->state     = IPP_JSTATE_ABORTED;
	job->completed = time(NULL);

        serverAddEvent(printer, job, SERVER_EVENT_JOB_COMPLETED, "Job aborted because creation of processing thread failed.");
      }
      break;
    }
  }
  _cupsRWUnlock(&(printer->rwlock));
}


/*
 * 'serverCleanJobs()' - Clean out old (completed) jobs.
 */

void
serverCleanJobs(server_printer_t *printer)	/* I - Printer */
{
  server_job_t	*job;			/* Current job */
  time_t	cleantime;		/* Clean time */


  if (cupsArrayCount(printer->jobs) == 0)
    return;

  cleantime = time(NULL) - 60;

  _cupsRWLockWrite(&(printer->rwlock));
  for (job = (server_job_t *)cupsArrayFirst(printer->jobs);
       job;
       job = (server_job_t *)cupsArrayNext(printer->jobs))
    if (job->completed && job->completed < cleantime)
    {
      cupsArrayRemove(printer->jobs, job);
      serverDeleteJob(job);
    }
    else
      break;
  _cupsRWUnlock(&(printer->rwlock));
}


/*
 * 'serverCopyJobStateReasons()' - Copy printer-state-reasons values.
 */

void
serverCopyJobStateReasons(
    ipp_t      *ipp,			/* I - Attributes */
    ipp_tag_t  group_tag,		/* I - Group */
    server_job_t *job)			/* I - Printer */
{
  server_jreason_t	creasons;	/* Combined job-state-reasons */


  creasons = job->state_reasons | job->dev_state_reasons;

  if (!creasons)
  {
    ippAddString(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons", NULL, "none");
  }
  else
  {
    int			i,		/* Looping var */
			num_reasons = 0;/* Number of reasons */
    server_jreason_t	reason;		/* Current reason */
    const char		*reasons[32];	/* Reason strings */

    for (i = 0, reason = 1; i < (int)(sizeof(server_jreasons) / sizeof(server_jreasons[0])); i ++, reason <<= 1)
    {
      if (creasons & reason)
        reasons[num_reasons ++] = server_jreasons[i];
    }

    ippAddStrings(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons", num_reasons, NULL, reasons);
  }
}


/*
 * 'serverCreateJob()' - Create a new job object from a Print-Job or Create-Job
 *                  request.
 */

server_job_t *			/* O - Job */
serverCreateJob(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Job attribute */
  char			uri[1024],	/* job-uri value */
			uuid[64];	/* job-uuid value */


  _cupsRWLockWrite(&(client->printer->rwlock));

 /*
  * Allocate and initialize the job object...
  */

  if ((job = calloc(1, sizeof(server_job_t))) == NULL)
  {
    perror("Unable to allocate memory for job");
    return (NULL);
  }

  job->printer    = client->printer;
  job->attrs      = ippNew();
  job->state      = IPP_JSTATE_HELD;
  job->fd         = -1;

 /*
  * Copy all of the job attributes...
  */

  serverCopyAttributes(job->attrs, client->request, NULL, IPP_TAG_JOB, 0);

 /*
  * Get the requesting-user-name, document format, and priority...
  */

  if ((attr = ippFindAttribute(client->request, "job-priority", IPP_TAG_INTEGER)) != NULL)
    job->priority = ippGetInteger(attr, 0);
  else
    job->priority = 50;

  if ((attr = ippFindAttribute(client->request, "requesting-user-name", IPP_TAG_NAME)) != NULL)
    job->username = ippGetString(attr, 0, NULL);
  else
    job->username = "anonymous";

  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_NAME, "job-originating-user-name", NULL, job->username);

  if (ippGetOperation(client->request) != IPP_OP_CREATE_JOB)
  {
    if ((attr = ippFindAttribute(job->attrs, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL)
      job->format = ippGetString(attr, 0, NULL);
    else if ((attr = ippFindAttribute(job->attrs, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL)
      job->format = ippGetString(attr, 0, NULL);
    else
      job->format = "application/octet-stream";
  }

  if ((attr = ippFindAttribute(client->request, "job-impressions", IPP_TAG_INTEGER)) != NULL)
    job->impressions = ippGetInteger(attr, 0);

  if ((attr = ippFindAttribute(client->request, "job-name", IPP_TAG_NAME)) != NULL)
    job->name = ippGetString(attr, 0, NULL);

 /*
  * Add job description attributes and add to the jobs array...
  */

  job->id = client->printer->next_job_id ++;

  snprintf(uri, sizeof(uri), "%s/%d", client->printer->uri, job->id);
  httpAssembleUUID(client->printer->hostname, client->printer->port, client->printer->name, job->id, uuid, sizeof(uuid));

  ippAddDate(job->attrs, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(time(&job->created)));
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uuid", NULL, uuid);
  if ((attr = ippFindAttribute(client->request, "printer-uri", IPP_TAG_URI)) != NULL)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, ippGetString(attr, 0, NULL));
  else
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, client->printer->uri);
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "time-at-creation", (int)(job->created - client->printer->start_time));

  cupsArrayAdd(client->printer->jobs, job);
  cupsArrayAdd(client->printer->active_jobs, job);

  _cupsRWUnlock(&(client->printer->rwlock));

  return (job);
}


/*
 * 'serverCreateJobFilename()' - Create the filename for a document in a job.
 */

void serverCreateJobFilename(
    server_printer_t *printer,		/* I - Printer */
    server_job_t     *job,		/* I - Job */
    const char     *format,		/* I - Format or NULL */
    char           *fname,		/* I - Filename buffer */
    size_t         fnamesize)		/* I - Size of filename buffer */
{
  char			name[256],	/* "Safe" filename */
			*nameptr;	/* Pointer into filename */
  const char		*ext,		/* Filename extension */
			*job_name;	/* job-name value */
  ipp_attribute_t	*job_name_attr;	/* job-name attribute */


 /*
  * Make a name from the job-name attribute...
  */

  if ((job_name_attr = ippFindAttribute(job->attrs, "job-name", IPP_TAG_NAME)) != NULL)
    job_name = ippGetString(job_name_attr, 0, NULL);
  else
    job_name = "untitled";

  for (nameptr = name; *job_name && nameptr < (name + sizeof(name) - 1); job_name ++)
    if (isalnum(*job_name & 255) || *job_name == '-')
      *nameptr++ = (char)tolower(*job_name & 255);
    else
      *nameptr++ = '_';

  *nameptr = '\0';

 /*
  * Figure out the extension...
  */

  if (!format)
    format = job->format;

  if (!strcasecmp(format, "image/jpeg"))
    ext = "jpg";
  else if (!strcasecmp(format, "image/png"))
    ext = "png";
  else if (!strcasecmp(format, "image/pwg-raster"))
    ext = "ras";
  else if (!strcasecmp(format, "image/urf"))
    ext = "urf";
  else if (!strcasecmp(format, "application/pdf"))
    ext = "pdf";
  else if (!strcasecmp(format, "application/postscript"))
    ext = "ps";
  else
    ext = "prn";

 /*
  * Create a filename with the job-id, job-name, and document-format (extension)...
  */

  snprintf(fname, fnamesize, "%s/%d-%s.%s", printer->directory, job->id, name, ext);
}


/*
 * 'serverDeleteJob()' - Remove from the printer and free all memory used by a job
 *                  object.
 */

void
serverDeleteJob(server_job_t *job)		/* I - Job */
{
  if (Verbosity)
    fprintf(stderr, "Removing job #%d from history.\n", job->id);

  _cupsRWLockWrite(&job->rwlock);

  ippDelete(job->attrs);

  if (job->filename)
  {
    if (!KeepFiles)
      unlink(job->filename);

    free(job->filename);
  }

  _cupsRWDeinit(&job->rwlock);

  free(job);
}


/*
 * 'serverFindJob()' - Find a job specified in a request.
 */

server_job_t *			/* O - Job or NULL */
serverFindJob(server_client_t *client,		/* I - Client */
         int           job_id)		/* I - Job ID to find or 0 to lookup */
{
  ipp_attribute_t	*attr;		/* job-id or job-uri attribute */
  server_job_t		key,		/* Job search key */
			*job;		/* Matching job, if any */


  if (job_id > 0)
  {
    key.id = job_id;
  }
  else if ((attr = ippFindAttribute(client->request, "job-uri", IPP_TAG_URI)) != NULL)
  {
    const char *uri = ippGetString(attr, 0, NULL);

    if (!strncmp(uri, client->printer->uri, client->printer->urilen) &&
        uri[client->printer->urilen] == '/')
      key.id = atoi(uri + client->printer->urilen + 1);
    else
      return (NULL);
  }
  else if ((attr = ippFindAttribute(client->request, "job-id", IPP_TAG_INTEGER)) != NULL)
  {
    key.id = ippGetInteger(attr, 0);
  }

  _cupsRWLockRead(&(client->printer->rwlock));
  job = (server_job_t *)cupsArrayFind(client->printer->jobs, &key);
  _cupsRWUnlock(&(client->printer->rwlock));

  return (job);
}


/*
 * 'serverGetJobStateReasonsBits()' - Get the bits associates with "job-state-reasons" values.
 */

server_jreason_t			/* O - Bits */
serverGetJobStateReasonsBits(
    ipp_attribute_t *attr)		/* I - "job-state-reasons" attribute */
{
  int			i, j,		/* Looping vars */
			count;		/* Number of "job-state-reasons" values */
  const char		*keyword;	/* "job-state-reasons" value */
  server_jreason_t	jreasons = SERVER_JREASON_NONE;
					/* Bits for "job-state-reasons" values */


  count = ippGetCount(attr);
  for (i = 0; i < count; i ++)
  {
    keyword = ippGetString(attr, i, NULL);

    for (j = 0; j < (int)(sizeof(server_jreasons) / sizeof(server_jreasons[0])); j ++)
    {
      if (!strcmp(keyword, server_jreasons[j]))
      {
        jreasons |= 1 << j;
	break;
      }
    }
  }

  return (jreasons);
}


/*
 * 'serverProcessJob()' - Process a print job.
 */

void *				/* O - Thread exit status */
serverProcessJob(server_job_t *job)		/* I - Job */
{
  job->state                   = IPP_JSTATE_PROCESSING;
  job->printer->state          = IPP_PSTATE_PROCESSING;
  job->processing              = time(NULL);
  job->printer->processing_job = job;

  serverAddEvent(job->printer, job, SERVER_EVENT_JOB_STATE_CHANGED, "Job processing.");

  while (job->printer->state_reasons & SERVER_PREASON_MEDIA_EMPTY)
  {
    job->printer->state_reasons |= SERVER_PREASON_MEDIA_NEEDED;

    sleep(1);
  }

  job->printer->state_reasons &= (server_preason_t)~SERVER_PREASON_MEDIA_NEEDED;

#if 0
  if (job->printer->command)
  {
   /*
    * Execute a command with the job spool file and wait for it to complete...
    */

    int 	pid,			/* Process ID */
		status;			/* Exit status */
    time_t	start,			/* Start time */
		end;			/* End time */
    char	*myargv[3],		/* Command-line arguments */
		*myenvp[200];		/* Environment variables */
    int		myenvc;			/* Number of environment variables */
    ipp_attribute_t *attr;		/* Job attribute */
    char	val[1280],		/* IPP_NAME=value */
		*valptr;		/* Pointer into string */
#ifndef WIN32
    int		mypipe[2];		/* Pipe for stderr */
    char	line[2048],		/* Line from stderr */
		*ptr,			/* Pointer into line */
		*endptr;		/* End of line */
    ssize_t	bytes;			/* Bytes read */
#endif /* !WIN32 */

    fprintf(stderr, "Running command \"%s %s\".\n", job->printer->command,
            job->filename);
    time(&start);

   /*
    * Setup the command-line arguments...
    */

    myargv[0] = job->printer->command;
    myargv[1] = job->filename;
    myargv[2] = NULL;

   /*
    * Copy the current environment, then add ENV variables for every Job
    * attribute...
    */

    for (myenvc = 0; environ[myenvc] && myenvc < (int)(sizeof(myenvp) / sizeof(myenvp[0]) - 1); myenvc ++)
      myenvp[myenvc] = strdup(environ[myenvc]);

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
    status = _spawnvpe(_P_WAIT, job->printer->command, myargv, myenvp);

#else
    if (pipe(mypipe))
    {
      perror("Unable to create pipe for stderr");
      mypipe[0] = mypipe[1] = -1;
    }

    if ((pid = fork()) == 0)
    {
     /*
      * Child comes here...
      */

      close(2);
      dup2(mypipe[1], 2);
      close(mypipe[0]);
      close(mypipe[1]);

      execve(job->printer->command, myargv, myenvp);
      exit(errno);
    }
    else if (pid < 0)
    {
     /*
      * Unable to fork process...
      */

      perror("Unable to start job processing command");
      status = -1;

      close(mypipe[0]);
      close(mypipe[1]);

     /*
      * Free memory used for environment...
      */

      while (myenvc > 0)
	free(myenvp[-- myenvc]);
    }
    else
    {
     /*
      * Free memory used for environment...
      */

      while (myenvc > 0)
	free(myenvp[-- myenvc]);

     /*
      * If the pipe exists, read from it until EOF...
      */

      if (mypipe[0] >= 0)
      {
	close(mypipe[1]);

	endptr = line;
	while ((bytes = read(mypipe[0], endptr, sizeof(line) - (size_t)(endptr - line) - 1)) > 0)
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
	    else if (Verbosity > 1)
	      fprintf(stderr, "%s: %s\n", job->printer->command, line);

	    bytes = ptr - line;
            if (ptr < endptr)
	      memmove(line, ptr, (size_t)(endptr - ptr));
	    endptr -= bytes;
	    *endptr = '\0';
	  }
	}

	close(mypipe[0]);
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

    if (status)
    {
#ifndef WIN32
      if (WIFEXITED(status))
#endif /* !WIN32 */
	fprintf(stderr, "Command \"%s\" exited with status %d.\n",
		job->printer->command, WEXITSTATUS(status));
#ifndef WIN32
      else
	fprintf(stderr, "Command \"%s\" terminated with signal %d.\n",
		job->printer->command, WTERMSIG(status));
#endif /* !WIN32 */
      job->state = IPP_JSTATE_ABORTED;
    }
    else if (status < 0)
      job->state = IPP_JSTATE_ABORTED;
    else
      fprintf(stderr, "Command \"%s\" completed successfully.\n",
	      job->printer->command);

   /*
    * Make sure processing takes at least 5 seconds...
    */

    time(&end);
    if ((end - start) < 5)
      sleep(5);
  }
  else
#endif // 0
  {
   /*
    * Sleep for a random amount of time to simulate job processing.
    */

    sleep((unsigned)(5 + (rand() % 11)));
  }

  if (job->cancel)
    job->state = IPP_JSTATE_CANCELED;
  else if (job->state == IPP_JSTATE_PROCESSING)
    job->state = IPP_JSTATE_COMPLETED;

  job->completed               = time(NULL);
  job->printer->state          = IPP_PSTATE_IDLE;
  job->printer->processing_job = NULL;

  serverAddEvent(job->printer, job, SERVER_EVENT_JOB_STATE_CHANGED, "Job fetchable.");

  return (NULL);
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

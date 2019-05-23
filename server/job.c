/*
 * Job object code for sample IPP server implementation.
 *
 * Copyright © 2014-2019 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * 'serverCheckJobs()' - Check for new jobs to process.
 */

void
serverCheckJobs(server_printer_t *printer)	/* I - Printer */
{
  server_job_t	*job;			/* Current job */


  serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Checking for new jobs to process.");

  if (printer->processing_job)
  {
    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Printer is already processing job %d.", printer->processing_job->id);
    return;
  }
  else if (printer->state == IPP_PSTATE_STOPPED)
  {
    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Printer is stopped.");
    return;
  }
  else if (printer->is_shutdown)
  {
    _cupsRWLockWrite(&printer->rwlock);

    printer->state = IPP_PSTATE_STOPPED;
    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Printer is now shutdown.");
    serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED | SERVER_EVENT_PRINTER_SHUTDOWN, "Printer shutdown.");
    _cupsRWUnlock(&printer->rwlock);
    return;
  }
  else if (printer->is_deleted)
  {
    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Printer is being deleted.");
    return;
  }
  else if (printer->state_reasons & SERVER_PREASON_MOVING_TO_PAUSED)
  {
    _cupsRWLockWrite(&printer->rwlock);
    printer->state         = IPP_PSTATE_STOPPED;
    printer->state_reasons |= SERVER_PREASON_PAUSED;
    printer->state_reasons &= (server_preason_t)~SERVER_PREASON_MOVING_TO_PAUSED;

    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Printer is now stopped.");
    serverAddEventNoLock(printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Printer is now stopped.");
    _cupsRWUnlock(&printer->rwlock);
    return;
  }

  _cupsRWLockWrite(&printer->rwlock);
  for (job = (server_job_t *)cupsArrayFirst(printer->active_jobs);
       job;
       job = (server_job_t *)cupsArrayNext(printer->active_jobs))
  {
    if (job->state == IPP_JSTATE_HELD && job->hold_until > 0 && job->hold_until <= time(NULL))
      serverReleaseJob(job);

    if (job->state == IPP_JSTATE_PENDING || (job->state == IPP_JSTATE_STOPPED && !(job->state_reasons & SERVER_JREASON_JOB_FETCHABLE)))
    {
      serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Starting job %d.", job->id);

      _cups_thread_t t = _cupsThreadCreate((_cups_thread_func_t)serverProcessJob, job);

      if (t)
      {
        _cupsThreadDetach(t);
      }
      else
      {
        _cupsRWLockWrite(&job->rwlock);

        job->state     = IPP_JSTATE_ABORTED;
	job->completed = time(NULL);

        serverAddEventNoLock(printer, job, NULL, SERVER_EVENT_JOB_COMPLETED, "Job aborted because creation of processing thread failed.");

        _cupsRWUnlock(&job->rwlock);
      }
      break;
    }
  }

  if (!job)
    serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "No jobs to process at this time.");

  _cupsRWUnlock(&printer->rwlock);
}


/*
 * 'serverCleanJobs()' - Clean out old (completed) jobs.
 */

void
serverCleanJobs(server_printer_t *printer)	/* I - Printer */
{
  server_job_t	*job;			/* Current job */
  time_t	cleantime;		/* Clean time */


  serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Cleaning jobs, %d completed jobs in memory...", cupsArrayCount(printer->completed_jobs));

  if (cupsArrayCount(printer->completed_jobs) == 0)
    return;

  cleantime = time(NULL) - 60;

  serverLogPrinter(SERVER_LOGLEVEL_DEBUG, printer, "Clean time is %ld.", (long)cleantime);

  _cupsRWLockWrite(&(printer->rwlock));
  for (job = (server_job_t *)cupsArrayFirst(printer->completed_jobs);
       job;
       job = (server_job_t *)cupsArrayNext(printer->completed_jobs))
    if (job->completed && job->completed < cleantime)
    {
     /*
      * Grab the write lock to make sure there are no readers of the job
      * object.  The printer write lock will prevent subsequent lookups of
      * jobs until we are done...
      */

      _cupsRWLockWrite(&job->rwlock);
      _cupsRWUnlock(&job->rwlock);

      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Cleaning job #%d.", job->id);
      cupsArrayRemove(printer->completed_jobs, job);
      cupsArrayRemove(printer->jobs, job); /* Last since removing a job from here calls serverDeleteJob() */
    }
    else if (job->completed)
      serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Not cleaning job #%d - completed on %ld.", job->id, (long)job->completed);
    else
      break;
  _cupsRWUnlock(&(printer->rwlock));
}


/*
 * 'serverCopyJobStateReasons()' - Copy printer-state-reasons values.
 */

void
serverCopyJobStateReasons(
    ipp_t        *ipp,			/* I - Attributes */
    ipp_tag_t    group_tag,		/* I - Group */
    server_job_t *job)			/* I - Printer */
{
  server_jreason_t	creasons;	/* Combined job-state-reasons */
  const char		*name;		/* Attribute name */


  if (group_tag == IPP_TAG_DOCUMENT)
    name = "document-state-reasons";
  else
    name = "job-state-reasons";

  creasons = job->state_reasons | job->dev_state_reasons;

  if (!creasons)
  {
    ippAddString(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), name, NULL, "none");
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

    ippAddStrings(ipp, group_tag, IPP_CONST_TAG(IPP_TAG_KEYWORD), name, num_reasons, NULL, reasons);
  }
}


/*
 * 'serverCreateJob()' - Create a new job object from a Print-Job or Create-Job
 *                  request.
 */

server_job_t *				/* O - Job */
serverCreateJob(
    server_client_t *client)		/* I - Client */
{
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Job attribute */
  char			uri[1024],	/* job-uri value */
			uuid[64];	/* job-uuid value */
  server_listener_t	*lis = (server_listener_t *)cupsArrayFirst(Listeners);
					/* First listener */


  _cupsRWLockWrite(&(client->printer->rwlock));

  if (MaxJobs > 0 && cupsArrayCount(client->printer->active_jobs) >= MaxJobs)
  {
    _cupsRWUnlock(&(client->printer->rwlock));
    return (NULL);
  }

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

  serverCopyAttributes(job->attrs, client->request, NULL, NULL, IPP_TAG_JOB, 0);

 /*
  * Get the requesting-user-name, document format, and priority...
  */

  if ((attr = ippFindAttribute(client->request, "job-priority", IPP_TAG_INTEGER)) != NULL)
    job->priority = ippGetInteger(attr, 0);
  else
    job->priority = 50;

  if (client->username[0])
    job->username = client->username;
  else if ((attr = ippFindAttribute(client->request, "requesting-user-name", IPP_TAG_NAME)) != NULL)
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

  snprintf(uri, sizeof(uri), "%s/%d", client->printer->default_uri, job->id);
  httpAssembleUUID(lis->host, lis->port, client->printer->name, job->id, uuid, sizeof(uuid));

  ippAddDate(job->attrs, IPP_TAG_JOB, "date-time-at-creation", ippTimeToDate(time(&job->created)));
  ippAddInteger(job->attrs, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-id", job->id);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uri", NULL, uri);
  ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-uuid", NULL, uuid);
  if ((attr = ippFindAttribute(client->request, "printer-uri", IPP_TAG_URI)) != NULL)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, ippGetString(attr, 0, NULL));
  else
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_URI, "job-printer-uri", NULL, client->printer->default_uri);
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
    server_job_t   *job,		/* I - Job */
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

  if (!strcasecmp(format, "application/pdf"))
    ext = "pdf";
  else if (!strcasecmp(format, "application/postscript"))
    ext = "ps";
  else if (!strcasecmp(format, "application/sla"))
    ext = "stl";
  else if (!strcasecmp(format, "application/vnd.hp-pcl"))
    ext = "pcl";
  else if (!strcasecmp(format, "application/vnd.pwg-safegcode"))
    ext = "gcode";
  else if (!strcasecmp(format, "application/vnd.pwg-xhtml-print+xml") || !strcasecmp(format, "application/xml+xhtml"))
    ext = "xhtml";
  else if (!strcasecmp(format, "image/jpeg"))
    ext = "jpg";
  else if (!strcasecmp(format, "image/png"))
    ext = "png";
  else if (!strcasecmp(format, "image/pwg-raster"))
    ext = "ras";
  else if (!strcasecmp(format, "image/urf"))
    ext = "apple";
  else if (!strcasecmp(format, "model/3mf") || !strcasecmp(format, "model/3mf+slice"))
    ext = "3mf";
  else if (!strcasecmp(format, "model/amf"))
    ext = "amf";
  else if (!strcasecmp(format, "text/html"))
    ext = "html";
  else if (!strcasecmp(format, "text/markdown"))
    ext = "md";
  else if (!strcasecmp(format, "text/plain"))
    ext = "txt";
  else
    ext = "prn";

 /*
  * Create a filename with the job-id, job-name, and document-format (extension)...
  */

  snprintf(fname, fnamesize, "%s/%s/%d-%s.%s", SpoolDirectory, job->printer->name, job->id, name, ext);
}


/*
 * 'serverDeleteJob()' - Remove from the printer and free all memory used by a job
 *                  object.
 */

void
serverDeleteJob(server_job_t *job)		/* I - Job */
{
  serverLogJob(SERVER_LOGLEVEL_DEBUG, job, "Removing job #%d from history.", job->id);

  _cupsRWLockWrite(&job->rwlock);

  ippDelete(job->attrs);
  ippDelete(job->doc_attrs);

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

server_job_t *				/* O - Job or NULL */
serverFindJob(
    server_client_t *client,		/* I - Client */
    int             job_id)		/* I - Job ID to find or 0 to lookup */
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
    const char	*uri = ippGetString(attr, 0, NULL);
					/* job-uri value */
    char	scheme[32],		/* URI scheme */
		userpass[256],		/* username:password */
		host[256],		/* Hostname/IP */
		resource[1024];		/* Resource path */
    int		port;			/* Port number */

    if (httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) >= HTTP_URI_STATUS_OK &&
        !strncmp(resource, client->printer->resource, client->printer->resourcelen) &&
        resource[client->printer->resourcelen] == '/')
      key.id = atoi(resource + client->printer->resourcelen + 1);
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
        jreasons |= (server_jreason_t)(1 << j);
	break;
      }
    }
  }

  return (jreasons);
}


/*
 * 'serverHoldJob()' - Hold a print job.
 */

int					/* O - 1 on success, 0 on failure */
serverHoldJob(
    server_job_t    *job,		/* I - Job to hold */
    ipp_attribute_t *hold_until)	/* I - Hold-until condition */
{
  ipp_attribute_t	*attr;		/* job-hold-until-xxx attribute */
  const char		*keyword;	/* job-hold-until keyword */


  _cupsRWLockWrite(&job->rwlock);

  if (job->state > IPP_JSTATE_HELD)
  {
    _cupsRWUnlock(&job->rwlock);
    return (0);
  }

  job->state = IPP_JSTATE_HELD;

  if (hold_until)
    job->state_reasons |= SERVER_JREASON_JOB_HOLD_UNTIL_SPECIFIED;
  else
    job->state_reasons &= (server_jreason_t)~SERVER_JREASON_JOB_HOLD_UNTIL_SPECIFIED;

  if (ippGetValueTag(hold_until) == IPP_TAG_DATE)
  {
    job->hold_until = ippDateToTime(ippGetDate(hold_until, 0));
  }
  else
  {
    time_t	curtime;		/* Current time */
    struct tm	*curdate;		/* Current date */

    curtime = time(NULL);
    curdate = localtime(&curtime);

    if ((keyword = ippGetString(hold_until, 0, NULL)) == NULL)
      keyword = "indefinite";

    if (!strcmp(keyword, "evening") || !strcmp(keyword, "night"))
    {
     /*
      * Hold to 6pm unless local time is > 6pm or < 6am.
      */

      if (curdate->tm_hour < 6 || curdate->tm_hour >= 18)
	job->hold_until = curtime;
      else
	job->hold_until = curtime + ((17 - curdate->tm_hour) * 60 + 59 - curdate->tm_min) * 60 + 60 - curdate->tm_sec;
    }
    else if (!strcmp(keyword, "second-shift"))
    {
     /*
      * Hold to 4pm unless local time is > 4pm.
      */

      if (curdate->tm_hour >= 16)
	job->hold_until = curtime;
      else
	job->hold_until = curtime + ((15 - curdate->tm_hour) * 60 + 59 - curdate->tm_min) * 60 + 60 - curdate->tm_sec;
    }
    else if (!strcmp(keyword, "third-shift"))
    {
     /*
      * Hold to 12am unless local time is < 8am.
      */

      if (curdate->tm_hour < 8)
	job->hold_until = curtime;
      else
	job->hold_until = curtime + ((23 - curdate->tm_hour) * 60 + 59 - curdate->tm_min) * 60 + 60 - curdate->tm_sec;
    }
    else if (!strcmp(keyword, "weekend"))
    {
     /*
      * Hold to weekend unless we are in the weekend.
      */

      if (curdate->tm_wday == 0 || curdate->tm_wday == 6)
	job->hold_until = curtime;
      else
	job->hold_until = curtime + (((5 - curdate->tm_wday) * 24 + (17 - curdate->tm_hour)) * 60 + 59 - curdate->tm_min) * 60 + 60 - curdate->tm_sec;
    }
    else
    {
     /*
      * Any other value maps to "indefinite" - hold until released.
      */

      job->hold_until = -1;
    }
  }

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO)) != NULL)
  {
    if (!hold_until)
      ippSetString(job->attrs, &attr, 0, "none");
    else if (ippGetValueTag(hold_until) == IPP_TAG_DATE)
      ippDeleteAttribute(job->attrs, attr);
    else
      ippSetString(job->attrs, &attr, 0, ippGetString(hold_until, 0, NULL));
  }
  else if (!hold_until)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-hold-until", NULL, "none");
  else if (ippGetValueTag(hold_until) != IPP_TAG_DATE)
    ippAddString(job->attrs, IPP_TAG_JOB, IPP_TAG_KEYWORD, "job-hold-until", NULL, ippGetString(hold_until, 0, NULL));

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until-time", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetValueTag(hold_until) == IPP_TAG_DATE)
      ippSetDate(job->attrs, &attr, 0, ippGetDate(hold_until, 0));
    else
      ippDeleteAttribute(job->attrs, attr);
  }
  else if (ippGetValueTag(hold_until) == IPP_TAG_DATE)
    ippAddDate(job->attrs, IPP_TAG_JOB, "job-hold-until-time", ippGetDate(hold_until, 0));

  serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED, "Job held.");

  _cupsRWUnlock(&job->rwlock);

  return (1);
}


/*
 * 'serverProcessJob()' - Process a print job.
 */

void *					/* O - Thread exit status */
serverProcessJob(server_job_t *job)	/* I - Job */
{
  _cupsRWLockWrite(&job->rwlock);

  job->state                   = IPP_JSTATE_PROCESSING;
  job->printer->state          = IPP_PSTATE_PROCESSING;
  job->processing              = time(NULL);
  job->printer->processing_job = job;

  serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED, "Job processing.");

  _cupsRWUnlock(&job->rwlock);

  while (job->printer->state_reasons & SERVER_PREASON_MEDIA_EMPTY)
  {
    _cupsRWLockWrite(&job->printer->rwlock);
    job->printer->state_reasons |= SERVER_PREASON_MEDIA_NEEDED;
    _cupsRWUnlock(&job->printer->rwlock);

    sleep(1);
  }

  _cupsRWLockWrite(&job->printer->rwlock);
  job->printer->state_reasons &= (server_preason_t)~SERVER_PREASON_MEDIA_NEEDED;
  _cupsRWUnlock(&job->printer->rwlock);

  if (job->printer->pinfo.command)
  {
   /*
    * Execute a command with the job spool file and wait for it to complete...
    */

    serverTransformJob(NULL, job, job->printer->pinfo.command, job->printer->pinfo.output_format, SERVER_TRANSFORM_COMMAND);
  }
  else if (job->printer->pinfo.proxy_group != SERVER_GROUP_NONE)
  {
   /*
    * Prepare the job for the proxy...
    */

    _cupsRWLockWrite(&job->rwlock);

    job->state         = IPP_JSTATE_STOPPED;
    job->state_reasons |= SERVER_JREASON_JOB_FETCHABLE;

    serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED | SERVER_EVENT_JOB_FETCHABLE, "Job fetchable.");

    _cupsRWUnlock(&job->rwlock);
  }
  else
  {
   /*
    * Sleep for a random amount of time to simulate job processing.
    */

    sleep((unsigned)(1 + (CUPS_RAND() % 4)));
  }

  _cupsRWLockWrite(&job->rwlock);

  if (job->cancel)
    job->state = IPP_JSTATE_CANCELED;
  else if (job->state == IPP_JSTATE_PROCESSING)
    job->state = IPP_JSTATE_COMPLETED;

  _cupsRWLockWrite(&job->printer->rwlock);

  if (job->printer->state_reasons & SERVER_PREASON_MOVING_TO_PAUSED)
  {
    job->printer->state         = IPP_PSTATE_STOPPED;
    job->printer->state_reasons &= (server_preason_t)~SERVER_PREASON_MOVING_TO_PAUSED;
    job->printer->state_reasons |= SERVER_PREASON_PAUSED;

    serverAddEventNoLock(job->printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED | SERVER_EVENT_PRINTER_STOPPED, "Printer stopped.");
  }
  else if (job->printer->is_deleted)
  {
    job->printer->state = IPP_PSTATE_STOPPED;
  }
  else
  {
    job->printer->state = IPP_PSTATE_IDLE;

    if (job->printer->state_reasons & SERVER_PREASON_PRINTER_RESTARTED)
    {
      serverAddEventNoLock(job->printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED | SERVER_EVENT_PRINTER_RESTARTED, "Printer restarted.");

      job->printer->state_reasons &= (server_preason_t)~SERVER_PREASON_PRINTER_RESTARTED;
    }
  }

  job->printer->processing_job = NULL;

  if (job->state >= IPP_JSTATE_CANCELED)
  {
    job->completed = time(NULL);

    serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED | SERVER_EVENT_JOB_COMPLETED, job->state == IPP_JSTATE_COMPLETED ? "Job completed." : job->state == IPP_JSTATE_ABORTED ? "Job aborted." : "Job canceled.");

    cupsArrayAdd(job->printer->completed_jobs, job);
    cupsArrayRemove(job->printer->active_jobs, job);

    if (MaxCompletedJobs > 0)
    {
     /*
      * Make sure the job history doesn't go over the limit...
      */

      while (cupsArrayCount(job->printer->completed_jobs) > MaxCompletedJobs)
      {
	server_job_t *tjob = (server_job_t *)cupsArrayFirst(job->printer->completed_jobs);

	if (tjob == job)
	  tjob = (server_job_t *)cupsArrayNext(job->printer->completed_jobs);

	cupsArrayRemove(job->printer->completed_jobs, tjob);
	cupsArrayRemove(job->printer->jobs, tjob); /* Removing here calls serverDeleteJob */
      }
    }
  }

  _cupsRWUnlock(&job->printer->rwlock);
  _cupsRWUnlock(&job->rwlock);

  if (job->printer->is_deleted)
    serverDeletePrinter(job->printer);
  else if (!job->printer->is_shutdown)
    serverCheckJobs(job->printer);

  return (NULL);
}


/*
 * 'serverReleaseJob()' - Release a held print job.
 */

int					/* O - 1 on success, 0 on failure */
serverReleaseJob(server_job_t *job)	/* I - Job to release */
{
  ipp_attribute_t	*attr;		/* Hold-until attribute */


  if (job->state != IPP_JSTATE_HELD)
    return (0);

  _cupsRWLockWrite(&job->rwlock);

  job->state         = IPP_JSTATE_PENDING;
  job->state_reasons &= (server_jreason_t)~SERVER_JREASON_JOB_HOLD_UNTIL_SPECIFIED;

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO)) != NULL)
    ippDeleteAttribute(job->attrs, attr);

  if ((attr = ippFindAttribute(job->attrs, "job-hold-until-time", IPP_TAG_ZERO)) != NULL)
    ippDeleteAttribute(job->attrs, attr);

  serverAddEventNoLock(job->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED, "Job released.");

  _cupsRWUnlock(&job->rwlock);

  return (1);
}

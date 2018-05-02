/*
 * IPP processing code for sample IPP server implementation.
 *
 * Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * Local types...
 */

typedef struct server_value_s		/**** Value Tag Mapping ****/
{
  const char	*name;			/* Attribute name */
  ipp_tag_t	value_tag;		/* Value tag */
} server_value_t;


/*
 * Local functions...
 */

static inline int	check_attribute(const char *name, cups_array_t *ra, cups_array_t *pa)
{
  return ((!pa || !cupsArrayFind(pa, (void *)name)) && (!ra || cupsArrayFind(ra, (void *)name)));
}
static void		copy_doc_attributes(server_client_t *client, server_job_t *job, cups_array_t *ra, cups_array_t *pa);
static void		copy_job_attributes(server_client_t *client, server_job_t *job, cups_array_t *ra, cups_array_t *pa);
static void		copy_subscription_attributes(server_client_t *client, server_subscription_t *sub, cups_array_t *ra, cups_array_t *pa);
static int		filter_cb(server_filter_t *filter, ipp_t *dst, ipp_attribute_t *attr);
static void		ipp_acknowledge_document(server_client_t *client);
static void		ipp_acknowledge_identify_printer(server_client_t *client);
static void		ipp_acknowledge_job(server_client_t *client);
static void		ipp_cancel_job(server_client_t *client);
static void		ipp_cancel_my_jobs(server_client_t *client);
static void		ipp_cancel_subscription(server_client_t *client);
static void		ipp_close_job(server_client_t *client);
static void		ipp_create_job(server_client_t *client);
static void		ipp_create_xxx_subscriptions(server_client_t *client);
static void		ipp_deregister_output_device(server_client_t *client);
static void		ipp_fetch_document(server_client_t *client);
static void		ipp_fetch_job(server_client_t *client);
static void		ipp_get_document_attributes(server_client_t *client);
static void		ipp_get_documents(server_client_t *client);
static void		ipp_get_job_attributes(server_client_t *client);
static void		ipp_get_jobs(server_client_t *client);
static void		ipp_get_notifications(server_client_t *client);
static void		ipp_get_output_device_attributes(server_client_t *client);
static void		ipp_get_printer_attributes(server_client_t *client);
static void		ipp_get_printer_supported_values(server_client_t *client);
static void		ipp_get_subscription_attributes(server_client_t *client);
static void		ipp_get_subscriptions(server_client_t *client);
static void		ipp_get_system_attributes(server_client_t *client);
static void		ipp_get_system_supported_values(server_client_t *client);
static void		ipp_identify_printer(server_client_t *client);
static void		ipp_print_job(server_client_t *client);
static void		ipp_print_uri(server_client_t *client);
static void		ipp_renew_subscription(server_client_t *client);
static void		ipp_send_document(server_client_t *client);
static void		ipp_send_uri(server_client_t *client);
//static void		ipp_set_job_attributes(server_client_t *client);
//static void		ipp_set_printer_attributes(server_client_t *client);
//static void		ipp_set_subscription_attributes(server_client_t *client);
static void		ipp_set_system_attributes(server_client_t *client);
static void		ipp_update_active_jobs(server_client_t *client);
static void		ipp_update_document_status(server_client_t *client);
static void		ipp_update_job_status(server_client_t *client);
static void		ipp_update_output_device_attributes(server_client_t *client);
static void		ipp_validate_document(server_client_t *client);
static void		ipp_validate_job(server_client_t *client);
static int		valid_doc_attributes(server_client_t *client);
static int		valid_job_attributes(server_client_t *client);


/*
 * 'serverCopyAttributes()' - Copy attributes from one request to another.
 */

void
serverCopyAttributes(
    ipp_t        *to,			/* I - Destination request */
    ipp_t        *from,			/* I - Source request */
    cups_array_t *ra,			/* I - Requested attributes */
    cups_array_t *pa,			/* I - Private attributes */
    ipp_tag_t    group_tag,		/* I - Group to copy */
    int          quickcopy)		/* I - Do a quick copy? */
{
  server_filter_t	filter;		/* Filter data */


  filter.ra        = ra;
  filter.pa        = pa;
  filter.group_tag = group_tag;

  ippCopyAttributes(to, from, quickcopy, (ipp_copycb_t)filter_cb, &filter);
}


/*
 * 'copy_doc_attrs()' - Copy document attributes to the response.
 */

static void
copy_doc_attributes(
    server_client_t *client,		/* I - Client */
    server_job_t    *job,		/* I - Job */
    cups_array_t    *ra,		/* I - requested-attributes */
    cups_array_t    *pa)		/* I - Private attributes */
{
  const char		*name;		/* Attribute name */
  ipp_attribute_t	*srcattr;	/* Source attribute */


 /*
  * Synthesize/copy the following Document Description/Status attributes:
  *
  *   compression ("none")
  *   date-time-at-xxx
  *   document-access-errors
  *   document-job-id (from job-id)
  *   document-job-uri (from job-uri)
  *   document-printer-uri (from job-printer-uri)
  *   document-metadata
  *   document-number (1)
  *   document-name
  *   document-uri
  *   document-uuid (from job-uuid)
  *   impressions (from job-impressions)
  *   impressions-col (from job-impressions-col)
  *   impressions-completed (from job-impressions-completed)
  *   impressions-completed-col (from job-impressions-completed-col)
  *   k-octets (from job-k-octets)
  *   last-document (true)
  *   media-sheets (from job-media-sheets)
  *   media-sheets-col (from job-media-sheets-col)
  *   media-sheets-completed (from job-media-sheets-completed)
  *   media-sheets-completed-col (from job-media-sheets-completed-col)
  *   pages (from job-pages)
  *   pages-col (from job-pages-col)
  *   pages-completed (from job-pages-completed)
  *   pages-completed-col (from job-pages-completed-col)
  *   time-at-xxx
  */

  serverCopyAttributes(client->response, job->attrs, ra, pa, IPP_TAG_DOCUMENT, 0);

  for (srcattr = ippFirstAttribute(job->attrs); srcattr; srcattr = ippNextAttribute(job->attrs))
  {
    if (ippGetGroupTag(srcattr) != IPP_TAG_JOB || (name = ippGetName(srcattr)) == NULL)
      continue;

    if ((!strncmp(name, "job-impressions", 15) || !strncmp(name, "job-k-octets", 12) || !strncmp(name, "job-media-sheets", 16) || !strncmp(name, "job-pages", 9)) && check_attribute(name + 4, ra, pa))
    {
      name += 4;

      if (strstr(name, "-col"))
        ippAddCollection(client->response, IPP_TAG_DOCUMENT, name, ippGetCollection(srcattr, 0));
      else
        ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, name, ippGetInteger(srcattr, 0));
    }
    else if (!strcmp(name, "document-uri") && check_attribute("document-uri", ra, pa))
      ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_URI, "document-uri", NULL, ippGetString(srcattr, 0, NULL));
    else if (!strcmp(name, "document-name") && check_attribute("document-name", ra, pa))
      ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_NAME, "document-name", NULL, ippGetString(srcattr, 0, NULL));
    else if (!strcmp(name, "job-printer-uri") && check_attribute("document-printer-uri", ra, pa))
      ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_URI, "document-printer-uri", NULL, ippGetString(srcattr, 0, NULL));
    else if (!strcmp(name, "job-uri") && check_attribute("document-job-uri", ra, pa))
      ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_URI, "document-job-uri", NULL, ippGetString(srcattr, 0, NULL));
    else if (!strcmp(name, "job-uuid") && check_attribute("document-uuid", ra, pa))
      ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_URI, "document-uuid", NULL, ippGetString(srcattr, 0, NULL));
  }

  if (check_attribute("compression", ra, pa))
    ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_CONST_TAG(IPP_TAG_KEYWORD), "compression", NULL, "none");

  if (check_attribute("date-time-at-completed", ra, pa))
  {
    if (job->completed)
      ippAddDate(client->response, IPP_TAG_DOCUMENT, "date-time-at-completed", ippTimeToDate(job->completed));
    else
      ippAddOutOfBand(client->response, IPP_TAG_DOCUMENT, IPP_TAG_NOVALUE, "date-time-at-completed");
  }

  if (check_attribute("date-time-at-created", ra, pa))
    ippAddDate(client->response, IPP_TAG_DOCUMENT, "date-time-at-created", ippTimeToDate(job->created));

  if (check_attribute("date-time-at-processing", ra, pa))
  {
    if (job->processing)
      ippAddDate(client->response, IPP_TAG_DOCUMENT, "date-time-at-processing", ippTimeToDate(job->processing));
    else
      ippAddOutOfBand(client->response, IPP_TAG_DOCUMENT, IPP_TAG_NOVALUE, "date-time-at-processing");
  }

  if (check_attribute("document-format", ra, pa))
    ippAddString(client->response, IPP_TAG_DOCUMENT, IPP_TAG_MIMETYPE, "document-format", NULL, job->format);

  if (check_attribute("document-job-id", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, "document-job-id", job->id);

  if (check_attribute("document-number", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, "document-number", 1);

  if (check_attribute("document-state", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_ENUM, "document-state", job->state);

  if (check_attribute("document-state-reasons", ra, pa))
    serverCopyJobStateReasons(client->response, IPP_TAG_DOCUMENT, job);

  if (check_attribute("impressions", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, "job-impressions", job->impressions);

  if (check_attribute("impressions-completed", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, "job-impressions-completed", job->impcompleted);

  if (check_attribute("last-document", ra, pa))
    ippAddBoolean(client->response, IPP_TAG_DOCUMENT, "last-document", 1);

  if (check_attribute("time-at-completed", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, job->completed ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE, "time-at-completed", (int)(job->completed - client->printer->start_time));

  if (check_attribute("time-at-created", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, IPP_TAG_INTEGER, "time-at-created", (int)(job->created - client->printer->start_time));

  if (check_attribute("time-at-processing", ra, pa))
    ippAddInteger(client->response, IPP_TAG_DOCUMENT, job->processing ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE, "time-at-processing", (int)(job->processing - client->printer->start_time));
}


/*
 * 'copy_job_attrs()' - Copy job attributes to the response.
 */

static void
copy_job_attributes(
    server_client_t *client,		/* I - Client */
    server_job_t    *job,		/* I - Job */
    cups_array_t    *ra,		/* I - requested-attributes */
    cups_array_t    *pa)		/* I - Private attributes */
{
  serverCopyAttributes(client->response, job->attrs, ra, pa, IPP_TAG_JOB, 0);

  if (check_attribute("date-time-at-completed", ra, pa))
  {
    if (job->completed)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-completed", ippTimeToDate(job->completed));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-completed");
  }

  if (check_attribute("date-time-at-processing", ra, pa))
  {
    if (job->processing)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-processing", ippTimeToDate(job->processing));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-processing");
  }

  if (check_attribute("job-impressions", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions", job->impressions);

  if (check_attribute("job-impressions-completed", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions-completed", job->impcompleted);

  if (check_attribute("job-printer-up-time", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-printer-up-time", (int)(time(NULL) - client->printer->start_time));

  if (check_attribute("job-state", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_ENUM, "job-state", job->state);

  if (check_attribute("job-state-message", ra, pa))
  {
    if (job->dev_state_message)
    {
      ippAddString(client->response, IPP_TAG_JOB, IPP_TAG_TEXT, "job-state-message", NULL, job->dev_state_message);
    }
    else
    {
      const char *message = "";		/* Message string */

      switch (job->state)
      {
	case IPP_JSTATE_PENDING :
	    message = "Job pending.";
	    break;

	case IPP_JSTATE_HELD :
	    if (job->state_reasons & SERVER_JREASON_JOB_INCOMING)
	      message = "Job incoming.";
	    else if (ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO))
	      message = "Job held.";
	    else
	      message = "Job created.";
	    break;

	case IPP_JSTATE_PROCESSING :
	    if (job->state_reasons & SERVER_JREASON_PROCESSING_TO_STOP_POINT)
	    {
	      if (job->cancel)
		message = "Cancel in progress.";
	      else
	        message = "Abort in progress.";
	    }
	    else
	      message = "Job printing.";
	    break;

	case IPP_JSTATE_STOPPED :
	    message = "Job stopped.";
	    break;

	case IPP_JSTATE_CANCELED :
	    message = "Job canceled.";
	    break;

	case IPP_JSTATE_ABORTED :
	    message = "Job aborted.";
	    break;

	case IPP_JSTATE_COMPLETED :
	    message = "Job completed.";
	    break;
      }

      ippAddString(client->response, IPP_TAG_JOB, IPP_CONST_TAG(IPP_TAG_TEXT), "job-state-message", NULL, message);
    }
  }

  if (check_attribute("job-state-reasons", ra, pa))
    serverCopyJobStateReasons(client->response, IPP_TAG_JOB, job);

  if (check_attribute("number-of-documents", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "number-of-documents", job->filename ? 1 : 0);

  if (check_attribute("time-at-completed", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, job->completed ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE, "time-at-completed", (int)(job->completed - client->printer->start_time));

  if (check_attribute("time-at-processing", ra, pa))
    ippAddInteger(client->response, IPP_TAG_JOB, job->processing ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE, "time-at-processing", (int)(job->processing - client->printer->start_time));
}


/*
 * 'copy_sub_attrs()' - Copy job attributes to the response.
 */

static void
copy_subscription_attributes(
    server_client_t       *client,	/* I - Client */
    server_subscription_t *sub,		/* I - Subscription */
    cups_array_t          *ra,		/* I - requested-attributes */
    cups_array_t          *pa)		/* I - Private attributes */
{
  serverCopyAttributes(client->response, sub->attrs, ra, pa, IPP_TAG_SUBSCRIPTION, 0);

  if (check_attribute("notify-lease-expiration-time", ra, pa))
    ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-lease-expiration-time", (int)(sub->expire - client->printer->start_time));

  if (check_attribute("notify-printer-up-time", ra, pa))
    ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-printer-up-time", (int)(time(NULL) - client->printer->start_time));

  if (check_attribute("notify-sequence-number", ra, pa))
    ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-sequence-number", sub->last_sequence);
}


/*
 * 'filter_cb()' - Filter printer attributes based on the requested array.
 */

static int				/* O - 1 to copy, 0 to ignore */
filter_cb(server_filter_t   *filter,	/* I - Filter parameters */
          ipp_t           *dst,		/* I - Destination (unused) */
	  ipp_attribute_t *attr)	/* I - Source attribute */
{
 /*
  * Filter attributes as needed...
  */

#ifndef WIN32 /* Avoid MS compiler bug */
  (void)dst;
#endif /* !WIN32 */

  ipp_tag_t group = ippGetGroupTag(attr);
  const char *name = ippGetName(attr);

  if ((filter->group_tag != IPP_TAG_ZERO && group != filter->group_tag && group != IPP_TAG_ZERO) || !name || (!strcmp(name, "media-col-database") && !cupsArrayFind(filter->ra, (void *)name)))
    return (0);

  if (filter->pa && cupsArrayFind(filter->pa, (void *)name))
    return (0);

  return (!filter->ra || cupsArrayFind(filter->ra, (void *)name) != NULL);
}


/*
 * 'ipp_acknowledge_document()' - Acknowledge receipt of a document.
 */

static void
ipp_acknowledge_document(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (!job->dev_uuid || strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job not assigned to device.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "document-number", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(attr) != IPP_TAG_OPERATION || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, attr ? "Bad document-number attribute." : "Missing document-number attribute.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_acknowledge_identify_printer()' - Acknowledge an identify command.
 */

static void
ipp_acknowledge_identify_printer(
    server_client_t *client)		/* I - Client */
{
  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  _cupsRWLockWrite(&client->printer->rwlock);

  if (client->printer->identify_actions)
  {
    static const char * const identify_actions[] =
    {
      "display",
      "sound"
    };

    serverRespondIPP(client, IPP_STATUS_OK, NULL);

    if (client->printer->identify_actions == SERVER_IDENTIFY_DISPLAY)
      ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "identify-actions", NULL, "display");
    else if (client->printer->identify_actions == SERVER_IDENTIFY_SOUND)
      ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "identify-actions", NULL, "sound");
    else
      ippAddStrings(client->response, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "identify-actions", 2, NULL, identify_actions);
    client->printer->identify_actions = SERVER_IDENTIFY_NONE;

    if (client->printer->identify_message)
    {
      ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_TEXT, "message", NULL, client->printer->identify_message);
      free(client->printer->identify_message);
      client->printer->identify_message = NULL;
    }

    client->printer->state_reasons &= (unsigned)~SERVER_PREASON_IDENTIFY_PRINTER_REQUESTED;

    serverAddEvent(client->printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Identify-Printer request received.");
  }
  else
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "No pending Identify-Printer request.");

  _cupsRWUnlock(&client->printer->rwlock);
}


/*
 * 'ipp_acknowledge_job()' - Acknowledge receipt of a job.
 */

static void
ipp_acknowledge_job(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (job->dev_uuid && strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Job not assigned to device.");
    return;
  }

  if (!(job->state_reasons & SERVER_JREASON_JOB_FETCHABLE))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Job not fetchable.");
    return;
  }

  if (!job->dev_uuid)
    job->dev_uuid = strdup(device->uuid);

  job->state_reasons &= (server_jreason_t)~SERVER_JREASON_JOB_FETCHABLE;

  serverAddEvent(client->printer, job, NULL, SERVER_EVENT_JOB_STATE_CHANGED, "Job acknowledged.");

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_cancel_job()' - Cancel a job.
 */

static void
ipp_cancel_job(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* Job information */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

 /*
  * See if the job is already completed, canceled, or aborted; if so,
  * we can't cancel...
  */

  switch (job->state)
  {
    case IPP_JSTATE_CANCELED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already canceled - can\'t cancel.", job->id);
        break;

    case IPP_JSTATE_ABORTED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already aborted - can\'t cancel.", job->id);
        break;

    case IPP_JSTATE_COMPLETED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already completed - can\'t cancel.", job->id);
        break;

    default :
       /*
        * Cancel the job...
	*/

	_cupsRWLockWrite(&(client->printer->rwlock));

	if (job->state == IPP_JSTATE_PROCESSING ||
	    (job->state == IPP_JSTATE_HELD && job->fd >= 0))
          job->cancel = 1;
	else
	{
	  job->state     = IPP_JSTATE_CANCELED;
	  job->completed = time(NULL);
	}

	_cupsRWUnlock(&(client->printer->rwlock));

        serverAddEvent(client->printer, job, NULL, SERVER_EVENT_JOB_COMPLETED, NULL);

	serverRespondIPP(client, IPP_STATUS_OK, NULL);
        break;
  }
}


/*
 * 'ipp_cancel_my_jobs()' - Cancel a user's jobs.
 */

static void
ipp_cancel_my_jobs(
    server_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*attr,		/* Current attribute */
			*job_ids,	/* List of job-id's to cancel */
			*bad_job_ids = NULL;
					/* List of bad job-id values */
  const char		*username;	/* Username */
  server_job_t		*job;		/* Current job pointer */
  cups_array_t		*to_cancel;	/* Jobs to cancel */


 /*
  * See which user is canceling jobs...
  */

  if (Authentication)
  {
   /*
    * Use authenticated username...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    username = client->username;
  }
  else if ((attr = ippFindAttribute(client->request, "requesting-user-name", IPP_TAG_NAME)) == NULL)
  {
   /*
    * No authentication and no requesting-user-name...
    */

    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Need requesting-user-name with Cancel-My-Jobs.");
    return;
  }
  else
  {
   /*
    * Use requesting-user-name value...
    */

    username = ippGetString(attr, 0, NULL);
  }

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Cancel-My-Jobs username='%s'", username);

 /*
  * and then see if a list of jobs was provided...
  */

  job_ids = ippFindAttribute(client->request, "job-ids", IPP_TAG_INTEGER);

 /*
  * OK, cancel jobs on this printer...
  */

  _cupsRWLockRead(&(client->printer->rwlock));

  to_cancel = cupsArrayNew(NULL, NULL);

  if (job_ids)
  {
   /*
    * Look for the specified jobs...
    */

    int			i,		/* Looping var */
			count;		/* Number of job-ids values */
    server_job_t	key;		/* Search key for jobs */

    for (i = 0, count = ippGetCount(job_ids); i < count; i ++)
    {
      key.id = ippGetInteger(job_ids, i);

      if ((job = (server_job_t *)cupsArrayFind(client->printer->jobs, &key)) != NULL)
      {
       /*
	* Validate this job...
	*/

	if (_cups_strcasecmp(username, job->username))
	{
	  if (!bad_job_ids)
	  {
	    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Job #%d is owned by another user.", job->id);

	    bad_job_ids = ippAddInteger(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "job-ids", job->id);
	  }
	  else
	    ippSetInteger(client->response, &bad_job_ids, ippGetCount(bad_job_ids), job->id);
	}
	else if (job->state >= IPP_JSTATE_CANCELED)
	{
	  if (!bad_job_ids)
	  {
	    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job #%d cannot be canceled.", job->id);

	    bad_job_ids = ippAddInteger(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "job-ids", job->id);
	  }
	  else
	    ippSetInteger(client->response, &bad_job_ids, ippGetCount(bad_job_ids), job->id);
	}
	else
	  cupsArrayAdd(to_cancel, job);
      }
      else if (!bad_job_ids)
      {
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job #%d does not exist.", key.id);

	bad_job_ids = ippAddInteger(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "job-ids", key.id);
      }
      else
	ippSetInteger(client->response, &bad_job_ids, ippGetCount(bad_job_ids), key.id);
    }
  }
  else
  {
   /*
    * Look for jobs belonging to the requesting user...
    */

    for (job = (server_job_t *)cupsArrayFirst(client->printer->jobs); job; job = (server_job_t *)cupsArrayNext(client->printer->jobs))
    {
      if (!_cups_strcasecmp(username, job->username) && job->state < IPP_JSTATE_CANCELED)
        cupsArrayAdd(to_cancel, job);
    }
  }

  if (!bad_job_ids)
  {
   /*
    * If we got this far then we have a valid list of jobs to cancel...
    */

    for (job = (server_job_t *)cupsArrayFirst(to_cancel); job; job = (server_job_t *)cupsArrayNext(to_cancel))
    {
      if (job->state == IPP_JSTATE_PROCESSING || (job->state == IPP_JSTATE_HELD && job->fd >= 0))
      {
	job->cancel = 1;
      }
      else
      {
	job->state     = IPP_JSTATE_CANCELED;
	job->completed = time(NULL);
      }

      serverAddEvent(client->printer, job, NULL, SERVER_EVENT_JOB_COMPLETED, NULL);
    }

    serverRespondIPP(client, IPP_STATUS_OK, NULL);
  }

  cupsArrayDelete(to_cancel);

  _cupsRWUnlock(&(client->printer->rwlock));
}


/*
 * 'ipp_cancel_subscription()' - Cancel a subscription.
 */

static void
ipp_cancel_subscription(
    server_client_t *client)		/* I - Client */
{
  server_subscription_t	*sub;		/* Subscription */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, sub->username, SERVER_GROUP_NONE, SubscriptionPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this subscription.");
    return;
  }

  _cupsRWLockWrite(&SubscriptionsRWLock);
  cupsArrayRemove(Subscriptions, sub);
  serverDeleteSubscription(sub);
  _cupsRWUnlock(&SubscriptionsRWLock);
  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_close_job()' - Close an open job.
 */

static void
ipp_close_job(server_client_t *client)	/* I - Client */
{
  server_job_t	*job;			/* Job information */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

 /*
  * See if the job is already completed, canceled, or aborted; if so,
  * we can't cancel...
  */

  switch (job->state)
  {
    case IPP_JSTATE_CANCELED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is canceled - can\'t close.", job->id);
        break;

    case IPP_JSTATE_ABORTED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is aborted - can\'t close.", job->id);
        break;

    case IPP_JSTATE_COMPLETED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is completed - can\'t close.", job->id);
        break;

    case IPP_JSTATE_PROCESSING :
    case IPP_JSTATE_STOPPED :
	serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
		    "Job #%d is already closed.", job->id);
        break;

    default :
	serverRespondIPP(client, IPP_STATUS_OK, NULL);
        break;
  }
}


/*
 * 'ipp_create_job()' - Create a job object.
 */

static void
ipp_create_job(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* New job */
  cups_array_t		*ra;		/* Attributes to send in response */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
    return;

 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Unexpected document data following request.");
    return;
  }

 /*
  * Create the job...
  */

  if ((job = serverCreateJob(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_TOO_MANY_JOBS, "Too many jobs are queued.");
    return;
  }

 /*
  * Return the job info...
  */

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-message");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra, NULL);
  cupsArrayDelete(ra);

 /*
  * Add any subscriptions...
  */

  client->job = job;
  ipp_create_xxx_subscriptions(client);
}


/*
 * 'ipp_create_xxx_subscriptions()' - Create job and printer subscriptions.
 */

static void
ipp_create_xxx_subscriptions(
    server_client_t *client)
{
  server_subscription_t	*sub;		/* Subscription */
  ipp_attribute_t	*attr;		/* Subscription attribute */
  const char		*username;	/* requesting-user-name or
					   authenticated username */
  int			num_subs = 0,	/* Number of subscriptions */
			ok_subs = 0;	/* Number of good subscriptions */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

 /*
  * For the Create-xxx-Subscriptions operations, queue up a successful-ok
  * response...
  */

  if (ippGetOperation(client->request) == IPP_OP_CREATE_JOB_SUBSCRIPTIONS || ippGetOperation(client->request) == IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS)
    serverRespondIPP(client, IPP_STATUS_OK, NULL);

 /*
  * Get the authenticated user name, if any...
  */

  if (client->username[0])
    username = client->username;
  else if ((attr = ippFindAttribute(client->request, "requesting-user-name", IPP_TAG_NAME)) != NULL && ippGetGroupTag(attr) == IPP_TAG_OPERATION && ippGetCount(attr) == 1)
    username = ippGetString(attr, 0, NULL);
  else
    username = "anonymous";

 /*
  * Skip past the initial attributes to the first subscription group.
  */

  attr = ippFirstAttribute(client->request);
  while (attr && ippGetGroupTag(attr) != IPP_TAG_SUBSCRIPTION)
    attr = ippNextAttribute(client->request);

  while (attr)
  {
    server_job_t	*job = NULL;	/* Job */
    const char		*attrname,	/* Attribute name */
			*pullmethod = NULL;
    					/* notify-pull-method */
    ipp_attribute_t	*notify_attributes = NULL,
					/* notify-attributes */
			*notify_events = NULL,
					/* notify-events */
			*notify_user_data = NULL;
					/* notify-user-data */
    int			interval = 0,	/* notify-time-interval */
			lease = SERVER_NOTIFY_LEASE_DURATION_DEFAULT;
					/* notify-lease-duration */
    ipp_status_t	status = IPP_STATUS_OK;
					/* notify-status-code */

    num_subs ++;

    while (attr)
    {
      if ((attrname = ippGetName(attr)) == NULL)
        break;

      if (!strcmp(attrname, "notify-recipient-uri"))
      {
       /*
        * Push notifications not supported.
	*/

        status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	ippCopyAttribute(client->response, attr, 0);
      }
      else if (!strcmp(attrname, "notify-pull-method"))
      {
	pullmethod = ippGetString(attr, 0, NULL);

        if (ippGetValueTag(attr) != IPP_TAG_KEYWORD || ippGetCount(attr) != 1 || !pullmethod || strcmp(pullmethod, "ippget"))
	{
          ippCopyAttribute(client->response, attr, 0);
	  pullmethod = NULL;
	  status     = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	}
      }
      else if (!strcmp(attrname, "notify-attributes"))
      {
        if (ippGetValueTag(attr) != IPP_TAG_KEYWORD)
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}

	notify_attributes = attr;
      }
      else if (!strcmp(attrname, "notify-charset"))
      {
        if (ippGetValueTag(attr) != IPP_TAG_CHARSET || ippGetCount(attr) != 1 ||
	    (strcmp(ippGetString(attr, 0, NULL), "us-ascii") && strcmp(ippGetString(attr, 0, NULL), "utf-8")))
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
      }
      else if (!strcmp(attrname, "notify-natural-language"))
      {
        if (ippGetValueTag(attr) !=  IPP_TAG_LANGUAGE || ippGetCount(attr) != 1 || strcmp(ippGetString(attr, 0, NULL), "en"))
        {
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
      }
      else if (!strcmp(attrname, "notify-user-data"))
      {
        int	datalen;		/* Length of data */

        if (ippGetValueTag(attr) != IPP_TAG_STRING || ippGetCount(attr) != 1 || !ippGetOctetString(attr, 0, &datalen) || datalen > 63)
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
	else
	  notify_user_data = attr;
      }
      else if (!strcmp(attrname, "notify-events"))
      {
        if (ippGetValueTag(attr) != IPP_TAG_KEYWORD)
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
	else
          notify_events = attr;
      }
      else if (!strcmp(attrname, "notify-lease-duration"))
      {
        if (ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) < 0)
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
	else
          lease = ippGetInteger(attr, 0);
      }
      else if (!strcmp(attrname, "notify-time-interval"))
      {
        if (ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) < 0)
	{
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
	else
          interval = ippGetInteger(attr, 0);
      }
      else if (!strcmp(attrname, "notify-job-id"))
      {
        if (ippGetOperation(client->request) != IPP_OP_CREATE_JOB_SUBSCRIPTIONS || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetInteger(attr, 0) < 1)
        {
	  status = IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES;
	  ippCopyAttribute(client->response, attr, 0);
	}
	else if ((job = serverFindJob(client, ippGetInteger(attr, 0))) == NULL)
	{
	  status = IPP_STATUS_ERROR_NOT_FOUND;
	  ippCopyAttribute(client->response, attr, 0);
	}
      }

      attr = ippNextAttribute(client->request);
    }

    if (status)
    {
      ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM, "notify-status-code", status);
    }
    else if (!pullmethod)
    {
      ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM, "notify-status-code", IPP_STATUS_ERROR_BAD_REQUEST);
    }
    else
    {
      switch (ippGetOperation(client->request))
      {
	case IPP_OP_PRINT_JOB :
	case IPP_OP_PRINT_URI :
	case IPP_OP_CREATE_JOB :
	    job = client->job;
	    break;

	default :
	    break;
      }

      if ((sub = serverCreateSubscription(client->printer, job, interval, lease, username, notify_events, notify_attributes, notify_user_data)) != NULL)
      {
        ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-subscription-id", sub->id);
        ok_subs ++;
      }
      else
        ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_ENUM, "notify-status-code", IPP_STATUS_ERROR_INTERNAL);
    }
  }

  if (ok_subs == 0 && num_subs != 0)
    ippSetStatusCode(client->response, IPP_STATUS_ERROR_IGNORED_ALL_SUBSCRIPTIONS);
  else if (ok_subs != num_subs)
    ippSetStatusCode(client->response, IPP_STATUS_OK_IGNORED_SUBSCRIPTIONS);
}


/*
 * 'ipp_deregister_output_device()' - Unregister an output device.
 */

static void
ipp_deregister_output_device(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

 /*
  * Find the device...
  */

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Output device not found.");
    return;
  }

 /*
  * Remove the device from the printer...
  */

  _cupsRWLockWrite(&client->printer->rwlock);

  cupsArrayRemove(client->printer->devices, device);

  serverUpdateDeviceAttributesNoLock(client->printer);
  serverUpdateDeviceStateNoLock(client->printer);

  _cupsRWUnlock(&client->printer->rwlock);

 /*
  * Delete the device...
  */

  serverDeleteDevice(device);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_fetch_document()' - Download a document.
 */

static void
ipp_fetch_document(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */
  int			compression;	/* compression */
  char			filename[1024];	/* Job filename */
  const char		*format = NULL;	/* document-format */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (!job->dev_uuid || strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job not assigned to device.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "document-number", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(attr) != IPP_TAG_OPERATION || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, attr ? "Bad document-number attribute." : "Missing document-number attribute.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "compression-accepted", IPP_TAG_KEYWORD)) != NULL)
    compression = !strcmp(ippGetString(attr, 0, NULL), "gzip");
  else
    compression = 0;

  if ((attr = ippFindAttribute(client->request, "document-format-accepted", IPP_TAG_MIMETYPE)) == NULL)
    attr = ippFindAttribute(client->printer->dev_attrs, "document-format-supported", IPP_TAG_MIMETYPE);

  if (attr && !ippContainsString(attr, job->format))
  {
    if (ippContainsString(attr, "image/urf"))
      format = "image/urf";
    else if (ippContainsString(attr, "image/pwg-raster"))
      format = "image/pwg-raster";
    else if (ippContainsString(attr, "application/vnd.hp-pcl"))
      format = "application/vnd.hp-pcl";
    else
      format = NULL;

    if (format)
    {
     /*
      * Transform and stream document as raster...
      */

      serverRespondIPP(client, IPP_STATUS_OK, NULL);
      ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
      ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, compression ? "gzip" : "none");

      if (httpGetState(client->http) != HTTP_STATE_POST_SEND)
	httpFlush(client->http);	/* Flush trailing (junk) data */

      serverLogAttributes(client, "Response:", client->response, 2);

      serverLogClient(SERVER_LOGLEVEL_INFO, client, "%s", httpStatus(HTTP_STATUS_OK));

      httpClearFields(client->http);
      httpSetField(client->http, HTTP_FIELD_CONTENT_TYPE, "application/ipp");

      if (compression)
	httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, "gzip");

      httpSetLength(client->http, 0);
      if (httpWriteResponse(client->http, HTTP_STATUS_OK) < 0)
	return;

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "ipp_fetch_document: Sending %d bytes of IPP response.", (int)ippLength(client->response));

      ippSetState(client->response, IPP_STATE_IDLE);

      if (ippWrite(client->http, client->response) != IPP_STATE_DATA)
      {
	serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unable to write IPP response.");
	return;
      }

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "ipp_fetch_document: Sent IPP response.");

      job->state = IPP_JSTATE_PROCESSING;
      serverTransformJob(client, job, "ipptransform", format, SERVER_TRANSFORM_TO_CLIENT);

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "ipp_fetch_document: Sending 0-length chunk.");
      httpWrite2(client->http, "", 0);

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "ipp_fetch_document: Flushing write buffer.");
      httpFlushWrite(client->http);
      return;
    }
    else
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Document not available in requested format.");
      return;
    }
  }

  if (job->format)
  {
    serverCreateJobFilename(client->printer, job, job->format, filename, sizeof(filename));

    if (access(filename, R_OK))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Document not available in requested format.");
      return;
    }

    format = job->format;
  }
  else
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Document format unknown.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
  ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "compression", NULL, compression ? "gzip" : "none");

  client->fetch_file = open(filename, O_RDONLY);
}


/*
 * 'ipp_fetch_job()' - Download a job.
 */

static void
ipp_fetch_job(server_client_t *client)	/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (job->dev_uuid && strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job not assigned to device.");
    return;
  }

  if (!(job->state_reasons & SERVER_JREASON_JOB_FETCHABLE))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Job not fetchable.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  copy_job_attributes(client, job, NULL, NULL);
}


/*
 * 'ipp_get_document_attributes()' - Get the attributes for a document object.
 *
 * Note: This implementation only supports single document jobs so we
 *       synthesize the information for a single document from the job.
 */

static void
ipp_get_document_attributes(
    server_client_t *client)		/* I - Client */
{
  server_job_t	*job;			/* Job */
  ipp_attribute_t *number;		/* document-number attribute */
  cups_array_t	*ra;			/* requested-attributes */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job not found.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

  if ((number = ippFindAttribute(client->request, "document-number", IPP_TAG_INTEGER)) == NULL || ippGetInteger(number, 0) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Document %d not found.", ippGetInteger(number, 0));
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = ippCreateRequestedArray(client->request);
  copy_doc_attributes(client, job, ra, serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, DocumentPrivacyScope) ? NULL : DocumentPrivacyArray);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_documents()' - Get the list of documents in a job.
 *
 * Note: This implementation only supports single document jobs so we
 *       synthesize the information for a single document from the job.
 */

static void
ipp_get_documents(server_client_t *client)/* I - Client */
{
  server_job_t	*job;			/* Job */
  cups_array_t	*ra;			/* requested-attributes */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job not found.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = ippCreateRequestedArray(client->request);
  copy_doc_attributes(client, job, ra, serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, DocumentPrivacyScope) ? NULL : DocumentPrivacyArray);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_job_attributes()' - Get the attributes for a job object.
 */

static void
ipp_get_job_attributes(
    server_client_t *client)		/* I - Client */
{
  server_job_t	*job;			/* Job */
  cups_array_t	*ra;			/* requested-attributes */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job not found.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = ippCreateRequestedArray(client->request);
  copy_job_attributes(client, job, ra, serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope) ? NULL : JobPrivacyArray);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_jobs()' - Get a list of job objects.
 */

static void
ipp_get_jobs(server_client_t *client)	/* I - Client */
{
  ipp_attribute_t	*attr;		/* Current attribute */
  const char		*which_jobs = NULL;
					/* which-jobs values */
  int			job_comparison;	/* Job comparison */
  ipp_jstate_t		job_state;	/* job-state value */
  int			first_job_id,	/* First job ID */
			limit,		/* Maximum number of jobs to return */
			count;		/* Number of jobs that match */
  const char		*username;	/* Username */
  server_job_t		*job;		/* Current job pointer */
  cups_array_t		*ra;		/* Requested attributes array */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

 /*
  * See if the "which-jobs" attribute have been specified...
  */

  if ((attr = ippFindAttribute(client->request, "which-jobs",
                               IPP_TAG_KEYWORD)) != NULL)
  {
    which_jobs = ippGetString(attr, 0, NULL);
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Get-Jobs which-jobs='%s'", which_jobs);
  }

  if (!which_jobs || !strcmp(which_jobs, "not-completed"))
  {
    job_comparison = -1;
    job_state      = IPP_JSTATE_STOPPED;
  }
  else if (!strcmp(which_jobs, "completed"))
  {
    job_comparison = 1;
    job_state      = IPP_JSTATE_CANCELED;
  }
  else if (!strcmp(which_jobs, "aborted"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_ABORTED;
  }
  else if (!strcmp(which_jobs, "all"))
  {
    job_comparison = 1;
    job_state      = IPP_JSTATE_PENDING;
  }
  else if (!strcmp(which_jobs, "canceled"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_CANCELED;
  }
  else if (!strcmp(which_jobs, "pending"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_PENDING;
  }
  else if (!strcmp(which_jobs, "pending-held"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_HELD;
  }
  else if (!strcmp(which_jobs, "processing"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_PROCESSING;
  }
  else if (!strcmp(which_jobs, "processing-stopped"))
  {
    job_comparison = 0;
    job_state      = IPP_JSTATE_STOPPED;
  }
  else
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES,
                "The which-jobs value \"%s\" is not supported.", which_jobs);
    ippAddString(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_KEYWORD,
                 "which-jobs", NULL, which_jobs);
    return;
  }

 /*
  * See if they want to limit the number of jobs reported...
  */

  if ((attr = ippFindAttribute(client->request, "limit",
                               IPP_TAG_INTEGER)) != NULL)
  {
    limit = ippGetInteger(attr, 0);

    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Get-Jobs limit=%d", limit);
  }
  else
    limit = 0;

  if ((attr = ippFindAttribute(client->request, "first-job-id",
                               IPP_TAG_INTEGER)) != NULL)
  {
    first_job_id = ippGetInteger(attr, 0);

    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Get-Jobs first-job-id=%d", first_job_id);
  }
  else
    first_job_id = 1;

 /*
  * See if we only want to see jobs for a specific user...
  */

  username = NULL;

  if ((attr = ippFindAttribute(client->request, "my-jobs",
                               IPP_TAG_BOOLEAN)) != NULL)
  {
    int my_jobs = ippGetBoolean(attr, 0);

    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Get-Jobs my-jobs=%s", my_jobs ? "true" : "false");

    if (my_jobs)
    {
      if ((attr = ippFindAttribute(client->request, "requesting-user-name",
					IPP_TAG_NAME)) == NULL)
      {
	serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Need requesting-user-name with my-jobs.");
	return;
      }

      username = ippGetString(attr, 0, NULL);

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Get-Jobs requesting-user-name='%s'", username);
    }
  }

 /*
  * OK, build a list of jobs for this printer...
  */

  ra = ippCreateRequestedArray(client->request);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  _cupsRWLockRead(&(client->printer->rwlock));

  for (count = 0, job = (server_job_t *)cupsArrayFirst(client->printer->jobs);
       (limit <= 0 || count < limit) && job;
       job = (server_job_t *)cupsArrayNext(client->printer->jobs))
  {
   /*
    * Filter out jobs that don't match...
    */

    if ((job_comparison < 0 && job->state > job_state) ||
	(job_comparison == 0 && job->state != job_state) ||
	(job_comparison > 0 && job->state < job_state) ||
	job->id < first_job_id ||
	(username && job->username &&
	 strcasecmp(username, job->username)))
      continue;

    if (count > 0)
      ippAddSeparator(client->response);

    count ++;
    copy_job_attributes(client, job, ra, serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope) ? NULL : JobPrivacyArray);
  }

  cupsArrayDelete(ra);

  _cupsRWUnlock(&(client->printer->rwlock));
}


/*
 * 'ipp_get_notifications()' - Get notification events for one or more subscriptions.
 */

static void
ipp_get_notifications(
    server_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*sub_ids,	/* notify-subscription-ids */
			*seq_nums;	/* notify-sequence-numbers */
  int			notify_wait;	/* Wait for events? */
  int			i,		/* Looping vars */
			count,		/* Number of IDs */
			seq_num;	/* Sequence number */
  server_subscription_t	*sub;		/* Current subscription */
  ipp_t			*event;		/* Current event */
  int			num_events = 0;	/* Number of events returned */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((sub_ids = ippFindAttribute(client->request, "notify-subscription-ids", IPP_TAG_INTEGER)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing notify-subscription-ids attribute.");
    return;
  }

  count       = ippGetCount(sub_ids);
  seq_nums    = ippFindAttribute(client->request, "notify-sequence-numbers", IPP_TAG_INTEGER);
  notify_wait = ippGetBoolean(ippFindAttribute(client->request, "notify-wait", IPP_TAG_BOOLEAN), 0);

  if (seq_nums && count != ippGetCount(seq_nums))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "The notify-subscription-ids and notify-sequence-numbers attributes are different lengths.");
    return;
  }

  do
  {
    for (i = 0; i < count; i ++)
    {
      if ((sub = serverFindSubscription(client, ippGetInteger(sub_ids, i))) == NULL)
      {
        serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription #%d was not found.", ippGetInteger(sub_ids, i));
        ippAddInteger(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "notify-subscription-ids", ippGetInteger(sub_ids, i));
	break;
      }

      if (!serverAuthorizeUser(client, sub->username, SERVER_GROUP_NONE, SubscriptionPrivacyScope))
      {
        serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "You do not have access to subscription #%d.", ippGetInteger(sub_ids, i));
        ippAddInteger(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "notify-subscription-ids", ippGetInteger(sub_ids, i));
	break;
      }

      _cupsRWLockRead(&sub->rwlock);

      seq_num = ippGetInteger(seq_nums, i);
      if (seq_num < sub->first_sequence)
	seq_num = sub->first_sequence;

      if (seq_num > sub->last_sequence)
      {
        _cupsRWUnlock(&sub->rwlock);
	continue;
      }

      for (event = (ipp_t *)cupsArrayIndex(sub->events, seq_num - sub->first_sequence);
	   event;
	   event = (ipp_t *)cupsArrayNext(sub->events))
      {
	if (num_events == 0)
	{
	  serverRespondIPP(client, IPP_STATUS_OK, NULL);
	  ippAddInteger(client->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-get-interval", 30);
	}
	else
	  ippAddSeparator(client->response);

	ippCopyAttributes(client->response, event, 0, NULL, NULL);
	num_events ++;
      }

      _cupsRWUnlock(&sub->rwlock);
    }

    if (i < count)
      break;
    else if (num_events == 0 && notify_wait)
    {
      if (notify_wait > 0)
      {
       /*
	* Wait for more events...
	*/

        serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Waiting for events.");

	_cupsMutexLock(&NotificationMutex);
	_cupsCondWait(&NotificationCondition, &NotificationMutex, 30.0);
	_cupsMutexUnlock(&NotificationMutex);

        serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Done waiting for events.");

        notify_wait = -1;
      }
      else
      {
       /*
        * Stop waiting for events...
        */

        notify_wait = 0;
      }
    }
  }
  while (num_events == 0 && notify_wait);
}


/*
 * 'ipp_get_output_device_attributes()' - Get attributes for an output device.
 */

static void
ipp_get_output_device_attributes(
    server_client_t *client)		/* I - Client */
{
  cups_array_t		*ra;		/* Requested attributes array */
  server_device_t	*device;	/* Device */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Output device not found.");
    return;
  }

  ra = ippCreateRequestedArray(client->request);

  _cupsRWLockRead(&device->rwlock);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  serverCopyAttributes(client->response, device->attrs, ra, NULL, IPP_TAG_ZERO, IPP_TAG_ZERO);

  _cupsRWUnlock(&device->rwlock);

  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_printer_attributes()' - Get the attributes for a printer object.
 */

static void
ipp_get_printer_attributes(
    server_client_t *client)		/* I - Client */
{
  cups_array_t		*ra;		/* Requested attributes array */
  server_printer_t	*printer;	/* Printer */


 /*
  * Send the attributes...
  */

  ra      = ippCreateRequestedArray(client->request);
  printer = client->printer;

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  _cupsRWLockRead(&(printer->rwlock));

  serverCopyAttributes(client->response, printer->pinfo.attrs, ra, NULL, IPP_TAG_ZERO, IPP_TAG_CUPS_CONST);
  serverCopyAttributes(client->response, printer->dev_attrs, ra, NULL, IPP_TAG_ZERO, IPP_TAG_ZERO);
  serverCopyAttributes(client->response, PrivacyAttributes, ra, NULL, IPP_TAG_ZERO, IPP_TAG_CUPS_CONST);

  if (!ra || cupsArrayFind(ra, "printer-config-change-date-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-config-change-date-time", ippTimeToDate(printer->config_time));

  if (!ra || cupsArrayFind(ra, "printer-config-change-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-config-change-time", (int)(printer->config_time - printer->start_time));

  if (!ra || cupsArrayFind(ra, "printer-current-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-current-time", ippTimeToDate(time(NULL)));

  if (!ra || cupsArrayFind(ra, "printer-state"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_ENUM,
                  "printer-state", printer->state > printer->dev_state ? (int)printer->state : (int)printer->dev_state);

  if (!ra || cupsArrayFind(ra, "printer-state-change-date-time"))
    ippAddDate(client->response, IPP_TAG_PRINTER, "printer-state-change-date-time", ippTimeToDate(printer->state_time));

  if (!ra || cupsArrayFind(ra, "printer-state-change-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-state-change-time", (int)(printer->state_time - printer->start_time));

  if (!ra || cupsArrayFind(ra, "printer-state-message"))
  {
    static const char * const messages[] = { "Idle.", "Printing.", "Stopped." };

    if (printer->state > printer->dev_state)
      ippAddString(client->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-state-message", NULL, messages[printer->state - IPP_PSTATE_IDLE]);
    else
      ippAddString(client->response, IPP_TAG_PRINTER, IPP_CONST_TAG(IPP_TAG_TEXT), "printer-state-message", NULL, messages[printer->dev_state - IPP_PSTATE_IDLE]);
  }

  if (!ra || cupsArrayFind(ra, "printer-state-reasons"))
    serverCopyPrinterStateReasons(client->response, IPP_TAG_PRINTER, printer);

  if (printer->pinfo.strings && (!ra || cupsArrayFind(ra, "printer-strings-uri")))
  {
   /*
    * See if we have a localization that matches the request language.
    */

    ipp_attribute_t	*attr;		/* attributes-natural-language attribute */
    char		lang[32];	/* Copy of language string */
    server_lang_t	key, *match;	/* Localization key and match */

    ippFirstAttribute(client->request);
    attr = ippNextAttribute(client->request);
    strlcpy(lang, ippGetString(attr, 0, NULL), sizeof(lang));
    key.lang = lang;
    if ((match = cupsArrayFind(printer->pinfo.strings, &key)) == NULL && lang[2])
    {
     /*
      * Try base language...
      */

      lang[2] = '\0';
      match = cupsArrayFind(printer->pinfo.strings, &key);
    }

    if (match)
    {
      char		uri[1024];	/* printer-strings-uri value */
      server_listener_t	*lis = cupsArrayFirst(Listeners);
					/* Default listener */
      const char	*scheme = "http";
					/* URL scheme */

#ifdef HAVE_SSL
      if (Encryption != HTTP_ENCRYPTION_NEVER)
        scheme = "https";
#endif /* HAVE_SSL */

      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, NULL, lis->host, lis->port, "%s/%s.strings", printer->resource, match->lang);
      ippAddString(client->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-strings-uri", NULL, uri);
    }
  }

  if (!ra || cupsArrayFind(ra, "printer-up-time"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "printer-up-time", (int)(time(NULL) - printer->start_time));

  if (!ra || cupsArrayFind(ra, "queued-job-count"))
    ippAddInteger(client->response, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "queued-job-count", cupsArrayCount(printer->active_jobs));

  _cupsRWUnlock(&(printer->rwlock));

  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_printer_supported_values()' - Return the supported values for
 *                                        the infrastructure printer.
 */

static void
ipp_get_printer_supported_values(
    server_client_t *client)		/* I - Client */
{
  cups_array_t	*ra = ippCreateRequestedArray(client->request);
					/* Requested attributes */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  serverCopyAttributes(client->response, client->printer->pinfo.attrs, ra, NULL, IPP_TAG_PRINTER, 1);

  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_subscription_attributes()' - Get attributes for a subscription.
 */

static void
ipp_get_subscription_attributes(
    server_client_t *client)		/* I - Client */
{
  server_subscription_t	*sub;		/* Subscription */
  cups_array_t		*ra = ippCreateRequestedArray(client->request);
					/* Requested attributes */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
  }
  else
  {
    serverRespondIPP(client, IPP_STATUS_OK, NULL);
    copy_subscription_attributes(client, sub, ra, serverAuthorizeUser(client, sub->username, SERVER_GROUP_NONE, SubscriptionPrivacyScope) ? NULL : SubscriptionPrivacyArray);
  }

  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_subscriptions()' - Get attributes for all subscriptions.
 */

static void
ipp_get_subscriptions(
    server_client_t *client)		/* I - Client */
{
  server_subscription_t	*sub;		/* Current subscription */
  cups_array_t		*ra = ippCreateRequestedArray(client->request);
					/* Requested attributes */
  int			first = 1;	/* First time? */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  _cupsRWLockRead(&SubscriptionsRWLock);
  for (sub = (server_subscription_t *)cupsArrayFirst(Subscriptions); sub; sub = (server_subscription_t *)cupsArrayNext(Subscriptions))
  {
    if (first)
      first = 0;
    else
      ippAddSeparator(client->response);

    copy_subscription_attributes(client, sub, ra, serverAuthorizeUser(client, sub->username, SERVER_GROUP_NONE, SubscriptionPrivacyScope) ? NULL : SubscriptionPrivacyArray);
  }
  _cupsRWUnlock(&SubscriptionsRWLock);

  cupsArrayDelete(ra);
}


/*
 * 'ipp_get_system_attributes()' - Get the attributes for the system object.
 */

static void
ipp_get_system_attributes(
    server_client_t *client)		/* I - Client */
{
  cups_array_t		*ra;		/* Requested attributes array */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the admin group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, AuthAdminGroup, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

 /*
  * Send the attributes...
  */

  ra = ippCreateRequestedArray(client->request);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  _cupsRWLockRead(&SystemRWLock);

  serverCopyAttributes(client->response, SystemAttributes, ra, NULL, IPP_TAG_ZERO, IPP_TAG_CUPS_CONST);
//  serverCopyAttributes(client->response, PrivacyAttributes, ra, NULL, IPP_TAG_ZERO, IPP_TAG_CUPS_CONST);

  /* system-default-printer-id (integer(1:65535) | no-value) */

  if (!ra || cupsArrayFind(ra, "system-config-change-date-time"))
    ippAddDate(client->response, IPP_TAG_SYSTEM, "system-config-change-date-time", ippTimeToDate(SystemConfigChangeTime));

  if (!ra || cupsArrayFind(ra, "system-config-change-time"))
    ippAddInteger(client->response, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "system-config-change-time", (int)(SystemConfigChangeTime - SystemStartTime));

  if (!ra || cupsArrayFind(ra, "system-config-changes"))
    ippAddInteger(client->response, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "system-config-change-date-time", SystemConfigChanges);

  if (!ra || cupsArrayFind(ra, "system-configured-printers"))
  {
    int			i,		/* Looping var */
  			count;		/* Number of printers */
    ipp_t		*col;		/* Collection value */
    ipp_attribute_t	*printers;	/* system-configured-printers attribute */
    server_printer_t	*printer;	/* Current printer */
    static const char * const types[] =	/* Service types */
    {
      "print",
      "print3d"
    };

    _cupsRWLockRead(&PrintersRWLock);

    if ((count = cupsArrayCount(Printers)) == 0)
    {
      ippAddOutOfBand(client->response, IPP_TAG_SYSTEM, IPP_TAG_NOVALUE, "system-configured-printers");
    }
    else
    {
      printers = ippAddCollections(client->response, IPP_TAG_SYSTEM, "system-configured-printers", count, NULL);

      for (i = 0, printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers), i ++)
      {
        _cupsRWLockRead(&printer->rwlock);

        col = ippNew();
        ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_INTEGER, "printer-id", printer->id);
        ippAddString(col, IPP_TAG_ZERO, IPP_TAG_TEXT, "printer-info", NULL, printer->name);
        ippAddBoolean(col, IPP_TAG_ZERO, "printer-is-accepting-jobs", (char)ippGetBoolean(ippFindAttribute(printer->pinfo.attrs, "printer-is-accepting-jobs", IPP_TAG_BOOLEAN), 0));
        ippAddString(col, IPP_TAG_ZERO, IPP_TAG_NAME, "printer-name", NULL, printer->name);
        ippAddString(col, IPP_TAG_ZERO, IPP_CONST_TAG(IPP_TAG_KEYWORD), "printer-service-type", NULL, types[printer->type]);
        ippAddInteger(col, IPP_TAG_ZERO, IPP_TAG_ENUM, "printer-state", printer->state);
        serverCopyPrinterStateReasons(col, IPP_TAG_ZERO, printer);
        ippCopyAttribute(col, ippFindAttribute(printer->pinfo.attrs, "printer-xri-supported", IPP_TAG_BEGIN_COLLECTION), 1);

        ippSetCollection(client->response, &printers, i, col);
        ippDelete(col);

        _cupsRWUnlock(&printer->rwlock);
      }
    }

    _cupsRWUnlock(&PrintersRWLock);
  }

  /* TODO: Update when resources are implemented */
  if (!ra || cupsArrayFind(ra, "system-configured-resources"))
    ippAddOutOfBand(client->response, IPP_TAG_SYSTEM, IPP_TAG_NOVALUE, "system-configured-resources");

  if (!ra || cupsArrayFind(ra, "system-current-time"))
    ippAddDate(client->response, IPP_TAG_SYSTEM, "system-current-time", ippTimeToDate(time(NULL)));

  if (!ra || cupsArrayFind(ra, "system-state"))
  {
    ipp_pstate_t	state = IPP_PSTATE_STOPPED;
					/* System state */
    server_printer_t	*printer;	/* Current printer */

    _cupsRWLockRead(&PrintersRWLock);

    for (printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
    {
      if (printer->state == IPP_PSTATE_PROCESSING)
        state = IPP_PSTATE_PROCESSING;
      else if (printer->state == IPP_PSTATE_IDLE && state == IPP_PSTATE_STOPPED)
        state = IPP_PSTATE_IDLE;
    }

    _cupsRWUnlock(&PrintersRWLock);

    ippAddInteger(client->response, IPP_TAG_SYSTEM, IPP_TAG_ENUM, "system-state", state);
  }

  if (!ra || cupsArrayFind(ra, "system-state-change-date-time"))
    ippAddDate(client->response, IPP_TAG_SYSTEM, "system-state-change-date-time", ippTimeToDate(SystemStateChangeTime));

  if (!ra || cupsArrayFind(ra, "system-state-change-time"))
    ippAddInteger(client->response, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "system-state-change-time", (int)(SystemStateChangeTime - SystemStartTime));

  if (!ra || cupsArrayFind(ra, "system-state-message"))
    ippAddString(client->response, IPP_TAG_SYSTEM, IPP_CONST_TAG(IPP_TAG_TEXT), "system-state-message", NULL, "");

  if (!ra || cupsArrayFind(ra, "system-state-reasons"))
    ippAddString(client->response, IPP_TAG_SYSTEM, IPP_TAG_KEYWORD, "system-state-reasons", NULL, "none");

  if (!ra || cupsArrayFind(ra, "system-up-time"))
    ippAddInteger(client->response, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "system-up-time", (int)(time(NULL) - SystemStartTime));

#if 0 /* TODO: Add strings support for system object */
  if (printer->pinfo.strings && (!ra || cupsArrayFind(ra, "printer-strings-uri")))
  {
   /*
    * See if we have a localization that matches the request language.
    */

    ipp_attribute_t	*attr;		/* attributes-natural-language attribute */
    char		lang[32];	/* Copy of language string */
    server_lang_t	key, *match;	/* Localization key and match */

    ippFirstAttribute(client->request);
    attr = ippNextAttribute(client->request);
    strlcpy(lang, ippGetString(attr, 0, NULL), sizeof(lang));
    key.lang = lang;
    if ((match = cupsArrayFind(printer->pinfo.strings, &key)) == NULL && lang[2])
    {
     /*
      * Try base language...
      */

      lang[2] = '\0';
      match = cupsArrayFind(printer->pinfo.strings, &key);
    }

    if (match)
    {
      char		uri[1024];	/* printer-strings-uri value */
      server_listener_t	*lis = cupsArrayFirst(Listeners);
					/* Default listener */
      const char	*scheme = "http";
					/* URL scheme */

#ifdef HAVE_SSL
      if (Encryption != HTTP_ENCRYPTION_NEVER)
        scheme = "https";
#endif /* HAVE_SSL */

      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, NULL, lis->host, lis->port, "%s/%s.strings", printer->resource, match->lang);
      ippAddString(client->response, IPP_TAG_PRINTER, IPP_TAG_URI, "printer-strings-uri", NULL, uri);
    }
  }
#endif /* 0 */

  cupsArrayDelete(ra);

  _cupsRWUnlock(&SystemRWLock);
}


/*
 * 'ipp_get_system_supported_values()' - Get the supported values for the system object.
 */

static void
ipp_get_system_supported_values(
    server_client_t *client)		/* I - Client */
{
  cups_array_t		*ra;		/* Requested attributes array */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the admin group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, AuthAdminGroup, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

 /*
  * Send the attributes...
  */

  ra = ippCreateRequestedArray(client->request);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  /* system-default-printer-id (1setOf integer(1:65535)) */
  if (!ra || cupsArrayFind(ra, "system-default-printer-id"))
  {
    int			*values,	/* printer-id values */
  			num_values,	/* Number of printer-id values */
  			count;		/* Number of printers */
    server_printer_t	*printer;	/* Current printer */

    _cupsRWLockRead(&PrintersRWLock);

    if ((count = cupsArrayCount(Printers)) == 0)
    {
      ippAddOutOfBand(client->response, IPP_TAG_SYSTEM, IPP_TAG_NOVALUE, "system-default-printer-id");
    }
    else if ((values = (int *)calloc((size_t)count, sizeof(int))) != NULL)
    {
      for (num_values = 0, printer = (server_printer_t *)cupsArrayFirst(Printers); printer; printer = (server_printer_t *)cupsArrayNext(Printers))
      {
        if (printer->id && printer->id <= 65535)
          values[num_values ++] = printer->id;
      }

      if (num_values > 0)
        ippAddIntegers(client->response, IPP_TAG_SYSTEM, IPP_TAG_INTEGER, "system-default-printer-id", num_values, values);
      else
	ippAddOutOfBand(client->response, IPP_TAG_SYSTEM, IPP_TAG_NOVALUE, "system-default-printer-id");

      free(values);
    }

    _cupsRWUnlock(&PrintersRWLock);
  }

  cupsArrayDelete(ra);
}


/*
 * 'ipp_identify_printer()' - Beep or display a message.
 */

static void
ipp_identify_printer(
    server_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*actions,	/* identify-actions */
			*message;	/* message */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  actions = ippFindAttribute(client->request, "identify-actions", IPP_TAG_KEYWORD);
  message = ippFindAttribute(client->request, "message", IPP_TAG_TEXT);

  if (client->printer->pinfo.proxy_group == SERVER_GROUP_NONE)
  {
   /*
    * Send a notification to the console...
    */

    if (ippContainsString(actions, "display"))
      printf("IDENTIFY-PRINTER: display (%s)\n", message ? ippGetString(message, 0, NULL) : "No message supplied");

    if (!actions || ippContainsString(actions, "sound"))
      puts("IDENTIFY-PRINTER: sound\007");
  }
  else
  {
   /*
    * Save this notification in the printer for the proxy to acknowledge...
    */

    _cupsRWLockWrite(&client->printer->rwlock);

    client->printer->identify_actions = SERVER_IDENTIFY_NONE;
    if (ippContainsString(actions, "display"))
      client->printer->identify_actions |= SERVER_IDENTIFY_DISPLAY;
    if (!actions || ippContainsString(actions, "sound"))
      client->printer->identify_actions |= SERVER_IDENTIFY_SOUND;

    if (client->printer->identify_message)
    {
      free(client->printer->identify_message);
      client->printer->identify_message = NULL;
    }

    if (message)
      client->printer->identify_message = strdup(ippGetString(message, 0, NULL));

    client->printer->state_reasons |= SERVER_PREASON_IDENTIFY_PRINTER_REQUESTED;

    _cupsRWUnlock(&client->printer->rwlock);

    serverAddEvent(client->printer, NULL, NULL, SERVER_EVENT_PRINTER_STATE_CHANGED, "Identify-Printer request received.");
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}




/*
 * 'ipp_print_job()' - Create a job object with an attached document.
 */

static void
ipp_print_job(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* New job */
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  cups_array_t		*ra;		/* Attributes to send in response */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
    return;

 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_SEND)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "No file in request.");
    return;
  }

 /*
  * Print the job...
  */

  if ((job = serverCreateJob(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_TOO_MANY_JOBS, "Too many jobs are queued.");
    return;
  }

 /*
  * Create a file for the request data...
  */

  serverCreateJobFilename(client->printer, job, NULL, filename, sizeof(filename));

  serverLogJob(SERVER_LOGLEVEL_INFO, job, "Creating job file \"%s\", format \"%s\".", filename, job->format);

  if ((job->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
  {
    job->state = IPP_JSTATE_ABORTED;

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to create print file: %s", strerror(errno));
    return;
  }

  while ((bytes = httpRead2(client->http, buffer, sizeof(buffer))) > 0)
  {
    if (write(job->fd, buffer, (size_t)bytes) < bytes)
    {
      int error = errno;		/* Write error */

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);

      serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                  "Unable to write print file: %s", strerror(error));
      return;
    }
  }

  if (bytes < 0)
  {
   /*
    * Got an error while reading the print data, so abort this job.
    */

    job->state = IPP_JSTATE_ABORTED;

    close(job->fd);
    job->fd = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to read print file.");
    return;
  }

  if (close(job->fd))
  {
    int error = errno;		/* Write error */

    job->state = IPP_JSTATE_ABORTED;
    job->fd    = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to write print file: %s", strerror(error));
    return;
  }

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

 /*
  * Process the job, if possible...
  */

  serverCheckJobs(client->printer);

 /*
  * Return the job info...
  */

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-message");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra, NULL);
  cupsArrayDelete(ra);

 /*
  * Process any pending subscriptions...
  */

  client->job = job;
  ipp_create_xxx_subscriptions(client);
}


/*
 * 'ipp_print_uri()' - Create a job object with a referenced document.
 */

static void
ipp_print_uri(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* New job */
  ipp_attribute_t	*uri;		/* document-uri */
  char			scheme[256],	/* URI scheme */
			userpass[256],	/* Username and password info */
			hostname[256],	/* Hostname */
			resource[1024];	/* Resource path */
  int			port;		/* Port number */
  http_uri_status_t	uri_status;	/* URI decode status */
  http_encryption_t	encryption;	/* Encryption to use, if any */
  http_t		*http;		/* Connection for http/https URIs */
  http_status_t		status;		/* Access status for http/https URIs */
  int			infile;		/* Input file for local file URIs */
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  cups_array_t		*ra;		/* Attributes to send in response */
  static const char * const uri_status_strings[] =
  {					/* URI decode errors */
    "URI too large.",
    "Bad arguments to function.",
    "Bad resource in URI.",
    "Bad port number in URI.",
    "Bad hostname in URI.",
    "Bad username in URI.",
    "Bad scheme in URI.",
    "Bad/empty URI."
  };


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
    return;

 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Unexpected document data following request.");
    return;
  }

 /*
  * Do we have a document URI?
  */

  if ((uri = ippFindAttribute(client->request, "document-uri", IPP_TAG_URI)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing document-uri.");
    return;
  }

  if (ippGetCount(uri) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Too many document-uri values.");
    return;
  }

  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL), scheme, sizeof(scheme), userpass, sizeof(userpass), hostname, sizeof(hostname), &port, resource, sizeof(resource));
  if (uri_status < HTTP_URI_STATUS_OK)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad document-uri: %s", uri_status_strings[uri_status - HTTP_URI_STATUS_OVERFLOW]);
    return;
  }

  if (strcmp(scheme, "file") &&
#ifdef HAVE_SSL
      strcmp(scheme, "https") &&
#endif /* HAVE_SSL */
      strcmp(scheme, "http"))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_URI_SCHEME, "URI scheme \"%s\" not supported.", scheme);
    return;
  }

  if (!strcmp(scheme, "file") && access(resource, R_OK))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to access URI: %s", strerror(errno));
    return;
  }

 /*
  * Print the job...
  */

  if ((job = serverCreateJob(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_TOO_MANY_JOBS, "Too many jobs are queued.");
    return;
  }

 /*
  * Create a file for the request data...
  */

  if (!strcasecmp(job->format, "image/jpeg"))
    snprintf(filename, sizeof(filename), "%s/%d.jpg", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "image/png"))
    snprintf(filename, sizeof(filename), "%s/%d.png", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "application/pdf"))
    snprintf(filename, sizeof(filename), "%s/%d.pdf", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "application/postscript"))
    snprintf(filename, sizeof(filename), "%s/%d.ps", SpoolDirectory, job->id);
  else
    snprintf(filename, sizeof(filename), "%s/%d.prn", SpoolDirectory, job->id);

  if ((job->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0)
  {
    job->state = IPP_JSTATE_ABORTED;

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL, "Unable to create print file: %s", strerror(errno));
    return;
  }

  if (!strcmp(scheme, "file"))
  {
    if ((infile = open(resource, O_RDONLY)) < 0)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to access URI: %s", strerror(errno));
      return;
    }

    do
    {
      if ((bytes = read(infile, buffer, sizeof(buffer))) < 0 && (errno == EAGAIN || errno == EINTR))
        bytes = 1;
      else if (bytes > 0 && write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	job->state = IPP_JSTATE_ABORTED;

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	close(infile);

	serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));
	return;
      }
    }
    while (bytes > 0);

    close(infile);
  }
  else
  {
#ifdef HAVE_SSL
    if (port == 443 || !strcmp(scheme, "https"))
      encryption = HTTP_ENCRYPTION_ALWAYS;
    else
#endif /* HAVE_SSL */
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

    if ((http = httpConnect2(hostname, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL)) == NULL)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to connect to %s: %s", hostname, cupsLastErrorString());
      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      return;
    }

    httpClearFields(http);
    httpSetField(http, HTTP_FIELD_ACCEPT_LANGUAGE, "en");
    if (httpGet(http, resource))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to GET URI: %s", strerror(errno));

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);
      return;
    }

    while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE);

    if (status != HTTP_STATUS_OK)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to GET URI: %s", httpStatus(status));

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);
      return;
    }

    while ((bytes = httpRead2(http, buffer, sizeof(buffer))) > 0)
    {
      if (write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	job->state = IPP_JSTATE_ABORTED;

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	httpClose(http);

	serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));
	return;
      }
    }

    httpClose(http);
  }

  if (close(job->fd))
  {
    int error = errno;		/* Write error */

    job->state = IPP_JSTATE_ABORTED;
    job->fd    = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL, "Unable to write print file: %s", strerror(error));
    return;
  }

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

 /*
  * Process the job...
  */

  serverCheckJobs(client->printer);

 /*
  * Return the job info...
  */

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra, NULL);
  cupsArrayDelete(ra);

 /*
  * Process any pending subscriptions...
  */

  client->job = job;
  ipp_create_xxx_subscriptions(client);
}


/*
 * 'ipp_renew_subscription()' - Renew a subscription.
 */

static void
ipp_renew_subscription(
    server_client_t *client)		/* I - Client */
{
  server_subscription_t	*sub;		/* Subscription */
  ipp_attribute_t	*attr;		/* notify-lease-duration */
  int			lease;		/* Lease duration in seconds */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, sub->username, SERVER_GROUP_NONE, SubscriptionPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this subscription.");
    return;
  }

  if (sub->job)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Per-job subscriptions cannot be renewed.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "notify-lease-duration", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetGroupTag(attr) != IPP_TAG_SUBSCRIPTION || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) < 0)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Bad notify-lease-duration.");
      return;
    }

    lease = ippGetInteger(attr, 0);
  }
  else
    lease = SERVER_NOTIFY_LEASE_DURATION_DEFAULT;

  sub->lease = lease;

  if (lease)
    sub->expire = time(NULL) + sub->lease;
  else
    sub->expire = INT_MAX;

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_send_document()' - Add an attached document to a job object created with
 *                         Create-Job.
 */

static void
ipp_send_document(server_client_t *client)/* I - Client */
{
  server_job_t		*job;		/* Job information */
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  ipp_attribute_t	*attr;		/* Current attribute */
  cups_array_t		*ra;		/* Attributes to send in response */


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

 /*
  * See if we already have a document for this job or the job has already
  * in a non-pending state...
  */

  if (job->state > IPP_JSTATE_HELD)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
                "Job is not in a pending state.");
    httpFlush(client->http);
    return;
  }
  else if (job->filename || job->fd >= 0)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_MULTIPLE_JOBS_NOT_SUPPORTED,
                "Multiple document jobs are not supported.");
    httpFlush(client->http);
    return;
  }

  if ((attr = ippFindAttribute(client->request, "last-document",
                               IPP_TAG_ZERO)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Missing required last-document attribute.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetValueTag(attr) != IPP_TAG_BOOLEAN || ippGetCount(attr) != 1 ||
           !ippGetBoolean(attr, 0))
  {
    serverRespondUnsupported(client, attr);
    httpFlush(client->http);
    return;
  }

 /*
  * Validate document attributes...
  */

  if (!valid_doc_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

  serverCopyAttributes(job->attrs, client->request, NULL, NULL, IPP_TAG_JOB, 0);

 /*
  * Get the document format for the job...
  */

  _cupsRWLockWrite(&(client->printer->rwlock));

  if ((attr = ippFindAttribute(job->attrs, "document-format-detected", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else if ((attr = ippFindAttribute(job->attrs, "document-format-supplied", IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else
    job->format = "application/octet-stream";

 /*
  * Create a file for the request data...
  */

  serverCreateJobFilename(client->printer, job, NULL, filename, sizeof(filename));

  serverLogJob(SERVER_LOGLEVEL_INFO, job, "Creating job file \"%s\", format \"%s\".", filename, job->format);

  job->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  _cupsRWUnlock(&(client->printer->rwlock));

  if (job->fd < 0)
  {
    job->state = IPP_JSTATE_ABORTED;

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to create print file: %s", strerror(errno));
    return;
  }

  while ((bytes = httpRead2(client->http, buffer, sizeof(buffer))) > 0)
  {
    if (write(job->fd, buffer, (size_t)bytes) < bytes)
    {
      int error = errno;		/* Write error */

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);

      serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                  "Unable to write print file: %s", strerror(error));
      return;
    }
  }

  if (bytes < 0)
  {
   /*
    * Got an error while reading the print data, so abort this job.
    */

    job->state = IPP_JSTATE_ABORTED;

    close(job->fd);
    job->fd = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to read print file.");
    return;
  }

  if (close(job->fd))
  {
    int error = errno;			/* Write error */

    job->state = IPP_JSTATE_ABORTED;
    job->fd    = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to write print file: %s", strerror(error));
    return;
  }

  _cupsRWLockWrite(&(client->printer->rwlock));

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

  _cupsRWUnlock(&(client->printer->rwlock));

 /*
  * Process the job, if possible...
  */

  serverCheckJobs(client->printer);

 /*
  * Return the job info...
  */

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra, NULL);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_send_uri()' - Add a referenced document to a job object created with
 *                    Create-Job.
 */

static void
ipp_send_uri(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* Job information */
  ipp_attribute_t	*uri;		/* document-uri */
  char			scheme[256],	/* URI scheme */
			userpass[256],	/* Username and password info */
			hostname[256],	/* Hostname */
			resource[1024];	/* Resource path */
  int			port;		/* Port number */
  http_uri_status_t	uri_status;	/* URI decode status */
  http_encryption_t	encryption;	/* Encryption to use, if any */
  http_t		*http;		/* Connection for http/https URIs */
  http_status_t		status;		/* Access status for http/https URIs */
  int			infile;		/* Input file for local file URIs */
  char			filename[1024],	/* Filename buffer */
			buffer[4096];	/* Copy buffer */
  ssize_t		bytes;		/* Bytes read */
  ipp_attribute_t	*attr;		/* Current attribute */
  cups_array_t		*ra;		/* Attributes to send in response */
  static const char * const uri_status_strings[] =
  {					/* URI decode errors */
    "URI too large.",
    "Bad arguments to function.",
    "Bad resource in URI.",
    "Bad port number in URI.",
    "Bad hostname in URI.",
    "Bad username in URI.",
    "Bad scheme in URI.",
    "Bad/empty URI."
  };


  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
    return;
  }

  if (Authentication && !serverAuthorizeUser(client, job->username, SERVER_GROUP_NONE, JobPrivacyScope))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this job.");
    return;
  }

 /*
  * See if we already have a document for this job or the job has already
  * in a non-pending state...
  */

  if (job->state > IPP_JSTATE_HELD)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE,
                "Job is not in a pending state.");
    httpFlush(client->http);
    return;
  }
  else if (job->filename || job->fd >= 0)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_MULTIPLE_JOBS_NOT_SUPPORTED,
                "Multiple document jobs are not supported.");
    httpFlush(client->http);
    return;
  }

  if ((attr = ippFindAttribute(client->request, "last-document",
                               IPP_TAG_ZERO)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Missing required last-document attribute.");
    httpFlush(client->http);
    return;
  }
  else if (ippGetValueTag(attr) != IPP_TAG_BOOLEAN || ippGetCount(attr) != 1 ||
           !ippGetBoolean(attr, 0))
  {
    serverRespondUnsupported(client, attr);
    httpFlush(client->http);
    return;
  }

 /*
  * Validate document attributes...
  */

  if (!valid_doc_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

 /*
  * Do we have a file to print?
  */

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Unexpected document data following request.");
    return;
  }

 /*
  * Do we have a document URI?
  */

  if ((uri = ippFindAttribute(client->request, "document-uri",
                              IPP_TAG_URI)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing document-uri.");
    return;
  }

  if (ippGetCount(uri) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "Too many document-uri values.");
    return;
  }

  uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL),
                               scheme, sizeof(scheme), userpass,
                               sizeof(userpass), hostname, sizeof(hostname),
                               &port, resource, sizeof(resource));
  if (uri_status < HTTP_URI_STATUS_OK)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad document-uri: %s",
                uri_status_strings[uri_status - HTTP_URI_STATUS_OVERFLOW]);
    return;
  }

  if (strcmp(scheme, "file") &&
#ifdef HAVE_SSL
      strcmp(scheme, "https") &&
#endif /* HAVE_SSL */
      strcmp(scheme, "http"))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_URI_SCHEME, "URI scheme \"%s\" not supported.", scheme);
    return;
  }

  if (!strcmp(scheme, "file") && access(resource, R_OK))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS, "Unable to access URI: %s", strerror(errno));
    return;
  }

 /*
  * Get the document format for the job...
  */

  _cupsRWLockWrite(&(client->printer->rwlock));

  if ((attr = ippFindAttribute(job->attrs, "document-format",
                               IPP_TAG_MIMETYPE)) != NULL)
    job->format = ippGetString(attr, 0, NULL);
  else
    job->format = "application/octet-stream";

 /*
  * Create a file for the request data...
  */

  if (!strcasecmp(job->format, "image/jpeg"))
    snprintf(filename, sizeof(filename), "%s/%d.jpg", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "image/png"))
    snprintf(filename, sizeof(filename), "%s/%d.png", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "application/pdf"))
    snprintf(filename, sizeof(filename), "%s/%d.pdf", SpoolDirectory, job->id);
  else if (!strcasecmp(job->format, "application/postscript"))
    snprintf(filename, sizeof(filename), "%s/%d.ps", SpoolDirectory, job->id);
  else
    snprintf(filename, sizeof(filename), "%s/%d.prn", SpoolDirectory, job->id);

  job->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  _cupsRWUnlock(&(client->printer->rwlock));

  if (job->fd < 0)
  {
    job->state = IPP_JSTATE_ABORTED;

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to create print file: %s", strerror(errno));
    return;
  }

  if (!strcmp(scheme, "file"))
  {
    if ((infile = open(resource, O_RDONLY)) < 0)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                  "Unable to access URI: %s", strerror(errno));
      return;
    }

    do
    {
      if ((bytes = read(infile, buffer, sizeof(buffer))) < 0 &&
          (errno == EAGAIN || errno == EINTR))
        bytes = 1;
      else if (bytes > 0 && write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	job->state = IPP_JSTATE_ABORTED;

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	close(infile);

	serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
		    "Unable to write print file: %s", strerror(error));
	return;
      }
    }
    while (bytes > 0);

    close(infile);
  }
  else
  {
#ifdef HAVE_SSL
    if (port == 443 || !strcmp(scheme, "https"))
      encryption = HTTP_ENCRYPTION_ALWAYS;
    else
#endif /* HAVE_SSL */
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

    if ((http = httpConnect2(hostname, port, NULL, AF_UNSPEC, encryption,
                             1, 30000, NULL)) == NULL)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                  "Unable to connect to %s: %s", hostname,
		  cupsLastErrorString());
      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      return;
    }

    httpClearFields(http);
    httpSetField(http, HTTP_FIELD_ACCEPT_LANGUAGE, "en");
    if (httpGet(http, resource))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                  "Unable to GET URI: %s", strerror(errno));

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);
      return;
    }

    while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE);

    if (status != HTTP_STATUS_OK)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                  "Unable to GET URI: %s", httpStatus(status));

      job->state = IPP_JSTATE_ABORTED;

      close(job->fd);
      job->fd = -1;

      unlink(filename);
      httpClose(http);
      return;
    }

    while ((bytes = httpRead2(http, buffer, sizeof(buffer))) > 0)
    {
      if (write(job->fd, buffer, (size_t)bytes) < bytes)
      {
	int error = errno;		/* Write error */

	job->state = IPP_JSTATE_ABORTED;

	close(job->fd);
	job->fd = -1;

	unlink(filename);
	httpClose(http);

	serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
		    "Unable to write print file: %s", strerror(error));
	return;
      }
    }

    httpClose(http);
  }

  if (close(job->fd))
  {
    int error = errno;		/* Write error */

    job->state = IPP_JSTATE_ABORTED;
    job->fd    = -1;

    unlink(filename);

    serverRespondIPP(client, IPP_STATUS_ERROR_INTERNAL,
                "Unable to write print file: %s", strerror(error));
    return;
  }

  _cupsRWLockWrite(&(client->printer->rwlock));

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

  _cupsRWUnlock(&(client->printer->rwlock));

 /*
  * Process the job, if possible...
  */

  serverCheckJobs(client->printer);

 /*
  * Return the job info...
  */

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = cupsArrayNew((cups_array_func_t)strcmp, NULL);
  cupsArrayAdd(ra, "job-id");
  cupsArrayAdd(ra, "job-state");
  cupsArrayAdd(ra, "job-state-reasons");
  cupsArrayAdd(ra, "job-uri");

  copy_job_attributes(client, job, ra, NULL);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_set_system_attributes()' - Set attributes for the system object.
 */

static void
ipp_set_system_attributes(
    server_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*settable,	/* system-settable-attributes-supported */
			*attr,		/* Current attribute */
			*unsupported;	/* Unsupported attribute */
  int			i;		/* Looping var */
  server_value_t	*value;		/* Current value tag */
  static server_value_t	values[] =	/* Value tags for settable attributes */
  {
    { "system-default-printer-id",	IPP_TAG_INTEGER },
    { "system-geo-location",		IPP_TAG_URI },
    { "system-info",			IPP_TAG_TEXT },
    { "system-location",		IPP_TAG_TEXT },
    { "system-make-and-model",		IPP_TAG_TEXT },
    { "system-name",			IPP_TAG_NAME },
    { "system-owner-col",		IPP_TAG_BEGIN_COLLECTION }
  };


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the admin group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, AuthAdminGroup, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  _cupsRWLockWrite(&SystemRWLock);

  settable = ippFindAttribute(SystemAttributes, "system-settable-attributes-supported", IPP_TAG_KEYWORD);

  for (attr = ippFirstAttribute(SystemAttributes); attr; attr = ippNextAttribute(SystemAttributes))
  {
    const char *name = ippGetName(attr);

    if (ippGetGroupTag(attr) != IPP_TAG_OPERATION && ippGetGroupTag(attr) != IPP_TAG_SYSTEM)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad group in request.");
      goto unlock_system;
    }

    if (!name)
      continue;

    if (!ippContainsString(settable, name))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_NOT_SETTABLE, "\"%s\" is not settable.", name);
      ippAddOutOfBand(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_NOTSETTABLE, name);
      goto unlock_system;
    }

    for (i = (int)(sizeof(values) / sizeof(values[0])), value = values; i > 0; i --, value ++)
      if (!strcmp(name, value->name))
        break;

    if (i == 0)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_NOT_SETTABLE, "\"%s\" is not settable.", name);
      ippAddOutOfBand(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_NOTSETTABLE, name);
      goto unlock_system;
    }

    if (ippGetValueTag(attr) != value->value_tag ||
        (value->value_tag == IPP_TAG_NAME && ippGetValueTag(attr) == IPP_TAG_NAMELANG) ||
        (value->value_tag == IPP_TAG_TEXT && ippGetValueTag(attr) == IPP_TAG_TEXTLANG))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Wrong type for \"%s\".", name);
      if ((unsupported = ippCopyAttribute(client->response, attr, 0)) != NULL)
        ippSetGroupTag(client->response, &unsupported, IPP_TAG_UNSUPPORTED_GROUP);
      goto unlock_system;
    }

    if (ippGetCount(attr) != 1)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Wrong number of values for \"%s\".", name);
      if ((unsupported = ippCopyAttribute(client->response, attr, 0)) != NULL)
        ippSetGroupTag(client->response, &unsupported, IPP_TAG_UNSUPPORTED_GROUP);
      goto unlock_system;
    }

    if (!strcmp(name, "system-owner-col"))
    {
      ipp_t		*col = ippGetCollection(attr, 0);
					/* Collection value */
      ipp_attribute_t	*member;	/* Member attribute */

      for (member = ippFirstAttribute(col); member; member = ippNextAttribute(col))
      {
        const char *mname = ippGetName(member);

        if (strcmp(mname, "owner-uri") && strcmp(mname, "owner-user-name") && strcmp(mname, "owner-vcard"))
        {
	  serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Unknown member attribute \"%s\" in \"%s\".", mname, name);
	  if ((unsupported = ippCopyAttribute(client->response, attr, 0)) != NULL)
	    ippSetGroupTag(client->response, &unsupported, IPP_TAG_UNSUPPORTED_GROUP);
	  goto unlock_system;
        }
        else if ((!strcmp(mname, "owner-uri") && (ippGetValueTag(member) != IPP_TAG_URI || ippGetCount(member) != 1)) ||
                 (!strcmp(mname, "owner-user-name") && ((ippGetValueTag(member) != IPP_TAG_NAME && ippGetValueTag(member) != IPP_TAG_NAMELANG) || ippGetCount(member) != 1)) ||
                 (!strcmp(mname, "owner-vcard") && ippGetValueTag(member) != IPP_TAG_TEXT && ippGetValueTag(member) != IPP_TAG_TEXTLANG))
        {
	  serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES, "Bad value for member attribute \"%s\" in \"%s\".", mname, name);
	  if ((unsupported = ippCopyAttribute(client->response, attr, 0)) != NULL)
	    ippSetGroupTag(client->response, &unsupported, IPP_TAG_UNSUPPORTED_GROUP);
	  goto unlock_system;
        }
      }
    }
  }

  for (attr = ippFirstAttribute(SystemAttributes); attr; attr = ippNextAttribute(SystemAttributes))
  {
    const char		*name = ippGetName(attr);
					/* Attribute name */
    ipp_attribute_t	*sattr;		/* Attribute to set */

    if (!name || ippGetGroupTag(attr) != IPP_TAG_SYSTEM)
      continue;

    if ((sattr = ippFindAttribute(SystemAttributes, name, IPP_TAG_ZERO)) != NULL)
    {
      switch (ippGetValueTag(attr))
      {
        case IPP_TAG_INTEGER :
            ippSetInteger(SystemAttributes, &sattr, 0, ippGetInteger(attr, 0));
            break;

        case IPP_TAG_NAME :
        case IPP_TAG_NAMELANG :
        case IPP_TAG_TEXT :
        case IPP_TAG_TEXTLANG :
        case IPP_TAG_URI :
           /*
            * Need to copy since ippSetString doesn't handle setting the
            * language override.
            */

	    ippDeleteAttribute(SystemAttributes, sattr);
	    ippCopyAttribute(SystemAttributes, attr, 0);
            break;

        case IPP_TAG_BEGIN_COLLECTION :
            ippSetCollection(SystemAttributes, &sattr, 0, ippGetCollection(attr, 0));
            break;

        default :
            break;
      }
    }
  }

 /*
  * Update config change time and count...
  */

  SystemConfigChangeTime = time(NULL);
  SystemConfigChanges ++;

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  unlock_system:

  _cupsRWUnlock(&SystemRWLock);
}


/*
 * 'ipp_update_active_jobs()' - Update the list of active jobs.
 */

static void
ipp_update_active_jobs(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Output device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*job_ids,	/* job-ids */
			*job_states;	/* output-device-job-states */
  int			i,		/* Looping var */
			count,		/* Number of values */
			num_different = 0,
					/* Number of jobs with different states */
			different[1000],/* Jobs with different states */
			num_unsupported = 0,
					/* Number of unsupported job-ids */
			unsupported[1000];
					/* Unsupported job-ids */
  ipp_jstate_t		states[1000];	/* Different job state values */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

 /*
  * Process the job-ids and output-device-job-states values...
  */

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job_ids = ippFindAttribute(client->request, "job-ids", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(job_ids) != IPP_TAG_OPERATION || ippGetValueTag(job_ids) != IPP_TAG_INTEGER)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, job_ids ? "Bad job-ids attribute." : "Missing required job-ids attribute.");
    return;
  }

  if ((job_states = ippFindAttribute(client->request, "output-device-job-states", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(job_states) != IPP_TAG_OPERATION || ippGetValueTag(job_states) != IPP_TAG_ENUM)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, job_ids ? "Bad output-device-job-states attribute." : "Missing required output-device-job-states attribute.");
    return;
  }

  count = ippGetCount(job_ids);
  if (count != ippGetCount(job_states))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "The job-ids and output-device-job-states attributes do not have the same number of values.");
    return;
  }

  for (i = 0; i < count; i ++)
  {
    if ((job = serverFindJob(client, ippGetInteger(job_ids, i))) == NULL || !job->dev_uuid || strcmp(job->dev_uuid, device->uuid))
    {
      if (num_unsupported < 1000)
        unsupported[num_unsupported ++] = ippGetInteger(job_ids, i);
    }
    else
    {
      ipp_jstate_t	state = (ipp_jstate_t)ippGetInteger(job_states, i);

      if (job->state >= IPP_JSTATE_STOPPED && state != job->state)
      {
        if (num_different < 1000)
	{
	  different[num_different] = job->id;
	  states[num_different ++] = job->state;
	}
      }
      else
        job->dev_state = state;
    }
  }

 /*
  * Then look for jobs assigned to the device but not listed...
  */

  for (job = (server_job_t *)cupsArrayFirst(client->printer->jobs);
       job && num_different < 1000;
       job = (server_job_t *)cupsArrayNext(client->printer->jobs))
  {
    if (job->dev_uuid && !strcmp(job->dev_uuid, device->uuid) && !ippContainsInteger(job_ids, job->id))
    {
      different[num_different] = job->id;
      states[num_different ++] = job->state;
    }
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  if (num_different > 0)
  {
    ippAddIntegers(client->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-ids", num_different, different);
    ippAddIntegers(client->response, IPP_TAG_OPERATION, IPP_TAG_ENUM, "output-device-job-states", num_different, (int *)states);
  }

  if (num_unsupported > 0)
  {
    ippAddIntegers(client->response, IPP_TAG_UNSUPPORTED_GROUP, IPP_TAG_INTEGER, "job-ids", num_unsupported, unsupported);
  }
}


/*
 * 'ipp_update_document_status()' - Update the state of a document.
 */

static void
ipp_update_document_status(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (!job->dev_uuid || strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job not assigned to device.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "document-number", IPP_TAG_ZERO)) == NULL || ippGetGroupTag(attr) != IPP_TAG_OPERATION || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetCount(attr) != 1 || ippGetInteger(attr, 0) != 1)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, attr ? "Bad document-number attribute." : "Missing document-number attribute.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "impressions-completed", IPP_TAG_INTEGER)) != NULL)
  {
    job->impcompleted = ippGetInteger(attr, 0);
    serverAddEvent(client->printer, job, NULL, SERVER_EVENT_JOB_PROGRESS, NULL);
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_update_job_status()' - Update the state of a job.
 */

static void
ipp_update_job_status(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */
  server_event_t		events = SERVER_EVENT_NONE;
					/* Event(s) */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Device was not found.");
    return;
  }

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job was not found.");
    return;
  }

  if (!job->dev_uuid || strcmp(job->dev_uuid, device->uuid))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Job not assigned to device.");
    return;
  }

  if ((attr = ippFindAttribute(client->request, "job-impressions-completed", IPP_TAG_INTEGER)) != NULL)
  {
    job->impcompleted = ippGetInteger(attr, 0);
    events |= SERVER_EVENT_JOB_PROGRESS;
  }

  if ((attr = ippFindAttribute(client->request, "output-device-job-state", IPP_TAG_ENUM)) != NULL)
  {
    job->dev_state = (ipp_jstate_t)ippGetInteger(attr, 0);
    events |= SERVER_EVENT_JOB_STATE_CHANGED;
  }

  if ((attr = ippFindAttribute(client->request, "output-device-job-state-reasons", IPP_TAG_KEYWORD)) != NULL)
  {
    job->dev_state_reasons = serverGetJobStateReasonsBits(attr);
    events |= SERVER_EVENT_JOB_STATE_CHANGED;
  }

  if (events)
    serverAddEvent(client->printer, job, NULL, events, NULL);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_update_output_device_attributes()' - Update the values for an output device.
 */

static void
ipp_update_output_device_attributes(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  ipp_attribute_t	*attr,		/* Current attribute */
			*dev_attr;	/* Device attribute */
  server_event_t	events = SERVER_EVENT_NONE;
					/* Config/state changed? */


  if (Authentication)
  {
   /*
    * Require authenticated username belonging to the proxy group...
    */

    if (!client->username[0])
    {
      serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
      return;
    }

    if (!serverAuthorizeUser(client, NULL, client->printer->pinfo.proxy_group, SERVER_SCOPE_DEFAULT))
    {
      serverRespondHTTP(client, HTTP_STATUS_FORBIDDEN, NULL, NULL, 0);
      return;
    }
  }

  if ((device = serverFindDevice(client)) == NULL)
  {
    if ((device = serverCreateDevice(client)) == NULL)
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Unable to add output device.");
      return;
    }
  }

  _cupsRWLockWrite(&device->rwlock);

  attr = ippFirstAttribute(client->request);
  while (attr && ippGetGroupTag(attr) != IPP_TAG_PRINTER)
    attr = ippNextAttribute(client->request);

  for (; attr; attr = ippNextAttribute(client->request))
  {
    const char	*attrname = ippGetName(attr),
					/* Attribute name */
		*dotptr;		/* Pointer to dot in name */

   /*
    * Skip attributes we don't care about...
    */

    if (!attrname)
      continue;

    if (strncmp(attrname, "copies", 6) && strncmp(attrname, "document-format", 15) && strncmp(attrname, "finishings", 10) && strncmp(attrname, "media", 5) && strncmp(attrname, "print-", 6) && strncmp(attrname, "sides", 5) && strncmp(attrname, "printer-alert", 13) && strncmp(attrname, "printer-input", 13) && strncmp(attrname, "printer-output", 14) && strncmp(attrname, "printer-resolution", 18) && strncmp(attrname, "pwg-raster", 10) && strncmp(attrname, "urf-", 4))
      continue;

    if (strncmp(attrname, "printer-alert", 13) || strncmp(attrname, "printer-state", 13))
      events |= SERVER_EVENT_PRINTER_CONFIG_CHANGED;
    else
      events |= SERVER_EVENT_PRINTER_STATE_CHANGED;

    if (!strcmp(attrname, "media-col-ready") || !strcmp(attrname, "media-ready"))
      events |= SERVER_EVENT_PRINTER_MEDIA_CHANGED;

    if (!strcmp(attrname, "finishings-col-ready") || !strcmp(attrname, "finishings-ready"))
      events |= SERVER_EVENT_PRINTER_FINISHINGS_CHANGED;

    if ((dotptr = strrchr(attrname, '.')) != NULL && isdigit(dotptr[1] & 255))
    {
     /*
      * Sparse representation: name.NNN or name.NNN-NNN
      */

      char	temp[256],		/* Temporary name string */
		*tempptr;		/* Pointer into temporary string */
      int	low, high;		/* Low and high numbers in range */

      low = (int)strtol(dotptr + 1, (char **)&dotptr, 10);
      if (dotptr && *dotptr == '-')
        high = (int)strtol(dotptr + 1, NULL, 10);
      else
        high = low;

      strlcpy(temp, attrname, sizeof(temp));
      if ((tempptr = strrchr(temp, '.')) != NULL)
        *tempptr = '\0';

      if (low >= 1 && low <= high && (dev_attr = ippFindAttribute(device->attrs, temp, IPP_TAG_ZERO)) != NULL)
      {
        int	i,			/* Looping var */
        	count = ippGetCount(attr),
        				/* New number of values */
        	dev_count = ippGetCount(dev_attr);
        				/* Current number of values */

	if (ippGetValueTag(attr) != ippGetValueTag(dev_attr) && ippGetValueTag(attr) != IPP_TAG_DELETEATTR)
	{
	  serverRespondUnsupported(client, attr);
	  continue;
	}
        else if (ippGetValueTag(attr) != IPP_TAG_DELETEATTR)
        {
          if (low < dev_count && count < (high - low + 1))
	  {
	   /*
	    * Deleting one or more values...
	    */

	    ippDeleteValues(device->attrs, &dev_attr, low - 1, high - low + 1 - count);
	  }
	  else if (high < dev_count && count > (high - low + 1))
	  {
	   /*
	    * Insert one or more values...
	    */

	    int offset = count - high + low - 1;
					  /* Number of values to insert */

	    switch (ippGetValueTag(dev_attr))
	    {
	      default :
		  break;

	      case IPP_TAG_BOOLEAN :
	          for (i = dev_count; i >= high; i --)
	            ippSetBoolean(device->attrs, &dev_attr, i + offset - 1, ippGetBoolean(dev_attr, i - 1));
		  break;

	      case IPP_TAG_INTEGER :
	      case IPP_TAG_ENUM :
	          for (i = dev_count; i >= high; i --)
	            ippSetInteger(device->attrs, &dev_attr, i + offset - 1, ippGetInteger(dev_attr, i - 1));
		  break;

	      case IPP_TAG_STRING :
	          for (i = dev_count; i >= high; i --)
	          {
	            int datalen;
	            void *data = ippGetOctetString(dev_attr, i - 1, &datalen);

	            ippSetOctetString(device->attrs, &dev_attr, i + offset - 1, data, datalen);
		  }
		  break;

	      case IPP_TAG_DATE :
	          for (i = dev_count; i >= high; i --)
	            ippSetDate(device->attrs, &dev_attr, i + offset - 1, ippGetDate(dev_attr, i - 1));
		  break;

	      case IPP_TAG_RESOLUTION :
	          for (i = dev_count; i >= high; i --)
	          {
	            int xres, yres;	/* Resolution */
	            ipp_res_t units;	/* Units */

	            xres = ippGetResolution(dev_attr, i - 1, &yres, &units);
	            ippSetResolution(device->attrs, &dev_attr, i + offset - 1, units, xres, yres);
		  }
		  break;

	      case IPP_TAG_RANGE :
	          for (i = dev_count; i >= high; i --)
	          {
	            int upper, lower = ippGetRange(dev_attr, i - 1, &upper);
					/* Range */

	            ippSetRange(device->attrs, &dev_attr, i + offset - 1, lower, upper);
		  }
		  break;

	      case IPP_TAG_BEGIN_COLLECTION :
	          for (i = dev_count; i >= high; i --)
	            ippSetCollection(device->attrs, &dev_attr, i + offset - 1, ippGetCollection(dev_attr, i - 1));
		  break;

	      case IPP_TAG_TEXTLANG :
	      case IPP_TAG_NAMELANG :
	      case IPP_TAG_TEXT :
	      case IPP_TAG_NAME :
	      case IPP_TAG_KEYWORD :
	      case IPP_TAG_URI :
	      case IPP_TAG_URISCHEME :
	      case IPP_TAG_CHARSET :
	      case IPP_TAG_LANGUAGE :
	      case IPP_TAG_MIMETYPE :
	          for (i = dev_count; i >= high; i --)
	            ippSetString(device->attrs, &dev_attr, i + offset - 1, ippGetString(dev_attr, i - 1, NULL));
		  break;
	    }
	  }
	}

	switch (ippGetValueTag(attr))
	{
          default : /* Don't allow updates for unknown values */
	      serverRespondUnsupported(client, attr);
              break;

	  case IPP_TAG_DELETEATTR :
	     /*
	      * Delete values from attribute...
	      */

	      if (low < count)
	      {
		if (high > count)
		  high = count;

		ippDeleteValues(device->attrs, &dev_attr, low - 1, high - low + 1);
	      }
	      break;

	  case IPP_TAG_INTEGER :
	  case IPP_TAG_ENUM :
	      for (i = high; i >= low; i --)
	        ippSetInteger(device->attrs, &dev_attr, i, ippGetInteger(attr, i - low));
	      break;

	  case IPP_TAG_BOOLEAN :
	      for (i = high; i >= low; i --)
	        ippSetBoolean(device->attrs, &dev_attr, i, ippGetBoolean(attr, i - low));
	      break;

          case IPP_TAG_STRING :
	      for (i = high; i >= low; i --)
	      {
	        int datalen;
	        void *data = ippGetOctetString(attr, i - low, &datalen);

	        ippSetOctetString(device->attrs, &dev_attr, i, data, datalen);
	      }
              break;

          case IPP_TAG_DATE :
	      for (i = high; i >= low; i --)
	        ippSetDate(device->attrs, &dev_attr, i, ippGetDate(attr, i - low));
              break;

          case IPP_TAG_RESOLUTION :
	      for (i = high; i >= low; i --)
	      {
	        int xres, yres;		/* Resolution */
	        ipp_res_t units;	/* Units */

                xres = ippGetResolution(attr, i - low, &yres, &units);
	        ippSetResolution(device->attrs, &dev_attr, i, units, xres, yres);
	      }
              break;

          case IPP_TAG_RANGE :
	      for (i = high; i >= low; i --)
	      {
	        int upper, lower = ippGetRange(attr, i - low, &upper);
					/* Range */

	        ippSetRange(device->attrs, &dev_attr, i, lower, upper);
	      }
              break;

          case IPP_TAG_BEGIN_COLLECTION :
	      for (i = high; i >= low; i --)
	        ippSetCollection(device->attrs, &dev_attr, i, ippGetCollection(attr, i - low));
              break;

          case IPP_TAG_TEXTLANG :
          case IPP_TAG_NAMELANG :
          case IPP_TAG_TEXT :
          case IPP_TAG_NAME :
          case IPP_TAG_KEYWORD :
          case IPP_TAG_URI :
          case IPP_TAG_URISCHEME :
          case IPP_TAG_CHARSET :
          case IPP_TAG_LANGUAGE :
          case IPP_TAG_MIMETYPE :
	      for (i = high; i >= low; i --)
	        ippSetString(device->attrs, &dev_attr, i, ippGetString(attr, i - low, NULL));
              break;
        }
      }
      else
        serverRespondUnsupported(client, attr);
    }
    else
    {
     /*
      * Regular representation - replace or delete current attribute,
      * if any...
      */

      if ((dev_attr = ippFindAttribute(device->attrs, attrname, IPP_TAG_ZERO)) != NULL)
        ippDeleteAttribute(device->attrs, dev_attr);

      if (ippGetValueTag(attr) != IPP_TAG_DELETEATTR)
        ippCopyAttribute(device->attrs, attr, 0);
    }
  }

  _cupsRWUnlock(&device->rwlock);

  if (events)
  {
    _cupsRWLockWrite(&client->printer->rwlock);
    if (events & SERVER_EVENT_PRINTER_CONFIG_CHANGED)
      serverUpdateDeviceAttributesNoLock(client->printer);
    if (events & SERVER_EVENT_PRINTER_STATE_CHANGED)
      serverUpdateDeviceStateNoLock(client->printer);
    _cupsRWUnlock(&client->printer->rwlock);

    serverAddEvent(client->printer, NULL, NULL, events, NULL);
  }
}


/*
 * 'ipp_validate_document()' - Validate document creation attributes.
 */

static void
ipp_validate_document(
    server_client_t *client)		/* I - Client */
{
  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

  if (valid_doc_attributes(client))
    serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_validate_job()' - Validate job creation attributes.
 */

static void
ipp_validate_job(server_client_t *client)	/* I - Client */
{
  if (Authentication && !client->username[0])
  {
   /*
    * Require authenticated username...
    */

    serverRespondHTTP(client, HTTP_STATUS_UNAUTHORIZED, NULL, NULL, 0);
    return;
  }

  if (Authentication && client->printer->pinfo.print_group != SERVER_GROUP_NONE && !serverAuthorizeUser(client, NULL, client->printer->pinfo.print_group, SERVER_SCOPE_DEFAULT))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_AUTHORIZED, "Not authorized to access this printer.");
    return;
  }

  if (valid_job_attributes(client))
    serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'serverProcessIPP()' - Process an IPP request.
 */

int					/* O - 1 on success, 0 on error */
serverProcessIPP(
    server_client_t *client)		/* I - Client */
{
  ipp_tag_t		group;		/* Current group tag */
  ipp_attribute_t	*attr;		/* Current attribute */
  ipp_attribute_t	*charset;	/* Character set attribute */
  ipp_attribute_t	*language;	/* Language attribute */
  ipp_attribute_t	*uri;		/* Printer URI attribute */
  int			major, minor;	/* Version number */
  const char		*name;		/* Name of attribute */


  serverLogAttributes(client, "Request:", client->request, 1);

 /*
  * First build an empty response message for this request...
  */

  client->operation_id = ippGetOperation(client->request);
  client->response     = ippNewResponse(client->request);

 /*
  * Then validate the request header and required attributes...
  */

  major = ippGetVersion(client->request, &minor);

  if (major < 1 || major > 2)
  {
   /*
    * Return an error, since we only support IPP 1.x and 2.x.
    */

    serverRespondIPP(client, IPP_STATUS_ERROR_VERSION_NOT_SUPPORTED,
                "Bad request version number %d.%d.", major, minor);
  }
  else if (ippGetRequestId(client->request) <= 0)
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Bad request-id %d.",
                ippGetRequestId(client->request));
  else if (!ippFirstAttribute(client->request))
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
                "No attributes in request.");
  else
  {
   /*
    * Make sure that the attributes are provided in the correct order and
    * don't repeat groups...
    */

    for (attr = ippFirstAttribute(client->request),
             group = ippGetGroupTag(attr);
	 attr;
	 attr = ippNextAttribute(client->request))
    {
      if (ippGetGroupTag(attr) < group && ippGetGroupTag(attr) != IPP_TAG_ZERO)
      {
       /*
	* Out of order; return an error...
	*/

	serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
		    "Attribute groups are out of order (%x < %x).",
		    ippGetGroupTag(attr), group);
	break;
      }
      else
	group = ippGetGroupTag(attr);
    }

    if (!attr)
    {
     /*
      * Then make sure that the first three attributes are:
      *
      *     attributes-charset
      *     attributes-natural-language
      *     printer-uri/job-uri
      */

      attr = ippFirstAttribute(client->request);
      name = ippGetName(attr);
      if (attr && name && !strcmp(name, "attributes-charset") &&
          ippGetGroupTag(attr) == IPP_TAG_OPERATION &&
	  ippGetValueTag(attr) == IPP_TAG_CHARSET)
	charset = attr;
      else
	charset = NULL;

      attr = ippNextAttribute(client->request);
      name = ippGetName(attr);

      if (attr && name && !strcmp(name, "attributes-natural-language") &&
          ippGetGroupTag(attr) == IPP_TAG_OPERATION &&
	  ippGetValueTag(attr) == IPP_TAG_LANGUAGE)
	language = attr;
      else
	language = NULL;

      attr = ippNextAttribute(client->request);
      name = ippGetName(attr);

      if (attr && name && (!strcmp(name, "system-uri") || !strcmp(name, "printer-uri") || !strcmp(name, "job-uri")) &&
          ippGetGroupTag(attr) == IPP_TAG_OPERATION &&
	  ippGetValueTag(attr) == IPP_TAG_URI)
	uri = attr;
      else
	uri = NULL;

      if (!uri && RelaxedConformance)
      {
       /*
        * The target URI isn't where it is supposed to be.  See if it is
        * elsewhere in the request...
        */

	if ((attr = ippFindAttribute(client->request, "system-uri", IPP_TAG_URI)) != NULL && ippGetGroupTag(attr) == IPP_TAG_OPERATION)
	  uri = attr;
	else if ((attr = ippFindAttribute(client->request, "printer-uri", IPP_TAG_URI)) != NULL && ippGetGroupTag(attr) == IPP_TAG_OPERATION)
	  uri = attr;
	else if ((attr = ippFindAttribute(client->request, "job-uri", IPP_TAG_URI)) != NULL && ippGetGroupTag(attr) == IPP_TAG_OPERATION)
	  uri = attr;

        if (uri)
	  serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Target URI not the third attribute in the request (section 4.1.5 of RFC 8011).");
      }

      if (charset &&
          strcasecmp(ippGetString(charset, 0, NULL), "us-ascii") &&
          strcasecmp(ippGetString(charset, 0, NULL), "utf-8"))
      {
       /*
        * Bad character set...
	*/

	serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Unsupported character set \"%s\".",
	            ippGetString(charset, 0, NULL));
      }
      else if (!charset || !language || !uri)
      {
       /*
	* Return an error, since attributes-charset,
	* attributes-natural-language, and printer-uri/job-uri are required
	* for all operations.
	*/

	serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST,
	            "Missing required attributes in request.");
      }
      else
      {
        char		scheme[32],	/* URI scheme */
			userpass[32],	/* Username/password in URI */
			host[256],	/* Host name in URI */
			resource[256],	/* Resource path in URI */
                        *resptr;	/* Pointer into resource path */
	int		port;		/* Port number in URI */

        name            = ippGetName(uri);
        client->printer = NULL;

        if (httpSeparateURI(HTTP_URI_CODING_ALL, ippGetString(uri, 0, NULL),
                            scheme, sizeof(scheme),
                            userpass, sizeof(userpass),
                            host, sizeof(host), &port,
                            resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
        {
	  serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES,
	              "Bad \"%s\" value '%s'.", name, ippGetString(uri, 0, NULL));
        }
        else if (!strcmp(name, "job-uri"))
        {
         /*
          * Validate job-uri...
          */

          if (strncmp(resource, "/ipp/print/", 11))
          {
            serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "\"%s\" '%s' not found.", name, ippGetString(uri, 0, NULL));
          }
          else
          {
           /*
            * Strip job-id from resource...
            */

            if ((resptr = strchr(resource + 11, '/')) != NULL)
              *resptr = '\0';

            if ((client->printer = serverFindPrinter(resource)) == NULL)
            {
              serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "\"%s\" '%s' not found.", name, ippGetString(uri, 0, NULL));
            }
          }
        }
        else if ((client->printer = serverFindPrinter(resource)) == NULL)
        {
          if (strcmp(resource, "/ipp/system"))
	    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "\"%s\" '%s' not found.", name, ippGetString(uri, 0, NULL));
        }

	if (client->printer)
	{
	 /*
	  * Try processing the Printer operation...
	  */

	  switch ((int)ippGetOperation(client->request))
	  {
	    case IPP_OP_PRINT_JOB :
		ipp_print_job(client);
		break;

	    case IPP_OP_PRINT_URI :
		ipp_print_uri(client);
		break;

	    case IPP_OP_VALIDATE_JOB :
		ipp_validate_job(client);
		break;

	    case IPP_OP_CREATE_JOB :
		ipp_create_job(client);
		break;

	    case IPP_OP_SEND_DOCUMENT :
		ipp_send_document(client);
		break;

	    case IPP_OP_SEND_URI :
		ipp_send_uri(client);
		break;

	    case IPP_OP_CANCEL_JOB :
		ipp_cancel_job(client);
		break;

	    case IPP_OP_CANCEL_MY_JOBS :
		ipp_cancel_my_jobs(client);
		break;

	    case IPP_OP_GET_JOB_ATTRIBUTES :
		ipp_get_job_attributes(client);
		break;

	    case IPP_OP_GET_JOBS :
		ipp_get_jobs(client);
		break;

	    case IPP_OP_GET_PRINTER_ATTRIBUTES :
		ipp_get_printer_attributes(client);
		break;

	    case IPP_OP_GET_PRINTER_SUPPORTED_VALUES :
		ipp_get_printer_supported_values(client);
		break;

	    case IPP_OP_CLOSE_JOB :
	        ipp_close_job(client);
		break;

	    case IPP_OP_IDENTIFY_PRINTER :
	        ipp_identify_printer(client);
		break;

	    case IPP_OP_CANCEL_SUBSCRIPTION :
	        ipp_cancel_subscription(client);
		break;

            case IPP_OP_CREATE_JOB_SUBSCRIPTIONS :
	    case IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS :
	        ipp_create_xxx_subscriptions(client);
		break;

	    case IPP_OP_GET_NOTIFICATIONS :
	        ipp_get_notifications(client);
		break;

	    case IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES :
	        ipp_get_subscription_attributes(client);
		break;

	    case IPP_OP_GET_SUBSCRIPTIONS :
	        ipp_get_subscriptions(client);
		break;

	    case IPP_OP_RENEW_SUBSCRIPTION :
	        ipp_renew_subscription(client);
		break;

	    case IPP_OP_GET_DOCUMENT_ATTRIBUTES :
		ipp_get_document_attributes(client);
		break;

	    case IPP_OP_GET_DOCUMENTS :
		ipp_get_documents(client);
		break;

	    case IPP_OP_VALIDATE_DOCUMENT :
		ipp_validate_document(client);
		break;

            case IPP_OP_ACKNOWLEDGE_DOCUMENT :
	        ipp_acknowledge_document(client);
		break;

            case IPP_OP_ACKNOWLEDGE_IDENTIFY_PRINTER :
	        ipp_acknowledge_identify_printer(client);
		break;

            case IPP_OP_ACKNOWLEDGE_JOB :
	        ipp_acknowledge_job(client);
		break;

            case IPP_OP_FETCH_DOCUMENT :
	        ipp_fetch_document(client);
		break;

            case IPP_OP_FETCH_JOB :
	        ipp_fetch_job(client);
		break;

            case IPP_OP_GET_OUTPUT_DEVICE_ATTRIBUTES :
	        ipp_get_output_device_attributes(client);
		break;

            case IPP_OP_UPDATE_ACTIVE_JOBS :
	        ipp_update_active_jobs(client);
		break;

            case IPP_OP_UPDATE_DOCUMENT_STATUS :
	        ipp_update_document_status(client);
		break;

            case IPP_OP_UPDATE_JOB_STATUS :
	        ipp_update_job_status(client);
		break;

            case IPP_OP_UPDATE_OUTPUT_DEVICE_ATTRIBUTES :
	        ipp_update_output_device_attributes(client);
		break;

            case IPP_OP_DEREGISTER_OUTPUT_DEVICE :
	        ipp_deregister_output_device(client);
		break;

	    default :
		serverRespondIPP(client, IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED,
			    "Operation not supported.");
		break;
	  }
	}
	else if (!strcmp(resource, "/ipp/system"))
	{
	 /*
	  * Try processing the System operation...
	  */

	  switch ((int)ippGetOperation(client->request))
	  {
	    case IPP_OP_GET_PRINTER_ATTRIBUTES :
	        if (DefaultPrinter)
	        {
	          client->printer = DefaultPrinter;
		  ipp_get_printer_attributes(client);
		}
		else
		{
		  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "No default printer.");
		}
		break;

	    case IPP_OP_CANCEL_SUBSCRIPTION :
	        ipp_cancel_subscription(client);
		break;

	    case IPP_OP_CREATE_SYSTEM_SUBSCRIPTIONS :
	        ipp_create_xxx_subscriptions(client);
		break;

	    case IPP_OP_GET_NOTIFICATIONS :
	        ipp_get_notifications(client);
		break;

	    case IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES :
	        ipp_get_subscription_attributes(client);
		break;

	    case IPP_OP_GET_SUBSCRIPTIONS :
	        ipp_get_subscriptions(client);
		break;

	    case IPP_OP_RENEW_SUBSCRIPTION :
	        ipp_renew_subscription(client);
		break;

	    case IPP_OP_GET_SYSTEM_ATTRIBUTES :
	        ipp_get_system_attributes(client);
	        break;

	    case IPP_OP_GET_SYSTEM_SUPPORTED_VALUES :
	        ipp_get_system_supported_values(client);
	        break;

	    case IPP_OP_SET_SYSTEM_ATTRIBUTES :
	        ipp_set_system_attributes(client);
	        break;

	    default :
		serverRespondIPP(client, IPP_STATUS_ERROR_OPERATION_NOT_SUPPORTED,
			    "Operation not supported.");
		break;
	  }
	}
      }
    }
  }

 /*
  * Send the HTTP header and return...
  */

  if (httpGetState(client->http) != HTTP_STATE_WAITING)
  {
    if (httpGetState(client->http) != HTTP_STATE_POST_SEND)
      httpFlush(client->http);		/* Flush trailing (junk) data */

    serverLogAttributes(client, "Response:", client->response, 2);

    return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "application/ipp", client->fetch_file >= 0 ? 0 : ippLength(client->response)));
  }
  else
    return (1);
}


/*
 * 'serverRespondIPP()' - Send an IPP response.
 */

void
serverRespondIPP(
    server_client_t *client,		/* I - Client */
    ipp_status_t    status,		/* I - status-code */
    const char      *message,		/* I - printf-style status-message */
    ...)				/* I - Additional args as needed */
{
  const char	*formatted = NULL;	/* Formatted message */


  ippSetStatusCode(client->response, status);

  if (message)
  {
    va_list		ap;		/* Pointer to additional args */
    ipp_attribute_t	*attr;		/* New status-message attribute */

    va_start(ap, message);
    if ((attr = ippFindAttribute(client->response, "status-message",
				 IPP_TAG_TEXT)) != NULL)
      ippSetStringfv(client->response, &attr, 0, message, ap);
    else
      attr = ippAddStringfv(client->response, IPP_TAG_OPERATION, IPP_TAG_TEXT,
			    "status-message", NULL, message, ap);
    va_end(ap);

    formatted = ippGetString(attr, 0, NULL);
  }

  if (formatted)
    serverLogClient(SERVER_LOGLEVEL_INFO, client, "%s %s (%s)", ippOpString(client->operation_id), ippErrorString(status), formatted);
  else
    serverLogClient(SERVER_LOGLEVEL_INFO, client, "%s %s", ippOpString(client->operation_id), ippErrorString(status));
}


/*
 * 'serverRespondUnsupported()' - Respond with an unsupported attribute.
 */

void
serverRespondUnsupported(
    server_client_t *client,		/* I - Client */
    ipp_attribute_t *attr)		/* I - Atribute */
{
  ipp_attribute_t	*temp;		/* Copy of attribute */


  serverRespondIPP(client, IPP_STATUS_ERROR_ATTRIBUTES_OR_VALUES,
              "Unsupported %s %s%s value.", ippGetName(attr),
              ippGetCount(attr) > 1 ? "1setOf " : "",
	      ippTagString(ippGetValueTag(attr)));

  temp = ippCopyAttribute(client->response, attr, 0);
  ippSetGroupTag(client->response, &temp, IPP_TAG_UNSUPPORTED_GROUP);
}


/*
 * 'valid_doc_attributes()' - Determine whether the document attributes are
 *                            valid.
 *
 * When one or more document attributes are invalid, this function adds a
 * suitable response and attributes to the unsupported group.
 */

static int				/* O - 1 if valid, 0 if not */
valid_doc_attributes(
    server_client_t *client)		/* I - Client */
{
  int			valid = 1;	/* Valid attributes? */
  ipp_op_t		op = ippGetOperation(client->request);
					/* IPP operation */
  const char		*op_name = ippOpString(op);
					/* IPP operation name */
  ipp_attribute_t	*attr,		/* Current attribute */
			*supported;	/* xxx-supported attribute */
  const char		*compression = NULL,
					/* compression value */
			*format = NULL;	/* document-format value */


 /*
  * Check operation attributes...
  */

  if ((attr = ippFindAttribute(client->request, "compression", IPP_TAG_ZERO)) != NULL)
  {
   /*
    * If compression is specified, only accept a supported value in a Print-Job
    * or Send-Document request...
    */

    compression = ippGetString(attr, 0, NULL);
    supported   = ippFindAttribute(client->printer->pinfo.attrs,
                                   "compression-supported", IPP_TAG_KEYWORD);

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
        ippGetGroupTag(attr) != IPP_TAG_OPERATION ||
        (op != IPP_OP_PRINT_JOB && op != IPP_OP_SEND_DOCUMENT &&
         op != IPP_OP_VALIDATE_JOB) ||
        !ippContainsString(supported, compression))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else
    {
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s compression='%s'", op_name, compression);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "compression-supplied", NULL, compression);

      if (strcmp(compression, "none"))
        httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, compression);
    }
  }

 /*
  * Is it a format we support?
  */

  if ((attr = ippFindAttribute(client->request, "document-format", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_MIMETYPE ||
        ippGetGroupTag(attr) != IPP_TAG_OPERATION)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else
    {
      format = ippGetString(attr, 0, NULL);

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s document-format='%s'", op_name, format);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-supplied", NULL, format);
    }
  }
  else
  {
    format = ippGetString(ippFindAttribute(client->printer->pinfo.attrs, "document-format-default", IPP_TAG_MIMETYPE), 0, NULL);
    if (!format)
      format = "application/octet-stream"; /* Should never happen */

    attr = ippAddString(client->request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
  }

  if ((!format || !strcmp(format, "application/octet-stream")) && (ippGetOperation(client->request) == IPP_OP_PRINT_JOB || ippGetOperation(client->request) == IPP_OP_SEND_DOCUMENT))
  {
   /*
    * Auto-type the file using the first 8 bytes of the file...
    */

    unsigned char	header[8];	/* First 8 bytes of file */

    memset(header, 0, sizeof(header));
    httpPeek(client->http, (char *)header, sizeof(header));

    if (!memcmp(header, "%PDF", 4))
      format = "application/pdf";
    else if (!memcmp(header, "%!", 2))
      format = "application/postscript";
    else if (!memcmp(header, "\377\330\377", 3) && header[3] >= 0xe0 && header[3] <= 0xef)
      format = "image/jpeg";
    else if (!memcmp(header, "\211PNG", 4))
      format = "image/png";
    else if (!memcmp(header, "RAS2", 4))
      format = "image/pwg-raster";
    else if (!memcmp(header, "UNIRAST", 8))
      format = "image/urf";
    else
      format = NULL;

    if (format)
    {
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "%s Auto-typed document-format='%s'", op_name, format);

      ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format-detected", NULL, format);
    }
  }

  if ((op == IPP_OP_PRINT_JOB || op == IPP_OP_SEND_DOCUMENT) && (supported = ippFindAttribute(client->printer->pinfo.attrs, "document-format-supported", IPP_TAG_MIMETYPE)) != NULL && !ippContainsString(supported, format) && attr && ippGetGroupTag(attr) == IPP_TAG_OPERATION)
  {
    serverRespondUnsupported(client, attr);
    valid = 0;
  }

 /*
  * document-name
  */

  if ((attr = ippFindAttribute(client->request, "document-name", IPP_TAG_NAME)) != NULL)
    ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_NAME, "document-name-supplied", NULL, ippGetString(attr, 0, NULL));

  return (valid);
}


/*
 * 'valid_job_attributes()' - Determine whether the job attributes are valid.
 *
 * When one or more job attributes are invalid, this function adds a suitable
 * response and attributes to the unsupported group.
 */

static int				/* O - 1 if valid, 0 if not */
valid_job_attributes(
    server_client_t *client)		/* I - Client */
{
  int			i,		/* Looping var */
			count,		/* Number of values */
			valid = 1;	/* Valid attributes? */
  ipp_attribute_t	*attr,		/* Current attribute */
			*supported;	/* xxx-supported attribute */


 /*
  * Check operation attributes...
  */

  valid = valid_doc_attributes(client);

 /*
  * Check the various job template attributes...
  */

  if ((attr = ippFindAttribute(client->request, "copies", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER ||
        ippGetInteger(attr, 0) < 1 || ippGetInteger(attr, 0) > 999)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "ipp-attribute-fidelity", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_BOOLEAN)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-hold-until", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD) ||
	strcmp(ippGetString(attr, 0, NULL), "no-hold"))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-impressions", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER || ippGetInteger(attr, 0) < 0)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-name", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }

    ippSetGroupTag(client->request, &attr, IPP_TAG_JOB);
  }
  else
    ippAddString(client->request, IPP_TAG_JOB, IPP_TAG_NAME, "job-name", NULL, "Untitled");

  if ((attr = ippFindAttribute(client->request, "job-priority", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_INTEGER ||
        ippGetInteger(attr, 0) < 1 || ippGetInteger(attr, 0) > 100)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "job-sheets", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD) ||
	strcmp(ippGetString(attr, 0, NULL), "none"))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "media", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 ||
        (ippGetValueTag(attr) != IPP_TAG_NAME &&
	 ippGetValueTag(attr) != IPP_TAG_NAMELANG &&
	 ippGetValueTag(attr) != IPP_TAG_KEYWORD))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else
    {
      if ((supported = ippFindAttribute(client->printer->dev_attrs, "media-supported", IPP_TAG_KEYWORD)) == NULL)
        supported = ippFindAttribute(client->printer->pinfo.attrs, "media-supported", IPP_TAG_KEYWORD);

      if (!ippContainsString(supported, ippGetString(attr, 0, NULL)))
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "media-col", IPP_TAG_ZERO)) != NULL)
  {
    ipp_t		*col,		/* media-col collection */
			*size;		/* media-size collection */
    ipp_attribute_t	*member,	/* Member attribute */
			*x_dim,		/* x-dimension */
			*y_dim;		/* y-dimension */
    int			x_value,	/* y-dimension value */
			y_value;	/* x-dimension value */

    if (ippGetCount(attr) != 1 ||
        ippGetValueTag(attr) != IPP_TAG_BEGIN_COLLECTION)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }

    col = ippGetCollection(attr, 0);

    if ((member = ippFindAttribute(col, "media-size-name", IPP_TAG_ZERO)) != NULL)
    {
      if (ippGetCount(member) != 1 ||
	  (ippGetValueTag(member) != IPP_TAG_NAME &&
	   ippGetValueTag(member) != IPP_TAG_NAMELANG &&
	   ippGetValueTag(member) != IPP_TAG_KEYWORD))
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
      else
      {
        if ((supported = ippFindAttribute(client->printer->dev_attrs, "media-supported", IPP_TAG_KEYWORD)) == NULL)
	  supported = ippFindAttribute(client->printer->pinfo.attrs, "media-supported", IPP_TAG_KEYWORD);

	if (!ippContainsString(supported, ippGetString(member, 0, NULL)))
	{
	  serverRespondUnsupported(client, attr);
	  valid = 0;
	}
      }
    }
    else if ((member = ippFindAttribute(col, "media-size", IPP_TAG_BEGIN_COLLECTION)) != NULL)
    {
      if (ippGetCount(member) != 1)
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
      else
      {
	size = ippGetCollection(member, 0);

	if ((supported = ippFindAttribute(client->printer->dev_attrs, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) == NULL)
	  supported = ippFindAttribute(client->printer->pinfo.attrs, "media-size-supported", IPP_TAG_BEGIN_COLLECTION);

	if ((x_dim = ippFindAttribute(size, "x-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(x_dim) != 1 ||
	    (y_dim = ippFindAttribute(size, "y-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(y_dim) != 1)
	{
	  serverRespondUnsupported(client, attr);
	  valid = 0;
	}
	else if (supported)
	{
	  x_value   = ippGetInteger(x_dim, 0);
	  y_value   = ippGetInteger(y_dim, 0);
	  count     = ippGetCount(supported);

	  for (i = 0; i < count ; i ++)
	  {
	    size  = ippGetCollection(supported, i);
	    x_dim = ippFindAttribute(size, "x-dimension", IPP_TAG_ZERO);
	    y_dim = ippFindAttribute(size, "y-dimension", IPP_TAG_ZERO);

	    if (ippContainsInteger(x_dim, x_value) && ippContainsInteger(y_dim, y_value))
	      break;
	  }

	  if (i >= count)
	  {
	    serverRespondUnsupported(client, attr);
	    valid = 0;
	  }
	}
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "multiple-document-handling", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD ||
        (strcmp(ippGetString(attr, 0, NULL),
		"separate-documents-uncollated-copies") &&
	 strcmp(ippGetString(attr, 0, NULL),
		"separate-documents-collated-copies")))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "orientation-requested", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_ENUM ||
        ippGetInteger(attr, 0) < IPP_ORIENT_PORTRAIT ||
        ippGetInteger(attr, 0) > IPP_ORIENT_REVERSE_PORTRAIT)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "page-ranges", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetValueTag(attr) != IPP_TAG_RANGE)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "print-quality", IPP_TAG_ZERO)) != NULL)
  {
    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_ENUM ||
        ippGetInteger(attr, 0) < IPP_QUALITY_DRAFT ||
        ippGetInteger(attr, 0) > IPP_QUALITY_HIGH)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  if ((attr = ippFindAttribute(client->request, "printer-resolution", IPP_TAG_ZERO)) != NULL)
  {
    if ((supported = ippFindAttribute(client->printer->dev_attrs, "printer-resolution-supported", IPP_TAG_RESOLUTION)) == NULL)
      supported = ippFindAttribute(client->printer->pinfo.attrs, "printer-resolution-supported", IPP_TAG_RESOLUTION);

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_RESOLUTION ||
        !supported)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else
    {
      int	xdpi,			/* Horizontal resolution for job template attribute */
		ydpi,			/* Vertical resolution for job template attribute */
		sydpi;			/* Vertical resolution for supported value */
      ipp_res_t	units,			/* Units for job template attribute */
		sunits;			/* Units for supported value */

      xdpi  = ippGetResolution(attr, 0, &ydpi, &units);
      count = ippGetCount(supported);

      for (i = 0; i < count; i ++)
      {
        if (xdpi == ippGetResolution(supported, i, &sydpi, &sunits) && ydpi == sydpi && units == sunits)
          break;
      }

      if (i >= count)
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
    }
  }

  if ((attr = ippFindAttribute(client->request, "sides", IPP_TAG_ZERO)) != NULL)
  {
    const char *sides = ippGetString(attr, 0, NULL);
					/* "sides" value... */

    if ((supported = ippFindAttribute(client->printer->dev_attrs, "sides-supported", IPP_TAG_KEYWORD)) == NULL)
      supported = ippFindAttribute(client->printer->pinfo.attrs, "sides-supported", IPP_TAG_KEYWORD);

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else if (!ippContainsString(supported, sides) && strcmp(sides, "one-sided"))
    {
      if (!ippContainsString(supported, sides))
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
    }
  }

  return (valid);
}

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
 * Local functions...
 */

static void		copy_job_attributes(server_client_t *client, server_job_t *job, cups_array_t *ra);
static void		copy_subscription_attributes(server_client_t *client, server_subscription_t *sub, cups_array_t *ra);
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
static void		ipp_identify_printer(server_client_t *client);
static void		ipp_print_job(server_client_t *client);
static void		ipp_print_uri(server_client_t *client);
static void		ipp_renew_subscription(server_client_t *client);
static void		ipp_send_document(server_client_t *client);
static void		ipp_send_uri(server_client_t *client);
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
    ipp_tag_t    group_tag,		/* I - Group to copy */
    int          quickcopy)		/* I - Do a quick copy? */
{
  server_filter_t	filter;		/* Filter data */


  filter.ra        = ra;
  filter.group_tag = group_tag;

  ippCopyAttributes(to, from, quickcopy, (ipp_copycb_t)filter_cb, &filter);
}


/*
 * 'copy_job_attrs()' - Copy job attributes to the response.
 */

static void
copy_job_attributes(
    server_client_t *client,		/* I - Client */
    server_job_t    *job,		/* I - Job */
    cups_array_t  *ra)			/* I - requested-attributes */
{
  serverCopyAttributes(client->response, job->attrs, ra, IPP_TAG_JOB, 0);

  if (!ra || cupsArrayFind(ra, "date-time-at-completed"))
  {
    if (job->completed)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-completed", ippTimeToDate(job->completed));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-completed");
  }

  if (!ra || cupsArrayFind(ra, "date-time-at-processing"))
  {
    if (job->processing)
      ippAddDate(client->response, IPP_TAG_JOB, "date-time-at-processing", ippTimeToDate(job->processing));
    else
      ippAddOutOfBand(client->response, IPP_TAG_JOB, IPP_TAG_NOVALUE, "date-time-at-processing");
  }

  if (!ra || cupsArrayFind(ra, "job-impressions"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions", job->impressions);

  if (!ra || cupsArrayFind(ra, "job-impressions-completed"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-impressions-completed", job->impcompleted);

  if (!ra || cupsArrayFind(ra, "job-printer-up-time"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_INTEGER, "job-printer-up-time", (int)(time(NULL) - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "job-state"))
    ippAddInteger(client->response, IPP_TAG_JOB, IPP_TAG_ENUM,
		  "job-state", job->state);

  if (!ra || cupsArrayFind(ra, "job-state-message"))
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

  if (!ra || cupsArrayFind(ra, "job-state-reasons"))
    serverCopyJobStateReasons(client->response, IPP_TAG_JOB, job);
/*
    switch (job->state)
    {
      case IPP_JSTATE_PENDING :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "none");
	  break;

      case IPP_JSTATE_HELD :
          if (job->fd >= 0)
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-incoming");
	  else if (ippFindAttribute(job->attrs, "job-hold-until", IPP_TAG_ZERO))
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-hold-until-specified");
          else
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-data-insufficient");
	  break;

      case IPP_JSTATE_PROCESSING :
	  if (job->cancel)
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "processing-to-stop-point");
	  else
	    ippAddString(client->response, IPP_TAG_JOB,
	                 IPP_CONST_TAG(IPP_TAG_KEYWORD),
	                 "job-state-reasons", NULL, "job-printing");
	  break;

      case IPP_JSTATE_STOPPED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-stopped");
	  break;

      case IPP_JSTATE_CANCELED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-canceled-by-user");
	  break;

      case IPP_JSTATE_ABORTED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "aborted-by-system");
	  break;

      case IPP_JSTATE_COMPLETED :
	  ippAddString(client->response, IPP_TAG_JOB,
	               IPP_CONST_TAG(IPP_TAG_KEYWORD), "job-state-reasons",
		       NULL, "job-completed-successfully");
	  break;
    }
*/

  if (!ra || cupsArrayFind(ra, "time-at-completed"))
    ippAddInteger(client->response, IPP_TAG_JOB,
                  job->completed ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE,
                  "time-at-completed", (int)(job->completed - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "time-at-processing"))
    ippAddInteger(client->response, IPP_TAG_JOB,
                  job->processing ? IPP_TAG_INTEGER : IPP_TAG_NOVALUE,
                  "time-at-processing", (int)(job->processing - client->printer->start_time));
}


/*
 * 'copy_sub_attrs()' - Copy job attributes to the response.
 */

static void
copy_subscription_attributes(
    server_client_t       *client,	/* I - Client */
    server_subscription_t *sub,		/* I - Subscription */
    cups_array_t        *ra)		/* I - requested-attributes */
{
  serverCopyAttributes(client->response, sub->attrs, ra, IPP_TAG_SUBSCRIPTION, 0);

  if (!ra || cupsArrayFind(ra, "notify-lease-expiration-time"))
    ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-lease-expiration-time", (int)(sub->expire - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "notify-printer-up-time"))
    ippAddInteger(client->response, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-printer-up-time", (int)(time(NULL) - client->printer->start_time));

  if (!ra || cupsArrayFind(ra, "notify-sequence-number"))
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

  return (!filter->ra || cupsArrayFind(filter->ra, (void *)name) != NULL);
}


/*
 * 'ipp_acknowledge_document()' - Acknowledge receipt of a document.
 */

static void
ipp_acknowledge_document(
    server_client_t *client)		/* I - Client */
{
  server_device_t		*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */


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
  // TODO: Implement this!
  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Need to implement this.");
}


/*
 * 'ipp_acknowledge_job()' - Acknowledge receipt of a job.
 */

static void
ipp_acknowledge_job(
    server_client_t *client)		/* I - Client */
{
  server_device_t		*device;	/* Device */
  server_job_t		*job;		/* Job */


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

  serverAddEvent(client->printer, job, SERVER_EVENT_JOB_STATE_CHANGED, "Job acknowledged.");

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_cancel_job()' - Cancel a job.
 */

static void
ipp_cancel_job(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* Job information */


 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
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

        serverAddEvent(client->printer, job, SERVER_EVENT_JOB_COMPLETED, NULL);

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
  // TODO: Implement this!
  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Need to implement this.");
}


/*
 * 'ipp_cancel_subscription()' - Cancel a subscription.
 */

static void
ipp_cancel_subscription(
    server_client_t *client)		/* I - Client */
{
  server_subscription_t	*sub;		/* Subscription */


  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
    return;
  }

  _cupsRWLockWrite(&client->printer->rwlock);
  cupsArrayRemove(client->printer->subscriptions, sub);
  serverDeleteSubscription(sub);
  _cupsRWUnlock(&client->printer->rwlock);
  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_close_job()' - Close an open job.
 */

static void
ipp_close_job(server_client_t *client)	/* I - Client */
{
  server_job_t		*job;		/* Job information */


 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
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


 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
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

  copy_job_attributes(client, job, ra);
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
    username = "guest";

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

      if ((sub = serverCreateSubcription(client->printer, job, interval, lease, username, notify_events, notify_attributes, notify_user_data)) != NULL)
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
  server_device_t	*device;		/* Device */


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

  if ((attr = ippFindAttribute(client->request, "document-format-accepted", IPP_TAG_MIMETYPE)) != NULL)
  {
    int	i,				/* Looping var */
	count = ippGetCount(attr);	/* Number of values */

    for (i = 0; i < count; i ++)
    {
      format = ippGetString(attr, i, NULL);

      serverCreateJobFilename(client->printer, job, format, filename, sizeof(filename));

      if (!access(filename, R_OK))
        break;
    }

    if (i >= count)
    {
      if (ippContainsString(attr, "image/pwg-raster"))
      {
       /*
        * Transform and stream document as PWG Raster...
	*/

	serverRespondIPP(client, IPP_STATUS_OK, NULL);
	ippAddString(client->response, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, "image/pwg-raster");
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

        serverTransformJob(client, job, "ipptransform", "image/pwg-raster", SERVER_TRANSFORM_TO_CLIENT);

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
  }
  else if ((attr = ippFindAttribute(job->attrs, "document-format", IPP_TAG_MIMETYPE)) != NULL)
  {
    format = ippGetString(attr, 0, NULL);

    serverCreateJobFilename(client->printer, job, format, filename, sizeof(filename));

    if (access(filename, R_OK))
    {
      serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FETCHABLE, "Document not available in requested format.");
      return;
    }
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
  server_device_t		*device;	/* Device */
  server_job_t		*job;		/* Job */


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
  serverCopyAttributes(client->response, job->attrs, NULL, IPP_TAG_JOB, 0);
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
  // TODO: Implement this!
  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Need to implement this.");
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
  // TODO: Implement this!
  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Need to implement this.");
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


  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job not found.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  ra = ippCreateRequestedArray(client->request);
  copy_job_attributes(client, job, ra);
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
    copy_job_attributes(client, job, ra);
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
//  ipp_attribute_t	*notify_wait;	/* Wait for events? */
  int			i,		/* Looping vars */
			count,		/* Number of IDs */
			first = 1,	/* First event? */
			seq_num;	/* Sequence number */
  server_subscription_t	*sub;		/* Current subscription */
  ipp_t			*event;		/* Current event */


  if ((sub_ids = ippFindAttribute(client->request, "notify-subscription-ids", IPP_TAG_INTEGER)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "Missing notify-subscription-ids attribute.");
    return;
  }

  count       = ippGetCount(sub_ids);
  seq_nums    = ippFindAttribute(client->request, "notify-sequence-numbers", IPP_TAG_INTEGER);
//  notify_wait = ippFindAttribute(client->request, "notify-wait", IPP_TAG_BOOLEAN);

  if (seq_nums && count != ippGetCount(seq_nums))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_BAD_REQUEST, "The notify-subscription-ids and notify-sequence-numbers attributes are different lengths.");
    return;
  }

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  ippAddInteger(client->response, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "notify-get-interval", 30);

  for (i = 0; i < count; i ++)
  {
    if ((sub = serverFindSubscription(client, ippGetInteger(sub_ids, i))) == NULL)
      continue;

    seq_num = ippGetInteger(seq_nums, i);
    if (seq_num < sub->first_sequence)
      seq_num = sub->first_sequence;

    if (seq_num > sub->last_sequence)
      continue;

    for (event = (ipp_t *)cupsArrayIndex(sub->events, seq_num - sub->first_sequence);
         event;
	 event = (ipp_t *)cupsArrayNext(sub->events))
    {
      if (first)
        first = 0;
      else
        ippAddSeparator(client->response);

      ippCopyAttributes(client->response, event, 0, NULL, NULL);
    }
  }
}


/*
 * 'ipp_get_output_device_attributes()' - Get attributes for an output device.
 */

static void
ipp_get_output_device_attributes(
    server_client_t *client)		/* I - Client */
{
  // TODO: Implement this!
  serverRespondIPP(client, IPP_STATUS_ERROR_NOT_POSSIBLE, "Need to implement this.");
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

  serverCopyAttributes(client->response, printer->pinfo.attrs, ra, IPP_TAG_ZERO,
		  IPP_TAG_CUPS_CONST);
  serverCopyAttributes(client->response, printer->dev_attrs, ra, IPP_TAG_ZERO, IPP_TAG_ZERO);

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


  serverRespondIPP(client, IPP_STATUS_OK, NULL);

  serverCopyAttributes(client->response, client->printer->pinfo.attrs, ra, IPP_TAG_PRINTER, 1);

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


  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
  }
  else
  {
    serverRespondIPP(client, IPP_STATUS_OK, NULL);
    copy_subscription_attributes(client, sub, ra);
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


  serverRespondIPP(client, IPP_STATUS_OK, NULL);
  _cupsRWLockRead(&client->printer->rwlock);
  for (sub = (server_subscription_t *)cupsArrayFirst(client->printer->subscriptions);
       sub;
       sub = (server_subscription_t *)cupsArrayNext(client->printer->subscriptions))
  {
    if (first)
      first = 0;
    else
      ippAddSeparator(client->response);

    copy_subscription_attributes(client, sub, ra);
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


  actions = ippFindAttribute(client->request, "identify-actions", IPP_TAG_KEYWORD);
  message = ippFindAttribute(client->request, "message", IPP_TAG_TEXT);

  if (!actions || ippContainsString(actions, "sound"))
  {
    putchar(0x07);
    fflush(stdout);
  }

  if (ippContainsString(actions, "display"))
    printf("IDENTIFY from %s: %s\n", client->hostname, message ? ippGetString(message, 0, NULL) : "No message supplied");

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


 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
  {
    httpFlush(client->http);
    return;
  }

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

  copy_job_attributes(client, job, ra);
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


 /*
  * Validate print job attributes...
  */

  if (!valid_job_attributes(client))
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
    serverRespondIPP(client, IPP_STATUS_ERROR_URI_SCHEME,
                "URI scheme \"%s\" not supported.", scheme);
    return;
  }

  if (!strcmp(scheme, "file") && access(resource, R_OK))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                "Unable to access URI: %s", strerror(errno));
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

  job->fd       = -1;
  job->filename = strdup(filename);
  job->state    = IPP_JSTATE_PENDING;

  /* TODO: Do something different here - only process if the printer is idle */
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

  copy_job_attributes(client, job, ra);
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


  if ((sub = serverFindSubscription(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Subscription was not found.");
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


 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
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

  serverCopyAttributes(job->attrs, client->request, NULL, IPP_TAG_JOB, 0);

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

  copy_job_attributes(client, job, ra);
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


 /*
  * Get the job...
  */

  if ((job = serverFindJob(client, 0)) == NULL)
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "Job does not exist.");
    httpFlush(client->http);
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
    serverRespondIPP(client, IPP_STATUS_ERROR_URI_SCHEME,
                "URI scheme \"%s\" not supported.", scheme);
    return;
  }

  if (!strcmp(scheme, "file") && access(resource, R_OK))
  {
    serverRespondIPP(client, IPP_STATUS_ERROR_DOCUMENT_ACCESS,
                "Unable to access URI: %s", strerror(errno));
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

  copy_job_attributes(client, job, ra);
  cupsArrayDelete(ra);
}


/*
 * 'ipp_update_active_jobs()' - Update the list of active jobs.
 */

static void
ipp_update_active_jobs(
    server_client_t *client)		/* I - Client */
{
  server_device_t		*device;	/* Output device */
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
  server_device_t		*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */


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
    serverAddEvent(client->printer, job, SERVER_EVENT_JOB_PROGRESS, NULL);
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
  server_device_t		*device;	/* Device */
  server_job_t		*job;		/* Job */
  ipp_attribute_t	*attr;		/* Attribute */
  server_event_t		events = SERVER_EVENT_NONE;
					/* Event(s) */


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
    serverAddEvent(client->printer, job, events, NULL);

  serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_update_output_device_attributes()' - Update the values for an output device.
 */

static void
ipp_update_output_device_attributes(
    server_client_t *client)		/* I - Client */
{
  server_device_t		*device;	/* Device */
  ipp_attribute_t	*attr,		/* Current attribute */
			*dev_attr;	/* Device attribute */
  server_event_t		events = SERVER_EVENT_NONE;
					/* Config/state changed? */


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
#if 0
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

      if ((dev_attr = ippFindAttribute(device->attrs, temp, IPP_TAG_ZERO)) != NULL)
      {
      }
      else
#endif /* 0 */
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

    serverAddEvent(client->printer, NULL, events, NULL);
  }
}


/*
 * 'ipp_validate_document()' - Validate document creation attributes.
 */

static void
ipp_validate_document(
    server_client_t *client)		/* I - Client */
{
  if (valid_doc_attributes(client))
    serverRespondIPP(client, IPP_STATUS_OK, NULL);
}


/*
 * 'ipp_validate_job()' - Validate job creation attributes.
 */

static void
ipp_validate_job(server_client_t *client)	/* I - Client */
{
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
	  ippGetValueTag(attr) == IPP_TAG_CHARSET)
	charset = attr;
      else
	charset = NULL;

      attr = ippNextAttribute(client->request);
      name = ippGetName(attr);

      if (attr && name && !strcmp(name, "attributes-natural-language") &&
	  ippGetValueTag(attr) == IPP_TAG_LANGUAGE)
	language = attr;
      else
	language = NULL;

      if ((attr = ippFindAttribute(client->request, "printer-uri",
                                   IPP_TAG_URI)) != NULL)
	uri = attr;
      else if ((attr = ippFindAttribute(client->request, "job-uri",
                                        IPP_TAG_URI)) != NULL)
	uri = attr;
      else
	uri = NULL;

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
	            "Missing required attributes.");
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
          serverRespondIPP(client, IPP_STATUS_ERROR_NOT_FOUND, "\"%s\" '%s' not found.", name, ippGetString(uri, 0, NULL));
        }

	if (client->printer)
	{
	 /*
	  * Try processing the operation...
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

    return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "application/ipp",
			 client->fetch_file >= 0 ? 0 : ippLength(client->response)));
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

	if ((x_dim = ippFindAttribute(size, "x-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(x_dim) != 1 ||
	    (y_dim = ippFindAttribute(size, "y-dimension", IPP_TAG_INTEGER)) == NULL || ippGetCount(y_dim) != 1)
	{
	  serverRespondUnsupported(client, attr);
	  valid = 0;
	}
	else if ((supported = ippFindAttribute(client->printer->pinfo.attrs, "media-size-supported", IPP_TAG_BEGIN_COLLECTION)) != NULL)
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
    supported = ippFindAttribute(client->printer->dev_attrs, "printer-resolution-supported", IPP_TAG_RESOLUTION);

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

    if (ippGetCount(attr) != 1 || ippGetValueTag(attr) != IPP_TAG_KEYWORD)
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
    else if ((supported = ippFindAttribute(client->printer->dev_attrs, "sides-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      if (!ippContainsString(supported, sides))
      {
	serverRespondUnsupported(client, attr);
	valid = 0;
      }
    }
    else if (strcmp(sides, "one-sided"))
    {
      serverRespondUnsupported(client, attr);
      valid = 0;
    }
  }

  return (valid);
}

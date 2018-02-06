/*
 * Subscription object code for sample IPP server implementation.
 *
 * Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2010-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * 'serverAddEvent()' - Add an event to a subscription.
 */

void
serverAddEvent(server_printer_t *printer,	/* I - Printer */
          server_job_t     *job,		/* I - Job, if any */
	  server_event_t   event,		/* I - Event */
	  const char     *message,	/* I - Printf-style notify-text message */
	  ...)				/* I - Additional printf arguments */
{
  server_subscription_t *sub;		/* Current subscription */
  ipp_t		*n;			/* Notify attributes */
  char		text[1024];		/* notify-text value */
  va_list	ap;			/* Argument pointer */


  if (message)
  {
    va_start(ap, message);
    vsnprintf(text, sizeof(text), message, ap);
    va_end(ap);
  }
  else
    text[0] = '\0';

  for (sub = (server_subscription_t *)cupsArrayFirst(printer->subscriptions);
       sub;
       sub = (server_subscription_t *)cupsArrayNext(printer->subscriptions))
  {
    if (sub->mask & event && (!sub->job || job == sub->job))
    {
      _cupsRWLockWrite(&sub->rwlock);

      n = ippNew();
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_CHARSET, "notify-charset", NULL, "utf-8");
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_LANGUAGE, "notify-natural-language", NULL, "en");
      ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-printer-up-time", (int)(time(NULL) - printer->start_time));
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_URI, "notify-printer-uri", NULL, printer->default_uri);
      if (job)
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-job-id", job->id);
      ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-subcription-id", sub->id);
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_URI, "notify-subscription-uuid", NULL, sub->uuid);
      ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-sequence-number", ++ sub->last_sequence);
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_KEYWORD, "notify-subscribed-event", NULL, serverGetNotifySubscribedEvent(event));
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_TEXT, "notify-text", NULL, text);
      if (printer && (event & SERVER_EVENT_PRINTER_ALL))
      {
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_ENUM, "printer-state", printer->state);
	serverCopyPrinterStateReasons(n, IPP_TAG_EVENT_NOTIFICATION, printer);
      }
      if (job && (event & SERVER_EVENT_JOB_ALL))
      {
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_ENUM, "job-state", job->state);
	serverCopyJobStateReasons(n, IPP_TAG_EVENT_NOTIFICATION, job);
	if (event == SERVER_EVENT_JOB_CREATED)
	{
	  ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_NAME, "job-name", NULL, job->name);
	  ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_NAME, "job-originating-user-name", NULL, job->username);
	}
      }

      cupsArrayAdd(sub->events, n);
      if (cupsArrayCount(sub->events) > 100)
      {
        n = (ipp_t *)cupsArrayFirst(sub->events);
	cupsArrayRemove(sub->events, n);
	ippDelete(n);
	sub->first_sequence ++;
      }

      _cupsRWUnlock(&sub->rwlock);
      _cupsCondBroadcast(&SubscriptionCondition);
    }
  }
}


/*
 * 'serverCreateSubcription()' - Create a new subscription object from a
 *                           Print-Job, Create-Job, or Create-xxx-Subscription
 *                           request.
 */

server_subscription_t *		/* O - Subscription object */
serverCreateSubcription(
    server_printer_t  *printer,		/* I - Printer */
    server_job_t      *job,		/* I - Job, if any */
    int             interval,		/* I - Interval for progress events */
    int             lease,		/* I - Lease duration */
    const char      *username,		/* I - User creating the subscription */
    ipp_attribute_t *notify_events,	/* I - Events to monitor */
    ipp_attribute_t *notify_attributes,	/* I - Attributes to report */
    ipp_attribute_t *notify_user_data)	/* I - User data, if any */
{
  server_listener_t *lis = (server_listener_t *)cupsArrayFirst(Listeners);
					/* First listener */
  server_subscription_t	*sub;		/* Subscription */
  ipp_attribute_t	*attr;		/* Subscription attribute */
  char			uuid[64];	/* notify-subscription-uuid value */


 /*
  * Allocate and initialize the subscription object...
  */

  if ((sub = calloc(1, sizeof(server_subscription_t))) == NULL)
  {
    perror("Unable to allocate memory for subscription");
    return (NULL);
  }

  _cupsRWLockWrite(&(printer->rwlock));

  sub->id       = printer->next_sub_id ++;
  sub->mask     = notify_events ? serverGetNotifyEventsBits(notify_events) : SERVER_EVENT_DEFAULT;
  sub->printer  = printer;
  sub->job      = job;
  sub->interval = interval;
  sub->lease    = lease;
  sub->attrs    = ippNew();

  if (lease)
    sub->expire = time(NULL) + sub->lease;
  else
    sub->expire = INT_MAX;

  _cupsRWInit(&(sub->rwlock));

 /*
  * Add subscription description attributes and add to the subscriptions
  * array...
  */

  ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-subscription-id", sub->id);

  httpAssembleUUID(lis->host, lis->port, printer->name, -sub->id, uuid, sizeof(uuid));
  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-subscription-uuid", NULL, uuid);
  sub->uuid = ippGetString(attr, 0, NULL);

  ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-printer-uri", NULL, printer->default_uri);

  if (job)
    ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-job-id", job->id);
  else
    ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-lease-duration", sub->lease);

  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_NAME, "notify-subscriber-user-name", NULL, username);
  sub->username = ippGetString(attr, 0, NULL);

  if (notify_events)
    ippCopyAttribute(sub->attrs, notify_events, 0);
  else
    ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events", NULL, SERVER_EVENT_DEFAULT_STRING);

  ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-pull-method", NULL, "ippget");

  if (notify_attributes)
    ippCopyAttribute(sub->attrs, notify_attributes, 0);

  if (notify_user_data)
    ippCopyAttribute(sub->attrs, notify_user_data, 0);

  sub->events = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_func_t)ippDelete);

  cupsArrayAdd(printer->subscriptions, sub);

  _cupsRWUnlock(&(printer->rwlock));

  return (sub);
}


/*
 * 'serverDeleteSubscription()' - Delete a subscription.
 */

void
serverDeleteSubscription(
    server_subscription_t *sub)		/* I - Subscription */
{
  sub->pending_delete = 1;

  _cupsCondBroadcast(&SubscriptionCondition);

  _cupsRWLockWrite(&sub->rwlock);

  ippDelete(sub->attrs);
  cupsArrayDelete(sub->events);

  _cupsRWDeinit(&sub->rwlock);

  free(sub);
}


/*
 * 'serverFindSubscription()' - Find a subcription.
 */

server_subscription_t *			/* O - Subscription */
serverFindSubscription(
    server_client_t *client,		/* I - Client */
    int             sub_id)		/* I - Subscription ID or 0 */
{
  ipp_attribute_t	*notify_subscription_id;
					/* notify-subscription-id */
  server_subscription_t	key,		/* Search key */
			*sub;		/* Matching subscription */


  if (sub_id > 0)
    key.id = sub_id;
  else if ((notify_subscription_id = ippFindAttribute(client->request, "notify-subscription-id", IPP_TAG_INTEGER)) == NULL)
    return (NULL);
  else
    key.id = ippGetInteger(notify_subscription_id, 0);

  _cupsRWLockRead(&client->printer->rwlock);
  sub = (server_subscription_t *)cupsArrayFind(client->printer->subscriptions, &key);
  _cupsRWUnlock(&client->printer->rwlock);

  return (sub);
}


/*
 * 'serverGetNotifyEventsBits()' - Get the bits associated with "notify-events" values.
 */

server_event_t				/* O - Bits */
serverGetNotifyEventsBits(
    ipp_attribute_t *attr)		/* I - "notify-events" attribute */
{
  int		i, j,			/* Looping vars */
		count;			/* Number of "notify-events" values */
  const char	*keyword;		/* "notify-events" value */
  server_event_t	events = SERVER_EVENT_NONE;
					/* Bits for "notify-events" values */


  count = ippGetCount(attr);
  for (i = 0; i < count; i ++)
  {
    keyword = ippGetString(attr, i, NULL);

    for (j = 0; j < (int)(sizeof(server_events) / sizeof(server_events[0])); j ++)
    {
      if (!strcmp(keyword, server_jreasons[j]))
      {
        events |= (server_event_t)(1 << j);
	break;
      }
    }
  }

  return (events);
}


/*
 * 'serverGetNotifySubscribedEvent()' - Get the event name.
 */

const char *				/* O - Event name */
serverGetNotifySubscribedEvent(
    server_event_t event)		/* I - Event bit */
{
  int		i;			/* Looping var */
  server_event_t	mask;		/* Current mask */


  for (i = 0, mask = 1; i < (int)(sizeof(server_events) / sizeof(server_events[0])); i ++, mask <<= 1)
    if (event & mask)
      return (server_events[i]);

  return ("none");
}

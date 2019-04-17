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
 * Local functions...
 */

static int	compare_subscriptions(server_subscription_t *a, server_subscription_t *b);


/*
 * 'serverAddEventNoLock()' - Add an event to a subscription.
 *
 * Note: Printer, job, resource, and subscription objects are not locked.
 */

void
serverAddEventNoLock(
    server_printer_t  *printer,		/* I - Printer, if any */
    server_job_t      *job,		/* I - Job, if any */
    server_resource_t *res,		/* I - Resource, if any */
    server_event_t    event,		/* I - Event */
    const char        *message,		/* I - Printf-style notify-text message */
    ...)				/* I - Additional printf arguments */
{
  server_subscription_t *sub;		/* Current subscription */
  ipp_t			*n;		/* Notify event attributes */
  ipp_attribute_t	*attr;		/* Event attribute */
  char			text[1024];	/* notify-text value */
  va_list		ap;		/* Argument pointer */


  if (message)
  {
    va_start(ap, message);
    vsnprintf(text, sizeof(text), message, ap);
    va_end(ap);
  }
  else
    text[0] = '\0';

  serverLog(SERVER_LOGLEVEL_DEBUG, "serverAddEventNoLock(printer=%p(%s), job=%p(%d), event=0x%x, message=\"%s\")", (void *)printer, printer ? printer->name : "(null)", (void *)job, job ? job->id : -1, event, text);

  _cupsRWLockRead(&SubscriptionsRWLock);

  for (sub = (server_subscription_t *)cupsArrayFirst(Subscriptions); sub; sub = (server_subscription_t *)cupsArrayNext(Subscriptions))
  {
    serverLog(SERVER_LOGLEVEL_DEBUG, "serverAddEvent: sub->id=%d, sub->mask=0x%x, sub->job=%p(%d)", sub->id, sub->mask, (void *)sub->job, sub->job ? sub->job->id : -1);

    if (sub->mask & event && (!sub->job || job == sub->job) && (!sub->printer || printer == sub->printer) && (!sub->resource || res == sub->resource))
    {
      _cupsRWLockWrite(&sub->rwlock);

      n = ippNew();
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_CHARSET, "notify-charset", NULL, sub->charset);
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_LANGUAGE, "notify-natural-language", NULL, sub->language);
      if (printer)
	ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_URI, "notify-printer-uri", NULL, printer->default_uri);
      else
	ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_URI, "notify-system-uri", NULL, DefaultSystemURI);

      if (job)
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-job-id", job->id);
      if (res)
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-resource-id", res->id);
      ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-subscription-id", sub->id);
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_URI, "notify-subscription-uuid", NULL, sub->uuid);
      ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "notify-sequence-number", ++ sub->last_sequence);
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_KEYWORD, "notify-subscribed-event", NULL, serverGetNotifySubscribedEvent(event));
      ippAddString(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_TEXT, "notify-text", NULL, text);
      if (sub->userdata)
      {
        attr = ippCopyAttribute(n, sub->userdata, 0);
        ippSetGroupTag(n, &attr, IPP_TAG_EVENT_NOTIFICATION);
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
      if (!sub->job && printer && (event & SERVER_EVENT_PRINTER_ALL))
      {
	ippAddBoolean(n, IPP_TAG_EVENT_NOTIFICATION, "printer-is-accepting-jobs", printer->is_accepting);
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_ENUM, "printer-state", printer->state);
	serverCopyPrinterStateReasons(n, IPP_TAG_EVENT_NOTIFICATION, printer);
      }
      if (printer)
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "printer-up-time", (int)(time(NULL) - printer->start_time));
      else
	ippAddInteger(n, IPP_TAG_EVENT_NOTIFICATION, IPP_TAG_INTEGER, "system-up-time", (int)(time(NULL) - SystemStartTime));

      cupsArrayAdd(sub->events, n);
      if (cupsArrayCount(sub->events) > 100)
      {
        n = (ipp_t *)cupsArrayFirst(sub->events);
	cupsArrayRemove(sub->events, n);
	ippDelete(n);
	sub->first_sequence ++;
      }

      _cupsRWUnlock(&sub->rwlock);

      serverLog(SERVER_LOGLEVEL_DEBUG, "Broadcasting new event.");
      _cupsCondBroadcast(&NotificationCondition);
    }
  }

  _cupsRWUnlock(&SubscriptionsRWLock);
}


/*
 * 'serverCreateSubscription()' - Create a new subscription object from a
 *                                Print-Job, Create-Job, or
 *                                Create-xxx-Subscription request.
 */

server_subscription_t *			/* O - Subscription object */
serverCreateSubscription(
    server_client_t *client,		/* I - Client */
    int             interval,		/* I - Interval for progress events */
    int             lease,		/* I - Lease duration */
    const char      *username,		/* I - User creating the subscription */
    ipp_attribute_t *notify_charset,	/* I - Character set for notifications */
    ipp_attribute_t *notify_natural_language,
					/* I - Language for notifications */
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

  _cupsRWLockWrite(&SubscriptionsRWLock);

  sub->id       = NextSubscriptionId ++;
  sub->mask     = notify_events ? serverGetNotifyEventsBits(notify_events) : SERVER_EVENT_DEFAULT;
  sub->printer  = client->printer;
  sub->job      = client->job;
  sub->resource = client->resource;
  sub->interval = interval;
  sub->lease    = lease;
  sub->attrs    = ippNew();

  serverLog(SERVER_LOGLEVEL_DEBUG, "serverCreateSubscription: notify-subscription-id=%d, printer=%p(%s)", sub->id, (void *)client->printer, client->printer ? client->printer->name : "(null)");

  if (lease)
    sub->expire = time(NULL) + sub->lease;
  else
    sub->expire = INT_MAX;

  _cupsRWInit(&(sub->rwlock));

 /*
  * Add subscription description attributes and add to the subscriptions
  * array...
  */

  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_CHARSET, "notify-charset", NULL, ippGetString(notify_charset ? notify_charset : ippFindAttribute(client->request, "attributes-charset", IPP_TAG_CHARSET), 0, NULL));
  sub->charset = ippGetString(attr, 0, NULL);

  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_LANGUAGE, "notify-natural-language", NULL, ippGetString(notify_natural_language ? notify_natural_language : ippFindAttribute(client->request, "attributes-natural-language", IPP_TAG_LANGUAGE), 0, NULL));
  sub->language = ippGetString(attr, 0, NULL);

  ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-subscription-id", sub->id);

  httpAssembleUUID(lis->host, lis->port, client->printer ? client->printer->name : "_system_", -sub->id, uuid, sizeof(uuid));
  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-subscription-uuid", NULL, uuid);
  sub->uuid = ippGetString(attr, 0, NULL);

  if (client->printer)
    ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-printer-uri", NULL, client->printer->default_uri);
  else
    ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-system-uri", NULL, DefaultSystemURI);

  if (client->job)
    ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-job-id", client->job->id);
  else if (client->resource)
    ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-resource-id", client->resource->id);
  else
    ippAddInteger(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER, "notify-lease-duration", sub->lease);

  attr = ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_TAG_NAME, "notify-subscriber-user-name", NULL, username);
  sub->username = ippGetString(attr, 0, NULL);

  if (notify_events)
  {
    int		i;			/* Looping var */
    server_event_t mask;		/* Event mask */

    ippCopyAttribute(sub->attrs, notify_events, 0);

    serverLog(SERVER_LOGLEVEL_DEBUG, "serverCreateSubscription: notify-events has %d values.", ippGetCount(notify_events));

    for (i = 0, mask = SERVER_EVENT_DOCUMENT_COMPLETED; i < (int)(sizeof(server_events) / sizeof(server_events[0])); i ++, mask *= 2)
    {
      if (ippContainsString(notify_events, server_events[i]))
      {
	serverLog(SERVER_LOGLEVEL_DEBUG, "serverCreateSubscription: Adding 0x%x (%s) to mask bits.", mask, server_events[i]);
	sub->mask |= mask;
      }
    }
  }
  else
  {
    ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-events", NULL, SERVER_EVENT_DEFAULT_STRING);
    sub->mask = SERVER_EVENT_DEFAULT;
  }

  serverLog(SERVER_LOGLEVEL_DEBUG, "serverCreateSubscription: sub->mask=0x%x", sub->mask);

  ippAddString(sub->attrs, IPP_TAG_SUBSCRIPTION, IPP_CONST_TAG(IPP_TAG_KEYWORD), "notify-pull-method", NULL, "ippget");

  if (notify_attributes)
    ippCopyAttribute(sub->attrs, notify_attributes, 0);

  if (notify_user_data)
    sub->userdata = ippCopyAttribute(sub->attrs, notify_user_data, 0);

  sub->events = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_func_t)ippDelete);

  if (!Subscriptions)
    Subscriptions = cupsArrayNew((cups_array_func_t)compare_subscriptions, NULL);

  cupsArrayAdd(Subscriptions, sub);

  _cupsRWUnlock(&SubscriptionsRWLock);

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

  serverLog(SERVER_LOGLEVEL_DEBUG, "Broadcasting deleted subscription.");
  _cupsCondBroadcast(&NotificationCondition);

  _cupsRWLockWrite(&sub->rwlock);

  ippDelete(sub->attrs);
  cupsArrayDelete(sub->events);

  _cupsRWDeinit(&sub->rwlock);

  free(sub);
}


/*
 * 'serverFindSubscription()' - Find a subscription.
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


  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverFindSubscription: sub_id=%d, printer=%p(%s)", sub_id, (void *)client->printer, client->printer ? client->printer->name : "(null)");

  if (sub_id > 0)
    key.id = sub_id;
  else if ((notify_subscription_id = ippFindAttribute(client->request, "notify-subscription-id", IPP_TAG_INTEGER)) == NULL)
    return (NULL);
  else
    key.id = ippGetInteger(notify_subscription_id, 0);

  _cupsRWLockRead(&SubscriptionsRWLock);
  sub = (server_subscription_t *)cupsArrayFind(Subscriptions, &key);
  _cupsRWUnlock(&SubscriptionsRWLock);

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverFindSubscription: sub=%p", (void *)sub);

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
  server_event_t events = SERVER_EVENT_NONE;
					/* Bits for "notify-events" values */


  count = ippGetCount(attr);
  for (i = 0; i < count; i ++)
  {
    keyword = ippGetString(attr, i, NULL);

    for (j = 0; j < (int)(sizeof(server_events) / sizeof(server_events[0])); j ++)
    {
      if (!strcmp(keyword, server_events[j]))
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
  server_event_t mask;			/* Current mask */


  for (i = 0, mask = 1; i < (int)(sizeof(server_events) / sizeof(server_events[0])); i ++, mask <<= 1)
    if (event & mask)
      return (server_events[i]);

  return ("none");
}


/*
 * 'compare_subscriptions()' - Compare two subscriptions.
 */

static int				/* O - Result of comparison */
compare_subscriptions(
    server_subscription_t *a,		/* I - First subscription */
    server_subscription_t *b)		/* I - Second subscription */
{
  return (b->id - a->id);
}

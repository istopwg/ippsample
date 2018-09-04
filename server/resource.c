/*
 * Resource object code for sample IPP server implementation.
 *
 * Copyright © 2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * Local functions...
 */

static int	compare_ids(server_resource_t *a, server_resource_t *b);
static int	compare_resources(server_resource_t *a, server_resource_t *b);


/*
 * 'serverAddResourceFile()' - Add the file associated with a resource.
 */

void
serverAddResourceFile(
    server_resource_t *res,		/* I - Resource */
    const char        *filename,	/* I - File */
    const char        *format)		/* I - MIME media type */
{
  server_listener_t *lis = (server_listener_t *)cupsArrayFirst(Listeners);
					/* First listener */
  char		uri[1024];		/* resource-data-uri value */
  struct stat	resinfo;		/* Resource info */


  _cupsRWLockWrite(&res->rwlock);

  res->filename = strdup(filename);
  res->format   = strdup(format);
  res->state    = IPP_RSTATE_AVAILABLE;

#ifdef HAVE_SSL
  if (Encryption != HTTP_ENCRYPTION_NEVER)
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "https", NULL, lis->host, lis->port, res->resource);
  else
#endif /* HAVE_SSL */
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "http", NULL, lis->host, lis->port, res->resource);
  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_URI, "resource-data-uri", NULL, uri);

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_MIMETYPE, "resource-format", NULL, res->format);

  if (!stat(res->filename, &resinfo))
    ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "resource-k-octets", (int)((resinfo.st_size + 1023) / 1024));
  else
    ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "resource-k-octets", 0);

  serverAddEventNoLock(NULL, NULL, res, SERVER_EVENT_RESOURCE_STATE_CHANGED, "Resource %d now available.", res->id);

  _cupsRWUnlock(&res->rwlock);
}


/*
 * 'serverCreateResource()' - Create a new resource object.
 */

server_resource_t *			/* O - Resource object */
serverCreateResource(
    const char *resource,		/* I - Remote resource path */
    const char *filename,		/* I - Local filename or `NULL` */
    const char *format,			/* I - MIME media type or `NULL` */
    const char *name,			/* I - Resource name */
    const char *info,			/* I - Resource info */
    const char *type,			/* I - Resource type */
    const char *owner)			/* I - Owner */
{
  server_listener_t *lis = (server_listener_t *)cupsArrayFirst(Listeners);
					/* First listener */
  server_resource_t	*res;		/* Resource */
  char			uri[1024],	/* resource-data-uri value */
			uuid[64];	/* resource-uuid value */
  time_t		curtime = time(NULL);
					/* Current system time */


 /*
  * Provide default values...
  */

  if (!name)
  {
    if (!filename)
      name = "unknown";
    else if ((name = strrchr(filename, '/')) != NULL)
      name ++;
    else
      name = filename;
  }

  if (!info)
  {
    if (!filename)
      info = "Unknown";
    else if ((info = strrchr(filename, '/')) != NULL)
      info ++;
    else
      info = filename;
  }

  if (!type)
  {
    const char *ext;			/* Extension */

    if (!filename || (ext = strrchr(filename, '.')) == NULL || !strcmp(ext, ".jpg") || !strcmp(ext, ".png"))
      type = "static-image";
    else if (!strcmp(ext, ".icc"))
      type = "static-icc-profile";
    else if (!strcmp(ext, ".strings"))
      type = "static-strings";
    else
      type = "static-other";
  }

 /*
  * Allocate and initialize the resource object...
  */

  if ((res = calloc(1, sizeof(server_resource_t))) == NULL)
  {
    perror("Unable to allocate memory for resource");
    return (NULL);
  }

  _cupsRWLockWrite(&ResourcesRWLock);

  res->attrs = ippNew();
  res->id    = NextResourceId ++;
  res->state = IPP_RSTATE_PENDING;

  if (resource)
  {
    res->resource = strdup(resource);
  }
  else
  {
    char	path[1024];		/* Resource path */
    const char	*ext;			/* Extension */

    if ((ext = strrchr(name, '.')) == NULL || (strcmp(ext, ".strings") && strlen(ext) != 4))
      ext = "";

    snprintf(path, sizeof(path), "/ipp/system/%d%s", res->id, ext);
    res->resource = strdup(path);
  }

  _cupsRWInit(&res->rwlock);
  _cupsRWLockWrite(&res->rwlock);

 /*
  * Add resource attributes and add the object to the resources arrays...
  */

  ippAddDate(res->attrs, IPP_TAG_RESOURCE, "date-time-at-creation", ippTimeToDate(curtime));

  if (res->state == IPP_RSTATE_INSTALLED)
    ippAddDate(res->attrs, IPP_TAG_RESOURCE, "date-time-at-installed", ippTimeToDate(curtime));
  else
    ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "date-time-at-installed");

  ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "date-time-at-canceled");

  ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "resource-id", res->id);

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_TEXT, "resource-info", NULL, info);

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NAME, "resource-name", NULL, name);

  if (owner)
  {
    ipp_t	*col = ippNew();	/* owner-col value */
    char	vcard[1024];		/* owner-vcard value */

    /* Yes, this is crap - we need owner-name */
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "username", NULL, NULL, 0, owner);
    snprintf(vcard, sizeof(vcard),
	     "BEGIN:VCARD\r\n"
	     "VERSION:4.0\r\n"
	     "FN:%s\r\n"
	     "END:VCARD\r\n", owner);
    ippAddString(col, IPP_TAG_ZERO, IPP_TAG_URI, "owner-uri", NULL, uri);
    ippAddString(col, IPP_TAG_ZERO, IPP_TAG_TEXT, "owner-vcard", NULL, vcard);

    ippAddCollection(res->attrs, IPP_TAG_RESOURCE, "resource-owner-col", col);
    ippDelete(col);
  }
  else
    ippAddCollection(res->attrs, IPP_TAG_RESOURCE, "resource-owner-col", ippGetCollection(ippFindAttribute(SystemAttributes, "system-owner-col", IPP_TAG_BEGIN_COLLECTION), 0));

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_KEYWORD, "resource-type", NULL, type);

  httpAssembleUUID(lis->host, lis->port, "_system_", res->id, uuid, sizeof(uuid));
  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_URI, "resource-uuid", NULL, uuid);

  ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "time-at-creation", (int)(curtime - SystemStartTime));

  if (res->state == IPP_RSTATE_INSTALLED)
    ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "time-at-installed", (int)(curtime - SystemStartTime));
  else
    ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "time-at-installed");

  ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "time-at-canceled");

  if (!ResourcesById)
    ResourcesById = cupsArrayNew((cups_array_func_t)compare_ids, NULL);
  if (!ResourcesByPath)
    ResourcesByPath = cupsArrayNew((cups_array_func_t)compare_resources, NULL);

  cupsArrayAdd(ResourcesById, res);
  cupsArrayAdd(ResourcesByPath, res);

  _cupsRWUnlock(&ResourcesRWLock);

  serverAddEventNoLock(NULL, NULL, res, SERVER_EVENT_RESOURCE_CREATED | SERVER_EVENT_RESOURCE_STATE_CHANGED, "Resource %d created.", res->id);

  _cupsRWUnlock(&res->rwlock);

  if (filename)
    serverAddResourceFile(res, filename, format);

  return (res);
}


/*
 * 'serverDeleteResource()' - Delete a resource.
 */

void
serverDeleteResource(
    server_resource_t *res)		/* I - Resource */
{
  _cupsRWLockWrite(&ResourcesRWLock);

  cupsArrayRemove(ResourcesById, res);
  cupsArrayRemove(ResourcesByPath, res);

  _cupsRWLockWrite(&res->rwlock);

  ippDelete(res->attrs);

  if (res->filename)
    free(res->filename);
  if (res->format)
    free(res->format);
  if (res->resource)
    free(res->resource);

  _cupsRWUnlock(&res->rwlock);
  _cupsRWDeinit(&res->rwlock);

  free(res);

  _cupsRWUnlock(&ResourcesRWLock);
}


/*
 * 'serverFindResourceById()' - Find a resource by its ID.
 */

server_resource_t *			/* O - Resource */
serverFindResourceById(int id)		/* I - Resource ID */
{
  server_resource_t	key,		/* Search key */
			*res;		/* Matching resource */


  key.id = id;

  _cupsRWLockRead(&ResourcesRWLock);
  res = (server_resource_t *)cupsArrayFind(ResourcesById, &key);
  _cupsRWUnlock(&ResourcesRWLock);

  return (res);
}


/*
 * 'serverFindResourceByPath()' - Find a resource by its path.
 */

server_resource_t *			/* O - Resource */
serverFindResourceByPath(
    const char *resource)		/* I - Resource path */
{
  server_resource_t	key,		/* Search key */
			*res;		/* Matching resource */


  key.resource = (char *)resource;

  _cupsRWLockRead(&ResourcesRWLock);
  res = (server_resource_t *)cupsArrayFind(ResourcesByPath, &key);
  _cupsRWUnlock(&ResourcesRWLock);

  return (res);
}


/*
 * 'compare_ids()' - Compare two resource IDs.
 */

static int				/* O - Result of comparison */
compare_ids(
    server_resource_t *a,		/* I - First resource */
    server_resource_t *b)		/* I - Second resource */
{
  return (b->id - a->id);
}


/*
 * 'compare_resources()' - Compare two resources by path.
 */

static int				/* O - Result of comparison */
compare_resources(
    server_resource_t *a,		/* I - First resource */
    server_resource_t *b)		/* I - Second resource */
{
  return (strcmp(a->resource, b->resource));
}

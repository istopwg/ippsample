/*
 * Resource object code for sample IPP server implementation.
 *
 * Copyright © 2018-2019 by the IEEE-ISTO Printer Working Group
 * Copyright © 2018-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"


/*
 * Local functions...
 */

static int	compare_filenames(server_resource_t *a, server_resource_t *b);
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

  _cupsRWLockWrite(&ResourcesRWLock);

  cupsArrayAdd(ResourcesByFilename, res);

  if (!res->resource)
  {
    char path[1024];			/* Resource path */

    serverCreateResourceFilename(res, format, "/ipp/resource", path, sizeof(path));

    res->resource = strdup(path);
    cupsArrayAdd(ResourcesByPath, res);
  }

  _cupsRWUnlock(&ResourcesRWLock);

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
 * 'serverCreateResourceFilename()' - Create a filename for a resource.
 */

void
serverCreateResourceFilename(
    server_resource_t *res,		/* I - Resource object */
    const char        *format,		/* I - MIME media type */
    const char        *prefix,		/* I - Directory prefix */
    char              *fname,		/* I - Filename buffer */
    size_t            fnamesize)	/* I - Size of filename buffer */
{
  char			name[256],	/* "Safe" filename */
			*nameptr;	/* Pointer into filename */
  const char		*ext,		/* Filename extension */
			*res_name;	/* resource-name value */
  ipp_attribute_t	*res_name_attr;	/* resource-name attribute */


  if ((res_name_attr = ippFindAttribute(res->attrs, "resource-name", IPP_TAG_NAME)) != NULL)
    res_name = ippGetString(res_name_attr, 0, NULL);
  else
    res_name = "untitled";

  if (!strcmp(format, "application/ipp"))
    ext = ".ipp";
  else if (!strcmp(format, "application/pdf"))
    ext = ".pdf";
  else if (!strcmp(format, "application/vnd.iccprofile"))
    ext = ".icc";
  else if (!strcmp(format, "image/jpeg"))
    ext = ".jpg";
  else if (!strcmp(format, "image/png"))
    ext = ".png";
  else if (!strcmp(format, "text/strings"))
    ext = ".strings";
  else
    ext = "";

  for (nameptr = name; *res_name && nameptr < (name + sizeof(name) - 1); res_name ++)
    if (!_cups_strcasecmp(res_name, ext))
      break;
    else if (isalnum(*res_name & 255) || *res_name == '-')
      *nameptr++ = (char)tolower(*res_name & 255);
    else
      *nameptr++ = '_';

  *nameptr = '\0';

  snprintf(fname, fnamesize, "%s/%d-%s%s", prefix, res->id, name, ext);
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
    const char *language)		/* I - Resource language or `NULL` */
{
  server_listener_t *lis = (server_listener_t *)cupsArrayFirst(Listeners);
					/* First listener */
  server_resource_t	*res;		/* Resource */
  char			uuid[64];	/* resource-uuid value */
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

  res->fd    = -1;
  res->attrs = ippNew();
  res->id    = NextResourceId ++;
  res->state = filename ? IPP_RSTATE_INSTALLED : IPP_RSTATE_PENDING;
  res->type  = strdup(type);

  if (resource)
    res->resource = strdup(resource);

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

  if (language)
    ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_LANGUAGE, "resource-natural-language", NULL, language);

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_TEXT, "resource-state-message", NULL, "");

  ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "resource-string-version");

  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_KEYWORD, "resource-type", NULL, type);

  httpAssembleUUID(lis->host, lis->port, "_system_", res->id, uuid, sizeof(uuid));
  ippAddString(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_URI, "resource-uuid", NULL, uuid);

  ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "resource-version");

  ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "time-at-creation", (int)(curtime - SystemStartTime));

  if (res->state == IPP_RSTATE_INSTALLED)
    ippAddInteger(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_INTEGER, "time-at-installed", (int)(curtime - SystemStartTime));
  else
    ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "time-at-installed");

  ippAddOutOfBand(res->attrs, IPP_TAG_RESOURCE, IPP_TAG_NOVALUE, "time-at-canceled");

  if (!ResourcesByFilename)
    ResourcesByFilename = cupsArrayNew((cups_array_func_t)compare_filenames, NULL);
  if (!ResourcesById)
    ResourcesById = cupsArrayNew((cups_array_func_t)compare_ids, NULL);
  if (!ResourcesByPath)
    ResourcesByPath = cupsArrayNew((cups_array_func_t)compare_resources, NULL);

  cupsArrayAdd(ResourcesById, res);
  if (res->resource)
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

  if (res->filename)
    cupsArrayRemove(ResourcesByFilename, res);
  cupsArrayRemove(ResourcesById, res);
  cupsArrayRemove(ResourcesByPath, res);

  _cupsRWLockWrite(&res->rwlock);

  ippDelete(res->attrs);

  free(res->filename);
  free(res->format);
  free(res->resource);
  free(res->type);

  _cupsRWUnlock(&res->rwlock);
  _cupsRWDeinit(&res->rwlock);

  free(res);

  _cupsRWUnlock(&ResourcesRWLock);
}


/*
 * 'serverFindResourceByFilename()' - Find a resource by its local filename.
 */

server_resource_t *			/* O - Resource */
serverFindResourceByFilename(
    const char *filename)		/* I - Resource filename */
{
  server_resource_t	key,		/* Search key */
			*res;		/* Matching resource */


  key.filename = (char *)filename;

  _cupsRWLockRead(&ResourcesRWLock);
  res = (server_resource_t *)cupsArrayFind(ResourcesByFilename, &key);
  _cupsRWUnlock(&ResourcesRWLock);

  return (res);
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
 * 'serverFindResourceByPath()' - Find a resource by its remote path.
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
 * 'serverSetResourceState()' - Set the state of a resource.
 */

void
serverSetResourceState(
    server_resource_t *resource,	/* I - Resource */
    ipp_rstate_t      state,		/* I - New state */
    const char        *message,		/* I - Printf-style message or `NULL` */
    ...)				/* I - Additional arguments as needed */
{
  ipp_attribute_t	*attr;		/* Resource attribute */


  _cupsRWLockWrite(&resource->rwlock);

  resource->state = state;

  if (state == IPP_RSTATE_INSTALLED)
  {
    if ((attr = ippFindAttribute(resource->attrs, "date-time-at-installed", IPP_TAG_NOVALUE)) != NULL)
      ippSetDate(resource->attrs, &attr, 0, ippTimeToDate(time(NULL)));

    if ((attr = ippFindAttribute(resource->attrs, "time-at-installed", IPP_TAG_NOVALUE)) != NULL)
      ippSetInteger(resource->attrs, &attr, 0, (int)(time(NULL) - SystemStartTime));
  }
  else if (state >= IPP_RSTATE_CANCELED)
  {
    resource->cancel = 0;

    if ((attr = ippFindAttribute(resource->attrs, "date-time-at-canceled", IPP_TAG_NOVALUE)) != NULL)
      ippSetDate(resource->attrs, &attr, 0, ippTimeToDate(time(NULL)));

    if ((attr = ippFindAttribute(resource->attrs, "time-at-canceled", IPP_TAG_NOVALUE)) != NULL)
      ippSetInteger(resource->attrs, &attr, 0, (int)(time(NULL) - SystemStartTime));
  }

  if (message && (attr = ippFindAttribute(resource->attrs, "resource-state-message", IPP_TAG_TEXT)) != NULL)
  {
    va_list	ap;			/* Argument pointer */
    char	buffer[1024];		/* Message String */

    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    ippSetString(resource->attrs, &attr, 0, buffer);
  }

  _cupsRWUnlock(&resource->rwlock);
}


/*
 * 'compare_filenames()' - Compare two resource filenames.
 */

static int				/* O - Result of comparison */
compare_filenames(
    server_resource_t *a,		/* I - First resource */
    server_resource_t *b)		/* I - Second resource */
{
  return (strcmp(a->filename, b->filename));
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

/*
 * Device support for sample IPP server implementation.
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
 * 'serverCreateDevice()' - Create an output device tracking object.
 */

server_device_t *			/* O - Device */
serverCreateDevice(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  ipp_attribute_t	*uuid;		/* output-device-uuid */


  if ((uuid = ippFindAttribute(client->request, "output-device-uuid", IPP_TAG_URI)) == NULL)
    return (NULL);

  if ((device = calloc(1, sizeof(server_device_t))) == NULL)
    return (NULL);

  _cupsRWInit(&device->rwlock);

  device->uuid  = strdup(ippGetString(uuid, 0, NULL));
  device->state = IPP_PSTATE_STOPPED;

  _cupsRWLockWrite(&client->printer->rwlock);
  cupsArrayAdd(client->printer->devices, device);
  _cupsRWUnlock(&client->printer->rwlock);

  return (device);
}


/*
 * 'serverDeleteDevice()' - Remove a device from a printer.
 *
 * Note: Caller is responsible for locking the printer object.
 */

void
serverDeleteDevice(server_device_t *device)	/* I - Device */
{
 /*
  * Free memory used for the device...
  */

  _cupsRWDeinit(&device->rwlock);

  if (device->name)
    free(device->name);

  free(device->uuid);

  ippDelete(device->attrs);

  free(device);
}

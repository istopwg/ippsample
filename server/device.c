/*
 * Device support for sample IPP server implementation.
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

static int	compare_devices(server_device_t *a, server_device_t *b);


/*
 * 'serverCreateDevice()' - Create an output device tracking object.
 */

server_device_t *			/* O - Device */
serverCreateDevice(
    server_client_t *client)		/* I - Client */
{
  server_device_t	*device;	/* Device */
  ipp_attribute_t	*uuid;		/* output-device-uuid */


  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverCreateDevice: Finding output-device-uuid.");

  if ((uuid = ippFindAttribute(client->request, "output-device-uuid", IPP_TAG_URI)) == NULL)
    return (NULL);

  _cupsRWLockWrite(&client->printer->rwlock);
  device = serverCreateDevicePinfo(&client->printer->pinfo, ippGetString(uuid, 0, NULL));
  _cupsRWUnlock(&client->printer->rwlock);

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverCreateDevice: Created device object for \"%s\".", device->uuid);

  return (device);
}


/*
 * 'serverCreateDevicePinfo()' - Create a device in a printer-info structure.
 */

server_device_t	*			/* O - Device */
serverCreateDevicePinfo(
    server_pinfo_t *pinfo,		/* I - Printer-info structure */
    const char     *uuid)		/* I - UUID string */
{
  server_device_t	*device;	/* Device */


  if ((device = calloc(1, sizeof(server_device_t))) == NULL)
    return (NULL);

  _cupsRWInit(&device->rwlock);

  device->uuid  = strdup(uuid);
  device->state = IPP_PSTATE_STOPPED;
  device->attrs = ippNew();

  if (!pinfo->devices)
    pinfo->devices = cupsArrayNew((cups_array_func_t)compare_devices, NULL);

  cupsArrayAdd(pinfo->devices, device);

  return (device);
}


/*
 * 'serverDeleteDevice()' - Remove a device from a printer.
 *
 * Note: Caller is responsible for locking the printer object.
 */

void
serverDeleteDevice(
    server_device_t *device)		/* I - Device */
{
 /*
  * Free memory used for the device...
  */

  serverLog(SERVER_LOGLEVEL_DEBUG, "Deleting device object for \"%s\".", device->uuid);

  _cupsRWDeinit(&device->rwlock);

  if (device->name)
    free(device->name);

  free(device->uuid);

  ippDelete(device->attrs);

  free(device);
}


/*
 * 'serverFindDevice()' - Find a device.
 */

server_device_t *			/* I - Device */
serverFindDevice(
    server_client_t *client)		/* I - Client */
{
  ipp_attribute_t	*uuid;		/* output-device-uuid */
  server_device_t	key,		/* Search key */
			*device;	/* Matching device */


  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverFindDevice: Looking for output-device-uuid.");

  if ((uuid = ippFindAttribute(client->request, "output-device-uuid", IPP_TAG_URI)) == NULL)
    return (NULL);

  key.uuid = (char *)ippGetString(uuid, 0, NULL);

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverFindDevice: Looking for \"%s\".", key.uuid);

  _cupsRWLockRead(&client->printer->rwlock);
  device = (server_device_t *)cupsArrayFind(client->printer->pinfo.devices, &key);
  _cupsRWUnlock(&client->printer->rwlock);

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverFindDevice: Returning device=%p", (void *)device);

  return (device);
}


/*
 * 'serverUpdateDeviceAttributesNoLock()' - Update the composite device attributes.
 *
 * Note: Caller MUST lock the printer object for writing before using.
 */

void
serverUpdateDeviceAttributesNoLock(
    server_printer_t *printer)		/* I - Printer */
{
  server_device_t	*device;	/* Current device */
  ipp_t			*dev_attrs;	/* Device attributes */


 /* TODO: Support multiple output devices, icons, etc... (Issue #89) */
  device    = (server_device_t *)cupsArrayFirst(printer->pinfo.devices);
  dev_attrs = ippNew();

  if (device)
    serverCopyAttributes(dev_attrs, device->attrs, NULL, NULL, IPP_TAG_PRINTER, 0);

  ippDelete(printer->dev_attrs);
  printer->dev_attrs   = dev_attrs;
  printer->config_time = time(NULL);
}


/*
 * 'serverUpdateDeviceStatusNoLock()' - Update the composite device state.
 *
 * Note: Caller MUST lock the printer object for writing before using.
 */

void
serverUpdateDeviceStateNoLock(
    server_printer_t *printer)		/* I - Printer */
{
  server_device_t	*device;	/* Current device */
  ipp_attribute_t	*attr;		/* Current attribute */


 /* TODO: Support multiple output devices, icons, etc... (Issue #89) */
  device = (server_device_t *)cupsArrayFirst(printer->pinfo.devices);

  if (device && (attr = ippFindAttribute(device->attrs, "printer-state", IPP_TAG_ENUM)) != NULL)
    printer->dev_state = (ipp_pstate_t)ippGetInteger(attr, 0);
  else
    printer->dev_state = IPP_PSTATE_STOPPED;

  if (device && (attr = ippFindAttribute(device->attrs, "printer-state-reasons", IPP_TAG_KEYWORD)) != NULL)
    printer->dev_reasons = serverGetPrinterStateReasonsBits(attr);
  else
    printer->dev_reasons = SERVER_PREASON_PAUSED;

  printer->state_time = time(NULL);
}


/*
 * 'compare_devices()' - Compare two devices...
 */

static int				/* O - Result of comparison */
compare_devices(server_device_t *a,	/* I - First device */
                server_device_t *b)	/* I - Second device */
{
  return (strcmp(a->uuid, b->uuid));
}

/*
 * Client code for sample IPP server implementation.
 *
 * Copyright © 2014-2023 by the Printer Working Group
 * Copyright © 2010-2019 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "ippserver.h"
#include "printer-png.h"
#include "printer3d-png.h"


/*
 * Local functions...
 */

static void		html_escape(server_client_t *client, const char *s, size_t slen);
static void		html_footer(server_client_t *client);
static void		html_header(server_client_t *client, const char *title, int refresh);
static void		html_printf(server_client_t *client, const char *format, ...) _CUPS_FORMAT(2, 3);
static size_t		parse_options(server_client_t *client, cups_option_t **options);
static int		send_mobile_config(server_client_t *client, server_printer_t *printer);
static void		send_printer_payload(server_client_t *client, server_printer_t *printer);
static int		show_materials(server_client_t *client, server_printer_t *printer, const char *encoding);
static int		show_media(server_client_t *client, server_printer_t *printer, const char *encoding);
static int		show_status(server_client_t *client, server_printer_t *printer, const char *encoding);
static int		show_supplies(server_client_t *client, server_printer_t *printer, const char *encoding);


/*
 * 'serverCreateClient()' - Accept a new network connection and create a client object.
 */

server_client_t *			/* O - Client */
serverCreateClient(int sock)		/* I - Listen socket */
{
  server_client_t	*client;	/* Client */
  static int		next_client_number = 1;
					/* Next client number */


  if ((client = calloc(1, sizeof(server_client_t))) == NULL)
  {
    perror("Unable to allocate memory for client");
    return (NULL);
  }

  client->number     = next_client_number ++;
  client->fetch_file = -1;

 /*
  * Accept the client and get the remote address...
  */

  if ((client->http = httpAcceptConnection(sock, 1)) == NULL)
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unable to accept client connection: %s", cupsGetErrorString());

    free(client);

    return (NULL);
  }

  httpGetHostname(client->http, client->hostname, sizeof(client->hostname));

  serverLogClient(SERVER_LOGLEVEL_INFO, client, "Accepted connection from \"%s\".", client->hostname);

  return (client);
}


/*
 * 'serverCreateListener()' - Create a listener socket.
 */

int					/* O - 1 on success, 0 on error */
serverCreateListeners(const char *host,	/* I - Hostname, IP address, or NULL for any address */
                      int        port)	/* I - Port number */
{
  int			count = 0;	/* Number of sockets */
  int			sock;		/* Listener socket */
  http_addrlist_t	*addrlist,	/* Listen address(es) */
			*addr;		/* Current address */
  char			service[32],	/* Service port */
			local[256];	/* Local hostname */
  server_listener_t	*lis;		/* New listener */


  if (host && !strcmp(host, "*"))
    host = NULL;

  snprintf(service, sizeof(service), "%d", port);
  if ((addrlist = httpAddrGetList(host, AF_UNSPEC, service)) == NULL)
  {
    serverLog(SERVER_LOGLEVEL_ERROR, "Unable to resolve Listen address \"%s\": %s", host ? host : "*", cupsGetErrorString());
    return (0);
  }

  if (!host)
  {
    httpGetHostname(NULL, local, sizeof(local));
    host = local;
  }

  for (addr = addrlist; addr; addr = addr->next)
  {
    if ((sock = httpAddrListen(&(addr->addr), port)) < 0)
      continue;

    if ((lis = calloc(1, sizeof(server_listener_t))) == NULL)
    {
      httpAddrClose(&addr->addr, sock);
      break;
    }

    lis->fd = sock;
    cupsCopyString(lis->host, host, sizeof(lis->host));
    lis->port = port;

    if (!Listeners)
      Listeners = cupsArrayNew(NULL, NULL, NULL, 0, NULL, NULL);

    cupsArrayAdd(Listeners, lis);
    count ++;
  }

  httpAddrFreeList(addrlist);

  return (count > 0);
}


/*
 * 'serverDeleteClient()' - Close the socket and free all memory used by a client object.
 */

void
serverDeleteClient(server_client_t *client)	/* I - Client */
{
  serverLogClient(SERVER_LOGLEVEL_INFO, client, "Closing connection from \"%s\".", client->hostname);

 /*
  * Flush pending writes before closing...
  */

  httpFlushWrite(client->http);

 /*
  * Free memory...
  */

  httpClose(client->http);

  ippDelete(client->request);
  ippDelete(client->response);

  free(client);
}


/*
 * 'serverProcessClient()' - Process client requests on a thread.
 */

void *					/* O - Exit status */
serverProcessClient(
    server_client_t *client)		/* I - Client */
{
 /*
  * Loop until we are out of requests or timeout (30 seconds)...
  */

  int first_time = 1;			/* First time request? */

  while (httpWait(client->http, 30000))
  {
    if (first_time && Encryption != HTTP_ENCRYPTION_NEVER)
    {
     /*
      * See if we need to negotiate a TLS connection...
      */

      char buf[1];			/* First byte from client */

      if (Encryption == HTTP_ENCRYPTION_ALWAYS ||
          (recv(httpGetFd(client->http), buf, 1, MSG_PEEK) == 1 && (!buf[0] || !strchr("DGHOPT", buf[0]))))
      {
        serverLogClient(SERVER_LOGLEVEL_INFO, client, "Starting HTTPS session.");

	if (!httpSetEncryption(client->http, HTTP_ENCRYPTION_ALWAYS))
	{
	  serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unable to encrypt connection: %s", cupsGetErrorString());
	  break;
        }

        serverLogClient(SERVER_LOGLEVEL_INFO, client, "Connection now encrypted.");
      }

      first_time = 0;
    }

    if (!serverProcessHTTP(client))
      break;
  }

 /*
  * Close the conection to the client and return...
  */

  serverDeleteClient(client);

  return (NULL);
}


/*
 * 'serverProcessHTTP()' - Process a HTTP request.
 */

int					/* O - 1 on success, 0 on failure */
serverProcessHTTP(
    server_client_t *client)		/* I - Client connection */
{
  char			uri[1024],	/* URI */
			*uriptr;	/* Pointer into URI */
  http_state_t		http_state;	/* HTTP state */
  http_status_t		http_status;	/* HTTP status */
  ipp_state_t		ipp_state;	/* State of IPP transfer */
  char			scheme[32],	/* Method/scheme */
			userpass[128],	/* Username:password */
			hostname[HTTP_MAX_HOST],
					/* Hostname */
			*ptr;		/* Pointer into Host: header */
  int			port;		/* Port number */
  const char		*authorization;	/* Authorization value */
  const char		*encoding;	/* Content-Encoding value */
  server_resource_t	*res;		/* Resource */


 /*
  * Clear state variables...
  */

  ippDelete(client->request);
  ippDelete(client->response);

  client->request   = NULL;
  client->response  = NULL;
  client->operation = HTTP_STATE_WAITING;

 /*
  * Read a request from the connection...
  */

  while ((http_state = httpReadRequest(client->http, uri, sizeof(uri))) == HTTP_STATE_WAITING)
    usleep(1);

 /*
  * Parse the request line...
  */

  if (http_state == HTTP_STATE_ERROR)
  {
    if (httpGetError(client->http) == EPIPE || httpGetError(client->http) == ETIMEDOUT || httpGetError(client->http) == 0)
      serverLogClient(SERVER_LOGLEVEL_INFO, client, "Client closed connection.");
    else
      serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Bad request line (%s).", strerror(httpGetError(client->http)));

    return (0);
  }
  else if (http_state == HTTP_STATE_UNKNOWN_METHOD)
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Bad/unknown operation.");
    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }
  else if (http_state == HTTP_STATE_UNKNOWN_VERSION)
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Bad HTTP version.");
    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  serverLogClient(SERVER_LOGLEVEL_INFO, client, "%s %s", httpStateString(http_state), uri);

 /*
  * Separate the URI into its components...
  */

  if (httpSeparateURI(HTTP_URI_CODING_MOST, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), hostname, sizeof(hostname), &port, client->uri, sizeof(client->uri)) < HTTP_URI_STATUS_OK &&
      (http_state != HTTP_STATE_OPTIONS || strcmp(uri, "*")))
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Bad URI \"%s\".", uri);
    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  if ((client->options = strchr(client->uri, '?')) != NULL)
    *(client->options)++ = '\0';

 /*
  * Process the request...
  */

  client->start     = time(NULL);
  client->operation = httpGetState(client->http);

 /*
  * Parse incoming parameters until the status changes...
  */

  while ((http_status = httpUpdate(client->http)) == HTTP_STATUS_CONTINUE);

  if (http_status != HTTP_STATUS_OK)
  {
    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  if (!httpGetField(client->http, HTTP_FIELD_HOST)[0] &&
      httpGetVersion(client->http) >= HTTP_VERSION_1_1)
  {
   /*
    * HTTP/1.1 and higher require the "Host:" field...
    */

    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
    return (0);
  }

  cupsCopyString(client->host_field, httpGetField(client->http, HTTP_FIELD_HOST), sizeof(client->host_field));
  if ((ptr = strrchr(client->host_field, ':')) != NULL)
  {
    char *end;				// End of port number

    // Grab port number from Host: header...
    *ptr++ = '\0';
    client->host_port = (int)strtol(ptr, &end, 10);

    if (errno == ERANGE || *end)
      client->host_port = DefaultPort;
  }
  else
  {
    // Use the default port number...
    client->host_port = DefaultPort;
  }

 /*
  * Handle HTTP Upgrade...
  */

  if (!strcasecmp(httpGetField(client->http, HTTP_FIELD_CONNECTION), "Upgrade"))
  {
    if (strstr(httpGetField(client->http, HTTP_FIELD_UPGRADE), "TLS/") != NULL && !httpIsEncrypted(client->http) && Encryption != HTTP_ENCRYPTION_NEVER)
    {
      if (!serverRespondHTTP(client, HTTP_STATUS_SWITCHING_PROTOCOLS, NULL, NULL, 0))
        return (0);

      serverLogClient(SERVER_LOGLEVEL_INFO, client, "Upgrading to encrypted connection.");

      if (!httpSetEncryption(client->http, HTTP_ENCRYPTION_REQUIRED))
      {
        serverLogClient(SERVER_LOGLEVEL_ERROR, client,
        "Unable to encrypt connection: %s", cupsGetErrorString());
	return (0);
      }

      serverLogClient(SERVER_LOGLEVEL_INFO, client, "Connection now encrypted.");
    }
    else if (!serverRespondHTTP(client, HTTP_STATUS_NOT_IMPLEMENTED, NULL, NULL, 0))
      return (0);
  }

  if (Encryption == HTTP_ENCRYPTION_REQUIRED && !httpIsEncrypted(client->http))
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Forcing encryption of connection.");
    serverRespondHTTP(client, HTTP_STATUS_UPGRADE_REQUIRED, NULL, NULL, 0);
    return (0);
  }

 /*
  * Handle HTTP Expect...
  */

  if (httpGetExpect(client->http) && (client->operation == HTTP_STATE_POST || client->operation == HTTP_STATE_PUT))
  {
    if (httpGetExpect(client->http) == HTTP_STATUS_CONTINUE)
    {
     /*
      * Send 100-continue header...
      */

      if (!serverRespondHTTP(client, HTTP_STATUS_CONTINUE, NULL, NULL, 0))
	return (0);
    }
    else
    {
     /*
      * Send 417-expectation-failed header...
      */

      if (!serverRespondHTTP(client, HTTP_STATUS_EXPECTATION_FAILED, NULL, NULL, 0))
	return (0);
    }
  }

 /*
  * Handle Authorization...
  */

  authorization = httpGetField(client->http, HTTP_FIELD_AUTHORIZATION);

  if (Authentication && *authorization && (http_status = serverAuthenticateClient(client)) != HTTP_STATUS_CONTINUE)
  {
    serverRespondHTTP(client, http_status, NULL, NULL, 0);
    return (0);
  }

 /*
  * Handle new transfers...
  */

  encoding = httpGetContentEncoding(client->http);

  switch (client->operation)
  {
    case HTTP_STATE_OPTIONS :
       /*
	* Do OPTIONS command...
	*/

	return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, NULL, 0));

    case HTTP_STATE_HEAD :
        if (!strncmp(client->uri, "/ipp/print/", 11))
        {
          if ((uriptr = strchr(client->uri + 11, '/')) != NULL)
	    *uriptr++ = '\0';
	  else
	    uriptr = client->uri + strlen(client->uri);
        }
        else if (!strncmp(client->uri, "/ipp/print3d/", 13))
        {
          if ((uriptr = strchr(client->uri + 13, '/')) != NULL)
	    *uriptr++ = '\0';
	  else
	    uriptr = client->uri + strlen(client->uri);
        }
        else if (!strcmp(client->uri, "/ipp/print"))
          uriptr = client->uri + strlen(client->uri);
        else
          uriptr = NULL;

        if (uriptr)
        {
	  server_printer_t *printer;	/* Printer */

          if ((printer = serverFindPrinter(client->uri)) == NULL)
            printer = (server_printer_t *)cupsArrayGetFirst(Printers);

          if (printer)
          {
            if (!strcmp(uriptr, "apple.mobileconfig"))
              return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "application/x-apple-aspen-config", 0));
            else if (!strcmp(uriptr, "icon.png"))
              return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "image/png", 0));
            else if (!*uriptr || !strcmp(uriptr, "materials") || !strcmp(uriptr, "media") || !strcmp(uriptr, "supplies"))
              return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "text/html", 0));
          }
        }
        else if (!strcmp(client->uri, "/ipp/system/apple.mobileconfig"))
	  return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "application/x-apple-aspen-config", 0));
        else if ((res = serverFindResourceByPath(client->uri)) != NULL && res->state == IPP_RSTATE_INSTALLED)
          return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, res->format, 0));
	else if (!strcmp(client->uri, "/"))
	  return (serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "text/html", 0));

        return (serverRespondHTTP(client, HTTP_STATUS_NOT_FOUND, NULL, NULL, 0));

    case HTTP_STATE_GET :
        if (!strncmp(client->uri, "/ipp/print/", 11))
        {
          if ((uriptr = strchr(client->uri + 11, '/')) != NULL)
	    *uriptr++ = '\0';
	  else
	    uriptr = client->uri + strlen(client->uri);
        }
        else if (!strncmp(client->uri, "/ipp/print3d/", 13))
        {
          if ((uriptr = strchr(client->uri + 13, '/')) != NULL)
	    *uriptr++ = '\0';
	  else
	    uriptr = client->uri + strlen(client->uri);
        }
        else if (!strcmp(client->uri, "/ipp/print"))
          uriptr = client->uri + strlen(client->uri);
        else
          uriptr = NULL;

        if (uriptr)
        {
	  server_printer_t *printer;	/* Printer */

          if ((printer = serverFindPrinter(client->uri)) == NULL)
            printer = (server_printer_t *)cupsArrayGetFirst(Printers);

          if (printer)
          {
            if (!strcmp(uriptr, "apple.mobileconfig"))
            {
              return (send_mobile_config(client, printer));
            }
            else if (!strcmp(uriptr, "icon.png"))
            {
             /*
              * Send PNG icon file.
              */

              int		fd;		/* Icon file */
              struct stat	fileinfo;	/* Icon file information */
              char		buffer[4096];	/* Copy buffer */
              ssize_t		bytes;		/* Bytes */

              if (printer->icon_resource)
              {
                serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Icon file is \"%s\".", printer->icon_resource->filename);

                if (!stat(printer->icon_resource->filename, &fileinfo) && (fd = open(printer->icon_resource->filename, O_RDONLY | O_BINARY)) >= 0)
                {
                  if (!serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "image/png", (size_t)fileinfo.st_size))
                  {
                    close(fd);
                    return (0);
                  }

                  while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
                    httpWrite(client->http, buffer, (size_t)bytes);

                  httpFlushWrite(client->http);

                  close(fd);
                  return (1);
                }
              }
              else if (printer)
              {
                serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Icon file is internal.");

                if (!strncmp(printer->resource, "/ipp/print3d", 12))
                {
                  if (!serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "image/png", sizeof(printer3d_png)))
                    return (0);

                  httpWrite(client->http, (void *)printer3d_png, sizeof(printer3d_png));
                }
                else
                {
                  if (!serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "image/png", sizeof(printer_png)))
                    return (0);

                  httpWrite(client->http, (void *)printer_png, sizeof(printer_png));
                }
                httpFlushWrite(client->http);

                return (1);
              }
            }
            else if (!*uriptr)
            {
              return (show_status(client, printer, encoding));
            }
            else if (!strcmp(uriptr, "materials"))
            {
              return (show_materials(client, printer, encoding));
            }
            else if (!strcmp(uriptr, "media"))
            {
              return (show_media(client, printer, encoding));
            }
            else if (!strcmp(uriptr, "supplies"))
            {
              return (show_supplies(client, printer, encoding));
            }
          }
	}
	else if (!strcmp(client->uri, "/ipp/system/apple.mobileconfig"))
	{
	  return (send_mobile_config(client, NULL));
	}
        else if ((res = serverFindResourceByPath(client->uri)) != NULL && res->state == IPP_RSTATE_INSTALLED)
        {
	  int		fd;		/* Icon file */
	  struct stat	fileinfo;	/* Icon file information */
	  char		buffer[4096];	/* Copy buffer */
	  ssize_t	bytes;		/* Bytes */

	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "Resource \"%s\" maps to \"%s\".", res->resource, res->filename);

	  if ((fd = open(res->filename, O_RDONLY | O_BINARY)) >= 0)
	  {
	    if (fstat(fd, &fileinfo))
	    {
	      serverRespondHTTP(client, HTTP_STATUS_SERVER_ERROR, NULL, NULL, 0);
	      close(fd);
	      return (0);
	    }
	    else if (!serverRespondHTTP(client, HTTP_STATUS_OK, NULL, res->format, (size_t)fileinfo.st_size))
	    {
	      close(fd);
	      return (0);
	    }

	    while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
	      httpWrite(client->http, buffer, (size_t)bytes);

	    httpFlushWrite(client->http);

	    close(fd);
	    return (1);
	  }
	}
	else if (!strcmp(client->uri, "/"))
	{
          return (show_status(client, NULL, encoding));
	}

        return (serverRespondHTTP(client, HTTP_STATUS_NOT_FOUND, NULL, NULL, 0));

    case HTTP_STATE_POST :
	if (strcmp(httpGetField(client->http, HTTP_FIELD_CONTENT_TYPE), "application/ipp"))
        {
	 /*
	  * Not an IPP request...
	  */

	  return (serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0));
	}

       /*
        * Read the IPP request...
	*/

	client->request = ippNew();

        while ((ipp_state = ippRead(client->http,
                                    client->request)) != IPP_STATE_DATA)
	{
	  if (ipp_state == IPP_STATE_ERROR)
	  {
            serverLogClient(SERVER_LOGLEVEL_ERROR, client, "IPP read error (%s).", cupsGetErrorString());
	    serverRespondHTTP(client, HTTP_STATUS_BAD_REQUEST, NULL, NULL, 0);
	    return (0);
	  }
	}

       /*
        * Now that we have the IPP request, process the request...
	*/

        return (serverProcessIPP(client));

    default :
        break; /* Anti-compiler-warning-code */
  }

  return (1);
}


/*
 * 'serverRespondHTTP()' - Send a HTTP response.
 */

int					/* O - 1 on success, 0 on failure */
serverRespondHTTP(
    server_client_t *client,		/* I - Client */
    http_status_t   code,		/* I - HTTP status of response */
    const char      *content_encoding,	/* I - Content-Encoding of response */
    const char      *type,		/* I - MIME media type of response */
    size_t          length)		/* I - Length of response */
{
  char	message[1024];			/* Text message */


  serverLogClient(SERVER_LOGLEVEL_INFO, client, "%s", httpStatusString(code));

  if (code == HTTP_STATUS_CONTINUE)
  {
   /*
    * 100-continue doesn't send any headers...
    */

    return (httpWriteResponse(client->http, HTTP_STATUS_CONTINUE));
  }

 /*
  * Format an error message...
  */

  if (!type && !length && code != HTTP_STATUS_OK && code != HTTP_STATUS_SWITCHING_PROTOCOLS)
  {
    snprintf(message, sizeof(message), "%d - %s\n", code, httpStatusString(code));

    type   = "text/plain";
    length = strlen(message);
  }
  else
    message[0] = '\0';

 /*
  * Send the HTTP response header...
  */

  httpClearFields(client->http);

  if (code == HTTP_STATUS_UNAUTHORIZED || code == HTTP_STATUS_FORBIDDEN)
  {
    char www_auth[256];		/* WWW-Authenicate header value */

    if (AuthType && !strcasecmp(AuthType, "Basic"))
      snprintf(www_auth, sizeof(www_auth), "Basic realm=\"%s\" charset=\"UTF-8\"", AuthName);
    else
      www_auth[0] = '\0';

    httpSetField(client->http, HTTP_FIELD_WWW_AUTHENTICATE, www_auth);
  }

  if (type)
  {
    if (!strcmp(type, "text/html"))
      httpSetField(client->http, HTTP_FIELD_CONTENT_TYPE, "text/html; charset=utf-8");
    else
      httpSetField(client->http, HTTP_FIELD_CONTENT_TYPE, type);

    if (content_encoding)
      httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, content_encoding);
  }

  httpSetLength(client->http, length);

  if (!httpWriteResponse(client->http, code))
    return (0);

 /*
  * Send the response data...
  */

  if (message[0])
  {
   /*
    * Send a plain text message.
    */

    if (httpPrintf(client->http, "%s", message) < 0)
      return (0);
  }
  else if (client->response)
  {
   /*
    * Send an IPP response...
    */

    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Sending %d bytes of IPP response (Content-Length=%d)", (int)ippGetLength(client->response), (int)length);

    ippSetState(client->response, IPP_STATE_IDLE);

    if (ippWrite(client->http, client->response) != IPP_STATE_DATA)
    {
      serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unable to write IPP response.");
      return (0);
    }

    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Sent IPP response.");

    if (client->fetch_file >= 0)
    {
      ssize_t	bytes;			/* Bytes read */
      char	buffer[32768];		/* Buffer */

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Sending file.");

      if (client->fetch_compression)
        httpSetField(client->http, HTTP_FIELD_CONTENT_ENCODING, "gzip");

      while ((bytes = read(client->fetch_file, buffer, sizeof(buffer))) > 0)
        httpWrite(client->http, buffer, (size_t)bytes);

      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Sent file.");

      close(client->fetch_file);
      client->fetch_file = -1;
    }

    if (length == 0)
    {
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Sending 0-length chunk.");
      httpWrite(client->http, "", 0);
    }
  }

  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "serverRespondHTTP: Flushing write buffer.");
  httpFlushWrite(client->http);

  return (1);
}


/*
 * 'serverRun()' - Run the server.
 */

void
serverRun(void)
{
  int			max_fd;		/* Number of file descriptors */
  fd_set		input;		/* select() input set */
  struct timeval	timeout;	/* Timeout for poll() */
  server_listener_t	*lis;		/* Listener */
  server_client_t	*client;	/* New client */
  time_t                next_clean = 0; /* Next time to clean old jobs */


  serverLog(SERVER_LOGLEVEL_DEBUG, "serverRun: %u printers configured.", (unsigned)cupsArrayGetCount(Printers));
  serverLog(SERVER_LOGLEVEL_DEBUG, "serverRun: %u listeners configured.", (unsigned)cupsArrayGetCount(Listeners));

 /*
  * Loop until we are killed or have a hard error...
  */

  for (;;)
  {
   /*
    * Setup select() data for the Bonjour service socket and listeners...
    */

    FD_ZERO(&input);
    max_fd = 0;

    for (lis = (server_listener_t *)cupsArrayGetFirst(Listeners); lis; lis = (server_listener_t *)cupsArrayGetNext(Listeners))
    {
      FD_SET(lis->fd, &input);
      if (max_fd < lis->fd)
        max_fd = lis->fd;
    }

    timeout.tv_sec  = DNSSDUpdate ? 1 : 10;
    timeout.tv_usec = 0;

    if (select(max_fd + 1, &input, NULL, NULL, &timeout) < 0 && errno != EINTR)
    {
      serverLog(SERVER_LOGLEVEL_ERROR, "Main loop failed (%s)", strerror(errno));
      break;
    }

    for (lis = (server_listener_t *)cupsArrayGetFirst(Listeners); lis; lis = (server_listener_t *)cupsArrayGetNext(Listeners))
    {
      if (FD_ISSET(lis->fd, &input))
      {
        serverLog(SERVER_LOGLEVEL_DEBUG, "serverRun: Incoming connection on listener %s:%d.", lis->host, lis->port);

        if ((client = serverCreateClient(lis->fd)) != NULL)
        {
          cups_thread_t t = cupsThreadCreate((cups_thread_func_t)serverProcessClient, client);

          if (t)
          {
            cupsThreadDetach(t);
          }
          else
          {
            serverLog(SERVER_LOGLEVEL_ERROR, "Unable to create client thread (%s)", strerror(errno));
            serverDeleteClient(client);
          }
        }
      }
    }

    if (DNSSDUpdate)
    {
      size_t		i,		/* Looping var */
			count;		/* Number of printers */
      server_printer_t	*printer;	/* Current printer */

      DNSSDUpdate = false;

      cupsRWLockRead(&PrintersRWLock);

      for (i = 0, count = cupsArrayGetCount(Printers); i < count; i ++)
      {
        printer = (server_printer_t *)cupsArrayGetElement(Printers, i);

        if (printer->dns_sd_collision || printer->dns_sd_update)
          serverRegisterPrinter(printer);
      }

      cupsRWUnlock(&PrintersRWLock);
    }

    if (time(NULL) >= next_clean)
    {
      serverCleanAllJobs();

      next_clean = time(NULL) + 30;
    }
  }
}


/*
 * 'html_escape()' - Write a HTML-safe string.
 */

static void
html_escape(server_client_t *client,	/* I - Client */
	    const char    *s,		/* I - String to write */
	    size_t        slen)		/* I - Number of characters to write */
{
  const char	*start,			/* Start of segment */
		*end;			/* End of string */


  start = s;
  end   = s + (slen > 0 ? slen : strlen(s));

  while (*s && s < end)
  {
    if (*s == '&' || *s == '<')
    {
      if (s > start)
        httpWrite(client->http, start, (size_t)(s - start));

      if (*s == '&')
        httpWrite(client->http, "&amp;", 5);
      else
        httpWrite(client->http, "&lt;", 4);

      start = s + 1;
    }

    s ++;
  }

  if (s > start)
    httpWrite(client->http, start, (size_t)(s - start));
}


/*
 * 'html_footer()' - Show the web interface footer.
 *
 * This function also writes the trailing 0-length chunk.
 */

static void
html_footer(server_client_t *client)	/* I - Client */
{
  html_printf(client,
	      "</div>\n"
	      "<div class=\"footer\">Copyright &copy; 2014-2023 by the Printer Working Group.<br>\n"
	      "ippserver is part of the <a href=\"https://github.com/istopwg/ippsample\" target=\"_blank\">ippsample</a> project and is provided on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. It is <em>not</em> intended for production use.</div>\n"
	      "</body>\n"
	      "</html>\n");
  httpWrite(client->http, "", 0);
}


/*
 * 'html_header()' - Show the web interface header and title.
 */

static void
html_header(server_client_t *client,	/* I - Client */
            const char      *title,	/* I - Title */
            int             refresh)	/* I - Refresh timer, if any */
{
  html_printf(client,
	      "<!doctype html>\n"
	      "<html>\n"
	      "<head>\n"
	      "<title>%s</title>\n"
	      "<link rel=\"shortcut icon\" href=\"/icon.png\" type=\"image/png\">\n"
	      "<link rel=\"apple-touch-icon\" href=\"/icon.png\" type=\"image/png\">\n"
	      "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=9\">\n", title);
  if (refresh > 0)
    html_printf(client, "<meta http-equiv=\"refresh\" content=\"%d\">\n", refresh);
  html_printf(client,
	      "<meta name=\"viewport\" content=\"width=device-width\">\n"
	      "<style>\n"
	      "body { font-family: sans-serif; margin: 0; }\n"
              "div.header { background: rgb(51,51,51); color: white; left: 0px; margin: 0px; padding: 10px; right: 0px; width: 100%%; }\n"
              "div.footer { background: rgba(0,0,0,0.8); color: white; bottom: 0px; left: 0px; margin: 0px; padding: 10px; position: fixed; right: 0px; width: 100%%; }\n"
              "div.header a, div.footer a { color: rgb(64, 154, 232); text-decoration: none; }\n"
	      "div.body { padding: 0px 10px 50px; }\n"
              "div.even { background: #fcfcfc; margin-left: -10px; margin-right: -10px; padding: 5px 10px; width: 100%%; }\n"
              "div.odd { background: #f0f0f0; margin-left: -10px; margin-right: -10px; padding: 5px 10px; width: 100%%; }\n"
	      "span.badge { background: #090; border-radius: 5px; color: #fff; padding: 5px 10px; }\n"
	      "span.bar { box-shadow: 0px 1px 5px #333; font-size: 75%%; }\n"
	      "table.form { border-collapse: collapse; margin-left: auto; margin-right: auto; margin-top: 10px; width: auto; }\n"
	      "table.form td, table.form th { padding: 5px 2px; }\n"
	      "table.form td.meter { border-right: solid 1px #ccc; padding: 0px; width: 400px; }\n"
	      "table.form th { text-align: right; }\n"
	      "table.striped { border-bottom: solid thin black; border-collapse: collapse; width: 100%%; }\n"
	      "table.striped tr:nth-child(even) { background: #fcfcfc; }\n"
	      "table.striped tr:nth-child(odd) { background: #f0f0f0; }\n"
	      "table.striped th { background: white; border-bottom: solid thin black; text-align: left; vertical-align: bottom; }\n"
	      "table.striped td { margin: 0; padding: 5px; vertical-align: top; }\n"
              "p.buttons { line-height: 200%%; }\n"
	      "a.button { background: black; border-color: black; border-radius: 8px; color: white; font-size: 12px; padding: 4px 10px; text-decoration: none; white-space: nowrap; }\n"
              "a:hover.button { background: #444; border-color: #444; }\n"
	      "</style>\n"
	      "</head>\n"
	      "<body>\n"
	      "<div class=\"header\"><a href=\"/\">ippserver v" IPPSAMPLE_VERSION "</a> Prototype IPP Implementation</div>\n"
	      "<div class=\"body\">\n");
}


/*
 * 'html_printf()' - Send formatted text to the client, quoting as needed.
 */

static void
html_printf(server_client_t *client,	/* I - Client */
	    const char    *format,	/* I - Printf-style format string */
	    ...)			/* I - Additional arguments as needed */
{
  va_list	ap;			/* Pointer to arguments */
  const char	*start;			/* Start of string */
  char		size,			/* Size character (h, l, L) */
		type;			/* Format type character */
  int		width,			/* Width of field */
		prec;			/* Number of characters of precision */
  char		tformat[100],		/* Temporary format string for snprintf() */
		*tptr,			/* Pointer into temporary format */
		temp[1024];		/* Buffer for formatted numbers */
  char		*s;			/* Pointer to string */


 /*
  * Loop through the format string, formatting as needed...
  */

  va_start(ap, format);
  start = format;

  while (*format)
  {
    if (*format == '%')
    {
      if (format > start)
        httpWrite(client->http, start, (size_t)(format - start));

      tptr    = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        httpWrite(client->http, "%", 1);
        format ++;
	start = format;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        *tptr++ = *format++;

      if (*format == '*')
      {
       /*
        * Get width from argument...
	*/

	format ++;
	width = va_arg(ap, int);

	snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", width);
	tptr += strlen(tptr);
      }
      else
      {
	width = 0;

	while (isdigit(*format & 255))
	{
	  if (tptr < (tformat + sizeof(tformat) - 1))
	    *tptr++ = *format;

	  width = width * 10 + *format++ - '0';
	}
      }

      if (*format == '.')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        format ++;

        if (*format == '*')
	{
         /*
	  * Get precision from argument...
	  */

	  format ++;
	  prec = va_arg(ap, int);

	  snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", prec);
	  tptr += strlen(tptr);
	}
	else
	{
	  prec = 0;

	  while (isdigit(*format & 255))
	  {
	    if (tptr < (tformat + sizeof(tformat) - 1))
	      *tptr++ = *format;

	    prec = prec * 10 + *format++ - '0';
	  }
	}
      }

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';

	if (tptr < (tformat + sizeof(tformat) - 2))
	{
	  *tptr++ = 'l';
	  *tptr++ = 'l';
	}

	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        size = *format++;
      }
      else
        size = 0;


      if (!*format)
      {
        start = format;
        break;
      }

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';
      start = format;

      switch (type)
      {
	case 'E' : /* Floating point formats */
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, double));

            httpWrite(client->http, temp, strlen(temp));
	    break;

        case 'B' : /* Integer formats */
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long long));
	    else
#  endif /* HAVE_LONG_LONG */
            if (size == 'l')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long));
	    else
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, int));

            httpWrite(client->http, temp, strlen(temp));
	    break;

	case 'p' : /* Pointer value */
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, void *));

            httpWrite(client->http, temp, strlen(temp));
	    break;

        case 'c' : /* Character or character array */
            if (width <= 1)
            {
              temp[0] = (char)va_arg(ap, int);
              temp[1] = '\0';
              html_escape(client, temp, 1);
            }
            else
              html_escape(client, va_arg(ap, char *), (size_t)width);
	    break;

	case 's' : /* String */
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

            html_escape(client, s, strlen(s));
	    break;
      }
    }
    else
      format ++;
  }

  if (format > start)
    httpWrite(client->http, start, (size_t)(format - start));

  va_end(ap);
}


/*
 * 'parse_options()' - Parse URL options into CUPS options.
 *
 * The client->options string is destroyed by this function.
 */

static size_t				/* O - Number of options */
parse_options(server_client_t *client,	/* I - Client */
              cups_option_t   **options)/* O - Options */
{
  char		*post_data = NULL,	/* POST form data */
		*name,			/* Name */
      		*value,			/* Value */
		*next;			/* Next name=value pair */
  size_t	num_options = 0;	/* Number of options */


  *options = NULL;

  if (httpGetState(client->http) == HTTP_STATE_POST_RECV)
  {
    off_t	post_len = httpGetLength(client->http);
					/* Length of POST form data */

    if (post_len == 0 || post_len > 65535)
      post_len = 65535;

    if ((post_data = calloc(1, (size_t)post_len + 1)) != NULL)
    {
      char	*post_ptr = post_data,	/* Pointer into POST form data */
		*post_end = post_data + post_len;
					/* Pointer to end of POST form data */
      ssize_t	post_bytes;		/* Number of bytes read... */


      while ((post_bytes = httpRead(client->http, post_ptr, (size_t)(post_end - post_ptr))) > 0)
      {
        post_ptr += post_bytes;
      }

      *post_ptr = '\0';
    }

    name = post_data;
  }
  else
    name = client->options;

  while (name && *name)
  {
    if ((value = strchr(name, '=')) == NULL)
      break;

    *value++ = '\0';
    if ((next = strchr(value, '&')) != NULL)
      *next++ = '\0';

    num_options = cupsAddOption(name, value, num_options, options);
    name        = next;
  }

  if (post_data)
    free(post_data);

  return (num_options);
}


/*
 * 'send_mobile_config()' - Send an Apple mobile configuration file for one or
 *                          more printers.
 */

static int				/* O - 1 on success, 0 on failure */
send_mobile_config(
    server_client_t  *client,		/* I - Client connection */
    server_printer_t *printer)		/* I - Printer to send (NULL for all) */
{
  if (!serverRespondHTTP(client, HTTP_STATUS_OK, NULL, "application/x-apple-aspen-config", 0))
    return (0);

  html_printf(client,
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
              "<plist version=\"1.0\">\n"
              "\t<dict>\n"
              "\t\t<key>PayloadIdentifier</key>\n"
              "\t\t<string>org.pwg.ippserver.%s</string>\n"
              "\t\t<key>PayloadUUID</key>\n"
              "\t\t<string>Unused</string>\n"
              "\t\t<key>PayloadType</key>\n"
              "\t\t<string>Configuration</string>\n"
              "\t\t<key>PayloadVersion</key>\n"
              "\t\t<integer>1</integer>\n"
              "\t\t<key>PayloadDescription</key>\n"
              "\t\t<string>Printers on %s.</string>\n"
              "\t\t<key>PayloadDisplayName</key>\n"
              "\t\t<string>%s Printers</string>\n"
              "\t\t<key>PayloadContent</key>\n"
              "\t\t<array>\n", ServerName, ServerName, ServerName);

  if (printer)
  {
    send_printer_payload(client, printer);
  }
  else
  {
    cupsRWLockRead(&PrintersRWLock);
    for (printer = (server_printer_t *)cupsArrayGetFirst(Printers); printer; printer = (server_printer_t *)cupsArrayGetNext(Printers))
    {
      if (printer->type == SERVER_TYPE_PRINT)
        send_printer_payload(client, printer);
    }
    cupsRWUnlock(&PrintersRWLock);
  }

  html_printf(client,
              "\t\t</array>\n"
              "\t</dict>\n"
              "</plist>\n");
  httpWrite(client->http, "", 0);

  return (1);
}


/*
 * 'send_printer_payload()' - Send the mobile configuration payload for a
 *                            printer.
 */

static void
send_printer_payload(
    server_client_t  *client,		/* I - Client connection */
    server_printer_t *printer)		/* I - Printer to send */
{

  const char	*make_and_model,	/* printer-make-and-model value */
		*uuid;			/* printer-uuid value */

  cupsRWLockRead(&printer->rwlock);

  make_and_model = ippGetString(ippFindAttribute(printer->pinfo.attrs, "printer-make-and-model", IPP_TAG_TEXT), 0, NULL);
  uuid           = ippGetString(ippFindAttribute(printer->pinfo.attrs, "printer-uuid", IPP_TAG_URI), 0, NULL);

  if (uuid)
    uuid += 9;				/* Skip "urn:uuid:" URI prefix */
  else
    uuid = "unknown";

  html_printf(client,
              "\t\t\t<dict>\n"
              "\t\t\t\t<key>PayloadIdentifier</key>\n"
              "\t\t\t\t<string>org.pwg.ippserver.%s.%s</string>\n"
              "\t\t\t\t<key>PayloadUUID</key>\n"
              "\t\t\t\t<string>%s</string>\n"
              "\t\t\t\t<key>PayloadType</key>\n"
              "\t\t\t\t<string>com.apple.airprint</string>\n"
              "\t\t\t\t<key>PayloadVersion</key>\n"
              "\t\t\t\t<integer>1</integer>\n"
              "\t\t\t\t<key>PayloadDescription</key>\n"
              "\t\t\t\t<string>%s</string>\n"
              "\t\t\t\t<key>PayloadDisplayName</key>\n"
              "\t\t\t\t<string>%s</string>\n"
              "\t\t\t\t<key>AirPrint</key>\n"
              "\t\t\t\t<array>\n"
              "\t\t\t\t\t<dict>\n"
              "\t\t\t\t\t\t<key>IPAddress</key>\n"
              "\t\t\t\t\t\t<string>%s</string>\n"
              "\t\t\t\t\t\t<key>ResourcePath</key>\n"
              "\t\t\t\t\t\t<string>%s</string>\n"
              "\t\t\t\t\t\t<key>Port</key>\n"
              "\t\t\t\t\t\t<integer>%d</integer>\n"
              "\t\t\t\t\t\t<key>ForceTLS</key>\n"
              "\t\t\t\t\t\t<true />\n"
              "\t\t\t\t\t</dict>\n"
              "\t\t\t\t</array>\n"
              "\t\t\t</dict>\n", ServerName, uuid, uuid, make_and_model ? make_and_model : "unknown", printer->dns_sd_name, ServerName, printer->resource, DefaultPort);

  cupsRWUnlock(&printer->rwlock);
}


/*
 * 'show_materials()' - Show material load state.
 */

static int				/* O - 1 on success, 0 on failure */
show_materials(
    server_client_t  *client,		/* I - Client connection */
    server_printer_t *printer,		/* I - Printer to show */
    const char       *encoding)		/* I - Content-Encoding */
{
  size_t		i, j,		/* Looping vars */
			count;		/* Number of values */
  ipp_attribute_t	*materials_db,	/* materials-col-database attribute */
			*materials_ready,/* materials-col-ready attribute */
                        *attr;		/* Other attribute */
  ipp_t			*materials_col;	/* materials-col-xxx value */
  const char            *material_name,	/* materials-col-database material-name value */
                        *material_key,	/* materials-col-database material-key value */
                        *ready_key,	/* materials-col-ready material-key value */
                        *ready_name;	/* materials-col-ready marterial-name value */
  int			max_materials;	/* max-materials-col-supported value */
  size_t		num_options = 0;/* Number of form options */
  cups_option_t		*options = NULL;/* Form options */


 /*
  * Grab the available, ready, and number of materials from the printer.
  */

  if (!serverRespondHTTP(client, HTTP_STATUS_OK, encoding, "text/html", 0))
    return (0);

  html_header(client, printer->dns_sd_name, 0);

  html_printf(client, "<p class=\"buttons\"><a class=\"button\" href=\"/\">Show Printers</a> <a class=\"button\" href=\"%s\">Show Jobs</a></p>\n", printer->resource);
  html_printf(client, "<h1><img align=\"left\" src=\"%s/icon.png\" width=\"64\" height=\"64\">%s Materials</h1>\n", printer->resource, printer->dns_sd_name);

  if ((materials_db = ippFindAttribute(printer->pinfo.attrs, "materials-col-database", IPP_TAG_BEGIN_COLLECTION)) == NULL)
  {
    html_printf(client, "<p>Error: No materials-col-database defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((materials_ready = ippFindAttribute(printer->pinfo.attrs, "materials-col-ready", IPP_TAG_ZERO)) == NULL)
  {
    html_printf(client, "<p>Error: No materials-col-ready defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((attr = ippFindAttribute(printer->pinfo.attrs, "max-materials-col-supported", IPP_TAG_INTEGER)) == NULL)
  {
    html_printf(client, "<p>Error: No max-materials-col-supported defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  max_materials = ippGetInteger(attr, 0);

 /*
  * Process form data if present...
  */

  if (printer->pinfo.web_forms)
    num_options = parse_options(client, &options);

  if (num_options > 0)
  {
   /*
    * WARNING: A real printer/server implementation MUST NOT implement
    * material updates via a GET request - GET requests are supposed to be
    * idempotent (without side-effects) and we obviously are not
    * authenticating access here.  This form is provided solely to
    * enable testing and development!
    */

    char	name[255];		/* Form name */
    const char	*val;			/* Form value */

    cupsRWLockWrite(&printer->rwlock);

    ippDeleteAttribute(printer->pinfo.attrs, materials_ready);
    materials_ready = NULL;

    for (i = 0; i < max_materials; i ++)
    {
      snprintf(name, sizeof(name), "material%u", (unsigned)i);
      if ((val = cupsGetOption(name, num_options, options)) == NULL || !*val)
        continue;

      for (j = 0, count = ippGetCount(materials_db); j < count; j ++)
      {
        materials_col = ippGetCollection(materials_db, j);
        material_key  = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);

        if (!strcmp(material_key, val))
        {
          if (!materials_ready)
            materials_ready = ippAddCollection(printer->pinfo.attrs, IPP_TAG_PRINTER, "materials-col-ready", materials_col);
          else
            ippSetCollection(printer->pinfo.attrs, &materials_ready, ippGetCount(materials_ready), materials_col);
          break;
        }
      }
    }

    if (!materials_ready)
      materials_ready = ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "materials-col-ready");

    cupsRWUnlock(&printer->rwlock);
  }

 /*
  * Show the currently loaded materials and allow the user to make selections...
  */

  if (printer->pinfo.web_forms)
    html_printf(client, "<form method=\"GET\" action=\"%s/materials\">\n", printer->resource);

  html_printf(client, "<table class=\"form\" summary=\"Materials\">\n");

  for (i = 0; i < max_materials; i ++)
  {
    materials_col = ippGetCollection(materials_ready, i);
    ready_key     = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);

    html_printf(client, "<tr><th>Material %u:</th>", (unsigned)i + 1);
    if (printer->pinfo.web_forms)
    {
      html_printf(client, "<td><select name=\"material%u\"><option value=\"\">None</option>", (unsigned)i);
      for (j = 0, count = ippGetCount(materials_db); j < count; j ++)
      {
	materials_col = ippGetCollection(materials_db, j);
	material_key  = ippGetString(ippFindAttribute(materials_col, "material-key", IPP_TAG_ZERO), 0, NULL);
	material_name = ippGetString(ippFindAttribute(materials_col, "material-name", IPP_TAG_NAME), 0, NULL);

	if (material_key && material_name)
	  html_printf(client, "<option value=\"%s\"%s>%s</option>", material_key, ready_key && material_key && !strcmp(ready_key, material_key) ? " selected" : "", material_name);
	else if (material_key)
	  html_printf(client, "<!-- Error: no material-name for material-key=\"%s\" -->", material_key);
	else if (material_name)
	  html_printf(client, "<!-- Error: no material-key for material-name=\"%s\" -->", material_name);
	else
	  html_printf(client, "<!-- Error: no material-key or material-name for materials-col-database[%u] -->", (unsigned)j + 1);
      }
      html_printf(client, "</select></td></tr>\n");
    }
    else if ((ready_name = ippGetString(ippFindAttribute(materials_col, "material-name", IPP_TAG_ZERO), 0, NULL)) != NULL)
      html_printf(client, "%s</td></tr>\n", ready_name);
    else if (ready_key)
      html_printf(client, "%s</td></tr>\n", ready_key);
    else
      html_printf(client, "None</td></tr>\n");
  }

  if (printer->pinfo.web_forms)
  {
    html_printf(client, "<tr><td></td><td><input type=\"submit\" value=\"Update Materials\">");
    if (num_options > 0)
      html_printf(client, " <span class=\"badge\" id=\"status\">Material updated.</span>\n");
    html_printf(client, "</td></tr></table></form>\n");

    if (num_options > 0)
      html_printf(client, "<script>\n"
			  "setTimeout(hide_status, 3000);\n"
			  "function hide_status() {\n"
			  "  var status = document.getElementById('status');\n"
			  "  status.style.display = 'none';\n"
			  "}\n"
			  "</script>\n");
  }
  else
    html_printf(client, "</table>\n");

  html_footer(client);

  cupsFreeOptions(num_options, options);

  return (1);
}


/*
 * 'show_media()' - Show media load state.
 */

static int				/* O - 1 on success, 0 on failure */
show_media(server_client_t  *client,	/* I - Client connection */
           server_printer_t *printer,	/* I - Printer to show */
           const char       *encoding)	/* I - Content-Encoding */
{
  size_t		i, j,		/* Looping vars */
                        num_ready,	/* Number of ready media */
                        num_sizes,	/* Number of media sizes */
			num_sources,	/* Number of media sources */
                        num_types;	/* Number of media types */
  ipp_attribute_t	*media_col_ready,/* media-col-ready attribute */
                        *media_ready,	/* media-ready attribute */
                        *media_sizes,	/* media-supported attribute */
                        *media_sources,	/* media-source-supported attribute */
                        *media_types,	/* media-type-supported attribute */
                        *input_tray;	/* printer-input-tray attribute */
//                        *attr;		/* Other attribute */
  ipp_t			*media_col;	/* media-col value */
  const char            *media_size,	/* media value */
                        *media_source,	/* media-source value */
                        *media_type,	/* media-type value */
                        *ready_size,	/* media-col-ready media-size[-name] value */
                        *ready_source,	/* media-col-ready media-source value */
                        *ready_tray,	/* printer-input-tray value */
                        *ready_type;	/* media-col-ready media-type value */
  char			tray_str[1024],	/* printer-input-tray string value */
			*tray_ptr;	/* Pointer into value */
  size_t		tray_len;	/* Length of printer-input-tray value */
  int			ready_sheets;	/* printer-input-tray sheets value */
  size_t		num_options = 0;/* Number of form options */
  cups_option_t		*options = NULL;/* Form options */
  static const int	sheets[] =	/* Number of sheets */
  {
    250,
    100,
    25,
    5,
    0
  };


  if (!serverRespondHTTP(client, HTTP_STATUS_OK, encoding, "text/html", 0))
    return (0);

  html_header(client, printer->name, 0);

  html_printf(client, "<p class=\"buttons\"><a class=\"button\" href=\"/\">Show Printers</a> <a class=\"button\" href=\"%s\">Show Jobs</a> <a class=\"button\" href=\"%s/supplies\">Show Supplies</a></p>\n", printer->resource, printer->resource);
  html_printf(client, "<h1><img align=\"left\" src=\"%s/icon.png\" width=\"64\" height=\"64\">%s Media</h1>\n", printer->resource, printer->dns_sd_name);

  if ((media_col_ready = ippFindAttribute(printer->pinfo.attrs, "media-col-ready", IPP_TAG_BEGIN_COLLECTION)) == NULL)
  {
    html_printf(client, "<p>Error: No media-col-ready defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  media_ready = ippFindAttribute(printer->pinfo.attrs, "media-ready", IPP_TAG_ZERO);

  if ((media_sizes = ippFindAttribute(printer->pinfo.attrs, "media-supported", IPP_TAG_ZERO)) == NULL)
  {
    html_printf(client, "<p>Error: No media-supported defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((media_sources = ippFindAttribute(printer->pinfo.attrs, "media-source-supported", IPP_TAG_ZERO)) == NULL)
  {
    html_printf(client, "<p>Error: No media-source-supported defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((media_types = ippFindAttribute(printer->pinfo.attrs, "media-type-supported", IPP_TAG_ZERO)) == NULL)
  {
    html_printf(client, "<p>Error: No media-type-supported defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if ((input_tray = ippFindAttribute(printer->pinfo.attrs, "printer-input-tray", IPP_TAG_STRING)) == NULL)
  {
    html_printf(client, "<p>Error: No printer-input-tray defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  num_ready   = ippGetCount(media_col_ready);
  num_sizes   = ippGetCount(media_sizes);
  num_sources = ippGetCount(media_sources);
  num_types   = ippGetCount(media_types);

  if (num_sources != ippGetCount(input_tray))
  {
    html_printf(client, "<p>Error: Different number of trays in media-source-supported and printer-input-tray defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

 /*
  * Process form data if present...
  */

  if (printer->pinfo.web_forms)
    num_options = parse_options(client, &options);

  if (num_options > 0)
  {
   /*
    * WARNING: A real printer/server implementation MUST NOT implement
    * media updates via a GET request - GET requests are supposed to be
    * idempotent (without side-effects) and we obviously are not
    * authenticating access here.  This form is provided solely to
    * enable testing and development!
    */

    char	name[255];		/* Form name */
    const char	*val;			/* Form value */
    pwg_media_t	*media;			/* Media info */

    cupsRWLockWrite(&printer->rwlock);

    ippDeleteAttribute(printer->pinfo.attrs, input_tray);
    input_tray = NULL;

    ippDeleteAttribute(printer->pinfo.attrs, media_col_ready);
    media_col_ready = NULL;

    if (media_ready)
    {
      ippDeleteAttribute(printer->pinfo.attrs, media_ready);
      media_ready = NULL;
    }

    printer->state_reasons &= (server_preason_t)~(SERVER_PREASON_MEDIA_LOW | SERVER_PREASON_MEDIA_EMPTY | SERVER_PREASON_MEDIA_NEEDED);

    for (i = 0; i < num_sources; i ++)
    {
      media_source = ippGetString(media_sources, i, NULL);

      snprintf(name, sizeof(name), "size%u", (unsigned)i);
      if ((media_size = cupsGetOption(name, num_options, options)) != NULL && (media = pwgMediaForPWG(media_size)) != NULL)
      {
        char	media_key[128];		/* media-key value */
        ipp_t	*media_size_col;	/* media-size collection */

        snprintf(name, sizeof(name), "type%u", (unsigned)i);
        media_type = cupsGetOption(name, num_options, options);

        if (media_ready)
          ippSetString(printer->pinfo.attrs, &media_ready, ippGetCount(media_ready), media_size);
        else
          media_ready = ippAddString(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-ready", NULL, media_size);

        if (media_type && *media_type)
          snprintf(media_key, sizeof(media_key), "%s_%s_%s", media_size, media_source, media_type);
        else
          snprintf(media_key, sizeof(media_key), "%s_%s", media_size, media_source);

        media_size_col = ippNew();
        ippAddInteger(media_size_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "x-dimension", media->width);
        ippAddInteger(media_size_col, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "y-dimension", media->length);

        media_col = ippNew();
        ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-key", NULL, media_key);
        ippAddCollection(media_col, IPP_TAG_PRINTER, "media-size", media_size_col);
        ippDelete(media_size_col);
        ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-size-name", NULL, media_size);
        ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-source", NULL, media_source);
        if (media_type && *media_type)
          ippAddString(media_col, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "media-type", NULL, media_type);

        if (media_col_ready)
          ippSetCollection(printer->pinfo.attrs, &media_col_ready, ippGetCount(media_col_ready), media_col);
        else
          media_col_ready = ippAddCollection(printer->pinfo.attrs, IPP_TAG_PRINTER, "media-col-ready", media_col);
        ippDelete(media_col);
      }
      else
        media = NULL;

      snprintf(name, sizeof(name), "level%u", (unsigned)i);
      if ((val = cupsGetOption(name, num_options, options)) != NULL)
        ready_sheets = atoi(val);
      else
        ready_sheets = 0;

      snprintf(tray_str, sizeof(tray_str), "type=sheetFeedAutoRemovableTray;mediafeed=%d;mediaxfeed=%d;maxcapacity=250;level=%d;status=0;name=%s;", media ? media->length : 0, media ? media->width : 0, ready_sheets, media_source);

      if (input_tray)
        ippSetOctetString(printer->pinfo.attrs, &input_tray, ippGetCount(input_tray), tray_str, strlen(tray_str));
      else
        input_tray = ippAddOctetString(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-input-tray", tray_str, strlen(tray_str));

      if (ready_sheets == 0)
      {
        printer->state_reasons |= SERVER_PREASON_MEDIA_EMPTY;
        if (printer->processing_job)
          printer->state_reasons |= SERVER_PREASON_MEDIA_NEEDED;
      }
      else if (ready_sheets < 25)
        printer->state_reasons |= SERVER_PREASON_MEDIA_LOW;
    }

    if (!media_col_ready)
      media_col_ready = ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "media-col-ready");

    if (!media_ready)
      media_ready = ippAddOutOfBand(printer->pinfo.attrs, IPP_TAG_PRINTER, IPP_TAG_NOVALUE, "media-ready");

    cupsRWUnlock(&printer->rwlock);
  }

  if (printer->pinfo.web_forms)
    html_printf(client, "<form method=\"GET\" action=\"%s/media\">\n", printer->resource);

  html_printf(client, "<table class=\"form\" summary=\"Media\">\n");
  for (i = 0; i < num_sources; i ++)
  {
    media_source = ippGetString(media_sources, i, NULL);

    for (j = 0, ready_size = NULL, ready_type = NULL; j < num_ready; j ++)
    {
      media_col    = ippGetCollection(media_col_ready, j);
      ready_size   = ippGetString(ippFindAttribute(media_col, "media-size-name", IPP_TAG_ZERO), 0, NULL);
      ready_source = ippGetString(ippFindAttribute(media_col, "media-source", IPP_TAG_ZERO), 0, NULL);
      ready_type   = ippGetString(ippFindAttribute(media_col, "media-type", IPP_TAG_ZERO), 0, NULL);

      if (ready_source && !strcmp(ready_source, media_source))
        break;

      ready_source = NULL;
      ready_size   = NULL;
      ready_type   = NULL;
    }

   /*
    * Media size...
    */

    html_printf(client, "<tr><th>%s:</th>", media_source);
    if (printer->pinfo.web_forms)
    {
      html_printf(client, "<td><select name=\"size%u\"><option value=\"\">None</option>", (unsigned)i);
      for (j = 0; j < num_sizes; j ++)
      {
        media_size = ippGetString(media_sizes, j, NULL);

        html_printf(client, "<option%s>%s</option>", (ready_size && !strcmp(ready_size, media_size)) ? " selected" : "", media_size);
      }
      html_printf(client, "</select>\n");
    }
    else
      html_printf(client, "<td>%s", ready_size);

   /*
    * Media type...
    */

    if (printer->pinfo.web_forms)
    {
      html_printf(client, "<select name=\"type%u\"><option value=\"\">None</option>", (unsigned)i);
      for (j = 0; j < num_types; j ++)
      {
	media_type = ippGetString(media_types, j, NULL);

	html_printf(client, "<option%s>%s</option>", (ready_type && !strcmp(ready_type, media_type)) ? " selected" : "", media_type);
      }
      html_printf(client, "</select>\n");
    }
    else if (ready_type)
      html_printf(client, ", %s", ready_type);

   /*
    * Level/sheets loaded...
    */

    if ((ready_tray = ippGetOctetString(input_tray, i, &tray_len)) != NULL)
    {
      if (tray_len > (sizeof(tray_str) - 1))
        tray_len = sizeof(tray_str) - 1;
      memcpy(tray_str, ready_tray, (size_t)tray_len);
      tray_str[tray_len] = '\0';

      if ((tray_ptr = strstr(tray_str, "level=")) != NULL)
        ready_sheets = atoi(tray_ptr + 6);
      else
        ready_sheets = 0;
    }
    else
      ready_sheets = 0;

    if (printer->pinfo.web_forms)
    {
      html_printf(client, "<select name=\"level%u\">", (unsigned)i);
      for (j = 0; j < (int)(sizeof(sheets) / sizeof(sheets[0])); j ++)
	html_printf(client, "<option value=\"%d\"%s>%d sheets</option>", sheets[j], sheets[j] == ready_sheets ? " selected" : "", sheets[j]);
      html_printf(client, "</select></td></tr>\n");
    }
    else if (ready_sheets > 0)
      html_printf(client, ", %d sheets</td></tr>\n", ready_sheets);
    else
      html_printf(client, "</td></tr>\n");
  }

  if (printer->pinfo.web_forms)
  {
    html_printf(client, "<tr><td></td><td><input type=\"submit\" value=\"Update Media\">");
    if (num_options > 0)
      html_printf(client, " <span class=\"badge\" id=\"status\">Media updated.</span>\n");
    html_printf(client, "</td></tr></table></form>\n");

    if (num_options > 0)
      html_printf(client, "<script>\n"
			  "setTimeout(hide_status, 3000);\n"
			  "function hide_status() {\n"
			  "  var status = document.getElementById('status');\n"
			  "  status.style.display = 'none';\n"
			  "}\n"
			  "</script>\n");
  }
  else
    html_printf(client, "</table>\n");

  html_footer(client);

  cupsFreeOptions(num_options, options);

  return (1);
}


/*
 * 'show_status()' - Show printer/system state.
 */

static int				/* O - 1 on success, 0 on failure */
show_status(server_client_t  *client,	/* I - Client connection */
            server_printer_t *printer,	/* I - Printer to show or NULL for system */
            const char       *encoding)	/* I - Content-Encoding to use */
{
  server_job_t		*job;		/* Current job */
  int			i, j;		/* Looping vars */
  server_preason_t	reason;		/* Current reason */
  int			apple_client;	/* Is the client running an Apple OS? */
  static const char * const reasons[] =	/* Reason strings */
  {
    "Other",
    "Cover Open",
    "Input Tray Missing",
    "Marker Supply Empty",
    "Marker Supply Low",
    "Marker Waste Almost Full",
    "Marker Waste Full",
    "Media Empty",
    "Media Jam",
    "Media Low",
    "Media Needed",
    "Moving to Paused",
    "Paused",
    "Spool Area Full",
    "Toner Empty",
    "Toner Low"
  };


  apple_client = strstr(httpGetField(client->http, HTTP_FIELD_USER_AGENT), "Mac OS X") != NULL;

  if (!serverRespondHTTP(client, HTTP_STATUS_OK, encoding, "text/html", 0))
    return (0);

  if (printer)
  {
    html_header(client, printer->dns_sd_name, printer->state == IPP_PSTATE_PROCESSING ? 5 : 15);
    if (!strncmp(printer->resource, "/ipp/print3d", 12))
    {
      html_printf(client, "<p class=\"buttons\"><a class=\"button\" href=\"/\">Show Printers</a> <a class=\"button\" href=\"%s/materials\">Show Materials</a>\n", printer->resource);
    }
    else
    {
      html_printf(client, "<p class=\"buttons\"><p class=\"buttons\"><a class=\"button\" href=\"/\">Show Printers</a> <a class=\"button\" href=\"%s/media\">Show Media</a> <a class=\"button\" href=\"%s/supplies\">Show Supplies</a>", printer->resource, printer->resource);
      if (apple_client)
        html_printf(client, " <a class=\"button\" href=\"%s/apple.mobileconfig\">Use on iOS</a>", printer->resource);
      html_printf(client, "</p>\n");
    }
    html_printf(client, "<h1><img align=\"left\" src=\"%s/icon.png\" width=\"64\" height=\"64\">%s Jobs</h1>\n", printer->resource, printer->dns_sd_name);
    html_printf(client, "<p>%s, %u job(s).", printer->state == IPP_PSTATE_IDLE ? "Idle" : printer->state == IPP_PSTATE_PROCESSING ? "Printing" : "Stopped", (unsigned)cupsArrayGetCount(printer->jobs));
    for (i = 0, reason = 1; i < (int)(sizeof(reasons) / sizeof(reasons[0])); i ++, reason <<= 1)
      if (printer->state_reasons & reason)
        html_printf(client, "\n<br>&nbsp;&nbsp;&nbsp;&nbsp;%s", reasons[i]);
    html_printf(client, "</p>\n");

    if (cupsArrayGetCount(printer->jobs) > 0)
    {
      cupsRWLockRead(&(printer->rwlock));

      html_printf(client, "<table class=\"striped\" summary=\"Jobs\"><thead><tr><th>Job #</th><th>Name</th><th>Owner</th><th>When</th></tr></thead><tbody>\n");
      for (job = (server_job_t *)cupsArrayGetFirst(printer->jobs); job; job = (server_job_t *)cupsArrayGetNext(printer->jobs))
      {
        char	when[256],		/* When job queued/started/finished */
                hhmmss[64];		/* Time HH:MM:SS */

        switch (job->state)
        {
          case IPP_JSTATE_PENDING :
          case IPP_JSTATE_HELD :
              snprintf(when, sizeof(when), "Queued at %s", serverTimeString(job->created, hhmmss, sizeof(hhmmss)));
              break;
          case IPP_JSTATE_PROCESSING :
          case IPP_JSTATE_STOPPED :
              snprintf(when, sizeof(when), "Started at %s", serverTimeString(job->processing, hhmmss, sizeof(hhmmss)));
              break;
          case IPP_JSTATE_ABORTED :
              snprintf(when, sizeof(when), "Aborted at %s", serverTimeString(job->completed, hhmmss, sizeof(hhmmss)));
              break;
          case IPP_JSTATE_CANCELED :
              snprintf(when, sizeof(when), "Canceled at %s", serverTimeString(job->completed, hhmmss, sizeof(hhmmss)));
              break;
          case IPP_JSTATE_COMPLETED :
              snprintf(when, sizeof(when), "Completed at %s", serverTimeString(job->completed, hhmmss, sizeof(hhmmss)));
              break;
        }

        html_printf(client, "<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>\n", job->id, job->name, job->username, when);
      }
      html_printf(client, "</tbody></table>\n");

      cupsRWUnlock(&(printer->rwlock));
    }
  }
  else
  {
    html_header(client, IPPSAMPLE_VERSION, 0);
    for (i = 0, printer = (server_printer_t *)cupsArrayGetFirst(Printers); printer; i ++, printer = (server_printer_t *)cupsArrayGetNext(Printers))
    {
      html_printf(client, "<div class=\"%s\">\n", (i & 1) ? "odd" : "even");
      html_printf(client, "  <h1><img align=\"left\" src=\"%s/icon.png\" width=\"64\" height=\"64\">%s</h1>\n", printer->resource, printer->dns_sd_name);
      html_printf(client, "  <p>%s, %u job(s).", printer->state == IPP_PSTATE_IDLE ? "Idle" : printer->state == IPP_PSTATE_PROCESSING ? "Printing" : "Stopped", (unsigned)cupsArrayGetCount(printer->jobs));
      for (j = 0, reason = 1; j < (int)(sizeof(reasons) / sizeof(reasons[0])); j ++, reason <<= 1)
        if (printer->state_reasons & reason)
          html_printf(client, "\n<br>&nbsp;&nbsp;&nbsp;&nbsp;%s", reasons[j]);
      html_printf(client, "</p>\n");
      if (!strncmp(printer->resource, "/ipp/print3d", 12))
      {
        html_printf(client, "  <p class=\"buttons\"><a class=\"button\" href=\"%s\">Show Jobs</a> <a class=\"button\" href=\"%s/materials\">Show Materials</a></p>\n", printer->resource, printer->resource);
      }
      else
      {
        html_printf(client, "  <p class=\"buttons\"><a class=\"button\" href=\"%s\">Show Jobs</a> <a class=\"button\" href=\"%s/media\">Show Media</a> <a class=\"button\" href=\"%s/supplies\">Show Supplies</a>", printer->resource, printer->resource, printer->resource);
	if (apple_client)
	  html_printf(client, " <a class=\"button\" href=\"%s/apple.mobileconfig\">Use on iOS</a>", printer->resource);
	html_printf(client, "</p>\n");
      }
      html_printf(client, "</div>\n");
    }
  }
  html_footer(client);

  return (1);
}


/*
 * 'show_supplies()' - Show printer supplies.
 */

static int				/* O - 1 on success, 0 on failure */
show_supplies(
    server_client_t  *client,		/* I - Client connection */
    server_printer_t *printer,		/* I - Printer to show */
    const char       *encoding)		/* I - Content-Encoding to use */
{
  size_t	i,			/* Looping var */
		num_supply;		/* Number of supplies */
  ipp_attribute_t *supply,		/* printer-supply attribute */
		*supply_desc;		/* printer-supply-description attribute */
  size_t	num_options = 0;	/* Number of form options */
  cups_option_t	*options = NULL;	/* Form options */
  size_t	supply_len;		/* Length of supply value */
  int		level;			/* Supply level */
  const char	*supply_value;		/* Supply value */
  char		supply_text[1024],	/* Supply string */
		*supply_ptr;		/* Pointer into supply string */
  static const char * const printer_supply[] =
  {					/* printer-supply values */
    "index=1;class=receptacleThatIsFilled;type=wasteToner;unit=percent;"
        "maxcapacity=100;level=%d;colorantname=unknown;",
    "index=2;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=%d;colorantname=black;",
    "index=3;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=%d;colorantname=cyan;",
    "index=4;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=%d;colorantname=magenta;",
    "index=5;class=supplyThatIsConsumed;type=toner;unit=percent;"
        "maxcapacity=100;level=%d;colorantname=yellow;"
  };
  static const char * const backgrounds[] =
  {					/* Background colors for the supply-level bars */
    "#777 linear-gradient(#333,#777)",
    "#000 linear-gradient(#666,#000)",
    "#0FF linear-gradient(#6FF,#0FF)",
    "#F0F linear-gradient(#F6F,#F0F)",
    "#CC0 linear-gradient(#EE6,#EE0)"
  };
  static const char * const colors[] =	/* Text colors for the supply-level bars */
  {
    "#fff",
    "#fff",
    "#000",
    "#000",
    "#000"
  };


  if (!serverRespondHTTP(client, HTTP_STATUS_OK, encoding, "text/html", 0))
    return (0);

  html_header(client, printer->name, 0);

  html_printf(client, "<p class=\"buttons\"><a class=\"button\" href=\"/\">Show Printers</a> <a class=\"button\" href=\"%s\">Show Jobs</a> <a class=\"button\" href=\"%s/media\">Show Media</a></p>\n", printer->resource, printer->resource);
  html_printf(client, "<h1><img align=\"left\" src=\"%s/icon.png\" width=\"64\" height=\"64\">%s Supplies</h1>\n", printer->resource, printer->dns_sd_name);

  if ((supply = ippFindAttribute(printer->pinfo.attrs, "printer-supply", IPP_TAG_STRING)) == NULL)
  {
    html_printf(client, "<p>Error: No printer-supply defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  num_supply = ippGetCount(supply);

  if ((supply_desc = ippFindAttribute(printer->pinfo.attrs, "printer-supply-description", IPP_TAG_TEXT)) == NULL)
  {
    html_printf(client, "<p>Error: No printer-supply-description defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if (num_supply != ippGetCount(supply_desc))
  {
    html_printf(client, "<p>Error: Different number of values for printer-supply and printer-supply-description defined for printer.</p>\n");
    html_footer(client);
    return (1);
  }

  if (printer->pinfo.web_forms)
    num_options = parse_options(client, &options);

  if (num_options > 0)
  {
   /*
    * WARNING: A real printer/server implementation MUST NOT implement
    * supply updates via a GET request - GET requests are supposed to be
    * idempotent (without side-effects) and we obviously are not
    * authenticating access here.  This form is provided solely to
    * enable testing and development!
    */

    char	name[64];		/* Form field */
    const char	*val;			/* Form value */

    cupsRWLockWrite(&printer->rwlock);

    ippDeleteAttribute(printer->pinfo.attrs, supply);
    supply = NULL;

    printer->state_reasons &= (server_preason_t)~(SERVER_PREASON_MARKER_SUPPLY_EMPTY | SERVER_PREASON_MARKER_SUPPLY_LOW | SERVER_PREASON_MARKER_WASTE_ALMOST_FULL | SERVER_PREASON_MARKER_WASTE_FULL | SERVER_PREASON_TONER_EMPTY | SERVER_PREASON_TONER_LOW);

    for (i = 0; i < num_supply; i ++)
    {
      snprintf(name, sizeof(name), "supply%u", (unsigned)i);
      if ((val = cupsGetOption(name, num_options, options)) != NULL)
      {
        level = atoi(val);      /* New level */

        snprintf(supply_text, sizeof(supply_text), printer_supply[i], level);
        if (supply)
          ippSetOctetString(printer->pinfo.attrs, &supply, ippGetCount(supply), supply_text, strlen(supply_text));
        else
          supply = ippAddOctetString(printer->pinfo.attrs, IPP_TAG_PRINTER, "printer-supply", supply_text, strlen(supply_text));

        if (i == 0)
        {
          if (level == 100)
            printer->state_reasons |= SERVER_PREASON_MARKER_WASTE_FULL;
          else if (level > 90)
            printer->state_reasons |= SERVER_PREASON_MARKER_WASTE_ALMOST_FULL;
        }
        else
        {
          if (level == 0)
            printer->state_reasons |= SERVER_PREASON_TONER_EMPTY;
          else if (level < 10)
            printer->state_reasons |= SERVER_PREASON_TONER_LOW;
        }
      }
    }

    cupsRWUnlock(&printer->rwlock);
  }

  if (printer->pinfo.web_forms)
    html_printf(client, "<form method=\"GET\" action=\"%s/supplies\">\n", printer->resource);

  html_printf(client, "<table class=\"form\" summary=\"Supplies\">\n");
  for (i = 0; i < num_supply; i ++)
  {
    supply_value = ippGetOctetString(supply, i, &supply_len);
    if (supply_len > (sizeof(supply_text) - 1))
      supply_len = sizeof(supply_text) - 1;

    memcpy(supply_text, supply_value, (size_t)supply_len);
    supply_text[supply_len] = '\0';

    if ((supply_ptr = strstr(supply_text, "level=")) != NULL)
      level = atoi(supply_ptr + 6);
    else
      level = 50;

    if (printer->pinfo.web_forms)
      html_printf(client, "<tr><th>%s:</th><td><input name=\"supply%u\" size=\"3\" value=\"%d\"></td>", ippGetString(supply_desc, i, NULL), (unsigned)i, level);
    else
      html_printf(client, "<tr><th>%s:</th>", ippGetString(supply_desc, i, NULL));

    if (level < 10)
      html_printf(client, "<td class=\"meter\"><span class=\"bar\" style=\"background: %s; padding: 5px %dpx;\"></span>&nbsp;%d%%</td></tr>\n", backgrounds[i], level * 2, level);
    else
      html_printf(client, "<td class=\"meter\"><span class=\"bar\" style=\"background: %s; color: %s; padding: 5px %dpx;\">%d%%</span></td></tr>\n", backgrounds[i], colors[i], level * 2, level);
  }

  if (printer->pinfo.web_forms)
  {
    html_printf(client, "<tr><td></td><td colspan=\"2\"><input type=\"submit\" value=\"Update Supplies\">");
    if (num_options > 0)
      html_printf(client, " <span class=\"badge\" id=\"status\">Supplies updated.</span>\n");
    html_printf(client, "</td></tr>\n</table>\n</form>\n");

    if (num_options > 0)
      html_printf(client, "<script>\n"
			  "setTimeout(hide_status, 3000);\n"
			  "function hide_status() {\n"
			  "  var status = document.getElementById('status');\n"
			  "  status.style.display = 'none';\n"
			  "}\n"
			  "</script>\n");
  }
  else
    html_printf(client, "</table>\n");

  html_footer(client);

  cupsFreeOptions(num_options, options);

  return (1);
}

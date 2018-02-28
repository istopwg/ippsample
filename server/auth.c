/*
 * Authentication code for sample IPP server implementation.
 *
 * Copyright © 2018 by the IEEE-ISTO Printer Working Group
 * Copyright © 2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "ippserver.h"

#ifdef HAVE_LIBPAM
#  ifdef HAVE_PAM_PAM_APPL_H
#    include <pam/pam_appl.h>
#  else
#    include <security/pam_appl.h>
#  endif /* HAVE_PAM_PAM_APPL_H */
#endif /* HAVE_LIBPAM */


/*
 * Authentication data...
 */

typedef struct server_authdata_s	/* Authentication data */
{
  char	username[256],			/* Username string */
	*password;			/* Password string */
} server_authdata_t;


/*
 * Local functions...
 */

#ifdef HAVE_LIBPAM
static int pam_func(int num_msg, const struct pam_message **msg, struct pam_response **resp, server_authdata_t *data);
#endif /* HAVE_LIBPAM */


/*
 * 'serverAuthenticateClient()' - Authenticate a client request.
 */

http_status_t				/* O - HTTP_STATUS_CONTINUE on success, other HTTP status on failure */
serverAuthenticateClient(
    server_client_t *client)		/* I - Client connection */
{
  const char	*authorization;		/* Authorization header */
  server_authdata_t data;		/* Authorization data */
  int		userlen;		/* Username:password length */
  char		*password;		/* Pointer to password */
#ifdef HAVE_LIBPAM
  pam_handle_t	*pamh;			/* PAM authentication handle */
  int		pamerr;			/* PAM error code */
  struct pam_conv pamdata;		/* PAM conversation data */
#endif /* HAVE_LIBPAM */


 /*
  * See if we have anything we can use in the Authorization header...
  */

  client->username[0] = '\0';

  authorization = httpGetField(client->http, HTTP_FIELD_AUTHORIZATION);

  if (!*authorization)
  {
    return (HTTP_STATUS_CONTINUE);
  }
  else if (!Authorization || strncmp(authorization, "Basic ", 6))
  {
    char	scheme[32],		/* Scheme */
		*schemeptr;		/* Pointer into scheme */

    strlcpy(scheme, authorization, sizeof(scheme));
    if ((schemeptr = strchr(scheme, ' ')) != NULL)
      *scheme = '\0';

    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unsupported authorization scheme \"%s\".", scheme);
    return (HTTP_STATUS_BAD_REQUEST);
  }

 /*
  * OK, what remains is a Basic authorization value.  Parse it and authenticate.
  */

  authorization += 5;
  while (_cups_isspace(*authorization & 255))
    authorization ++;

  userlen = sizeof(data.username);
  httpDecode64_2(data.username, &userlen, authorization);

  if ((password = strchr(data.username, ':')) == NULL)
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Missing password.");
    return (HTTP_STATUS_UNAUTHORIZED);
  }

  *password++ = '\0';

  if (!data.username[0])
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Empty username.");
    return (HTTP_STATUS_UNAUTHORIZED);
  }
  else if (!*password)
  {
    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Empty password.");
    return (HTTP_STATUS_UNAUTHORIZED);
  }

  data.password = password;

#ifdef HAVE_LIBPAM
  pamdata.conv        = pam_func;
  pamdata.appdata_ptr = &data;

  if ((pamerr = pam_start(Authorization, data.username, &pamdata, &pamh)) != PAM_SUCCESS)
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_start() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
    return (HTTP_STATUS_SERVER_ERROR);
  }

#  ifdef PAM_RHOST
  pam_set_item(pamh, PAM_RHOST, client->hostname);
#  endif /* PAM_RHOST */

#  ifdef PAM_TTY
  pam_set_item(pamh, PAM_TTY, "ippserver");
#  endif /* PAM_TTY */

  if ((pamerr = pam_authenticate(pamh, PAM_SILENT)) != PAM_SUCCESS)
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_authenticate() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
    goto auth_failed;
  }

  if ((pamerr = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT)) != PAM_SUCCESS)
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_setcred() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
    goto auth_failed;
  }

  if ((pamerr = pam_acct_mgmt(pamh, PAM_SILENT)) != PAM_SUCCESS)
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_acct_mgmt() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
    goto auth_failed;
  }

  pam_end(pamh, PAM_SUCCESS);

#else
  if (strcmp(data.username, "test") || strcmp(data.password, "test123"))
  {
    serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authentication failed.");
    return (HTTP_STATUS_UNAUTHORIZED);
  }
#endif /* HAVE_LIBPAM */

  serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authenticated as \"%s\".", data.username);

  strlcpy(client->username, data.username, sizeof(client->username));

  return (HTTP_STATUS_CONTINUE);

#ifdef HAVE_LIBPAM
 /*
  * If we get here then authentication failed...
  */

  auth_failed:

  pam_end(pamh, 0);

  serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authentication failed.");

  return (pamerr == PAM_AUTH_ERROR ? HTTP_STATUS_UNAUTHORIZED : HTTP_STATUS_SERVER_ERROR);
#endif /* HAVE_LIBPAM */
}


#ifdef HAVE_LIBPAM
/*
 * 'pam_func()' - PAM conversation function.
 */

static int				/* O - Success or failure */
pam_func(
    int                      num_msg,	/* I - Number of messages */
    const struct pam_message **msg,	/* I - Messages */
    struct pam_response      **resp,	/* O - Responses */
    server_authdata_t        *data)	/* I - Authentication data */
{
  int			i;		/* Looping var */
  struct pam_response	*replies;	/* Replies */
  char

 /*
  * Allocate memory for the responses...
  */

  if ((replies = malloc(sizeof(struct pam_response) * (size_t)num_msg)) == NULL)
    return (PAM_CONV_ERR);

 /*
  * Answer all of the messages...
  */

  for (i = 0; i < num_msg; i ++)
  {
    switch (msg[i]->msg_style)
    {
      case PAM_PROMPT_ECHO_ON :
          replies[i].resp_retcode = PAM_SUCCESS;
          replies[i].resp         = strdup(data->username);
          break;

      case PAM_PROMPT_ECHO_OFF :
          replies[i].resp_retcode = PAM_SUCCESS;
          replies[i].resp         = strdup(data->password);
          break;

      default :
          replies[i].resp_retcode = PAM_SUCCESS;
          replies[i].resp         = NULL;
          break;
    }
  }

 /*
  * Return the responses back to PAM...
  */

  *resp = replies;

  return (PAM_SUCCESS);
}
#endif /* HAVE_LIBPAM */


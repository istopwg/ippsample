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

#include <pwd.h>
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
  http_status_t	status = HTTP_STATUS_CONTINUE;
					/* Returned status */
  const char	*authorization;		/* Authorization header */
  server_authdata_t data;		/* Authorization data */
  int		userlen;		/* Username:password length */
  char		*password;		/* Pointer to password */


 /*
  * See if we have anything we can use in the Authorization header...
  */

  client->username[0] = '\0';

  authorization = httpGetField(client->http, HTTP_FIELD_AUTHORIZATION);

  if (!*authorization)
  {
    status = HTTP_STATUS_UNAUTHORIZED;
  }
  else if ((!AuthService && !AuthTestPassword) || strncmp(authorization, "Basic ", 6))
  {
    char	scheme[32],		/* Scheme */
		*schemeptr;		/* Pointer into scheme */

    strlcpy(scheme, authorization, sizeof(scheme));
    if ((schemeptr = strchr(scheme, ' ')) != NULL)
      *scheme = '\0';

    serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Unsupported authorization scheme \"%s\".", scheme);
    status = HTTP_STATUS_BAD_REQUEST;
  }
  else
  {
   /*
    * OK, what remains is a Basic authorization value.  Parse it and
    * authenticate.
    */

    authorization += 5;
    while (_cups_isspace(*authorization & 255))
      authorization ++;

    userlen = sizeof(data.username);
    httpDecode64_2(data.username, &userlen, authorization);

    if ((password = strchr(data.username, ':')) == NULL)
    {
      serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Missing password.");
      status = HTTP_STATUS_UNAUTHORIZED;
    }
    else
    {
      *password++ = '\0';
      data.password = password;

      if (!data.username[0])
      {
	serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Empty username.");
	status = HTTP_STATUS_UNAUTHORIZED;
      }
      else if (!*password)
      {
	serverLogClient(SERVER_LOGLEVEL_ERROR, client, "Empty password.");
	status = HTTP_STATUS_UNAUTHORIZED;
      }
      else if (!AuthService)
      {
	if (strcmp(data.password, AuthTestPassword))
	{
	  serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authentication failed.");
	  status = HTTP_STATUS_UNAUTHORIZED;
	}
      }
#ifdef HAVE_LIBPAM
      else
      {
       /*
	* Authenticate using PAM...
	*/

	pam_handle_t	*pamh;		/* PAM authentication handle */
	int		pamerr;		/* PAM error code */
	struct pam_conv pamdata;	/* PAM conversation data */

	pamdata.conv        = (int (*)(int, const struct pam_message **, struct pam_response **, void *))pam_func;
	pamdata.appdata_ptr = &data;
	pamh                = NULL;

	if ((pamerr = pam_start(AuthService, data.username, &pamdata, &pamh)) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_start() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}

#  ifdef PAM_RHOST
	else if ((pamerr = pam_set_item(pamh, PAM_RHOST, client->hostname)) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_set_item(PAM_RHOST) returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}
#  endif /* PAM_RHOST */

#  ifdef PAM_TTY
	else if ((pamerr = pam_set_item(pamh, PAM_TTY, "ippserver")) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_set_item(PAM_TTY) returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}
#  endif /* PAM_TTY */

	else if ((pamerr = pam_authenticate(pamh, PAM_SILENT)) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_authenticate() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}
	else if ((pamerr = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT)) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_setcred() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}
	else if ((pamerr = pam_acct_mgmt(pamh, PAM_SILENT)) != PAM_SUCCESS)
	{
	  serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "pam_acct_mgmt() returned %d (%s)", pamerr, pam_strerror(pamh, pamerr));
	}

	if (pamh)
	  pam_end(pamh, PAM_SUCCESS);

	if (pamerr == PAM_AUTH_ERR)
	  status = HTTP_STATUS_UNAUTHORIZED;
	else if (pamerr != PAM_SUCCESS)
	  status = HTTP_STATUS_SERVER_ERROR;
      }

#else /* !HAVE_LIBPAM */
      else
      {
       /*
	* No other authentication methods...
	*/

	serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authentication failed.");
	status = HTTP_STATUS_SERVER_ERROR;
      }
#endif /* HAVE_LIBPAM */
    }
  }

  if (status == HTTP_STATUS_CONTINUE)
  {
   /*
    * Authentication succeeded!
    */

    serverLogClient(SERVER_LOGLEVEL_INFO, client, "Authenticated as \"%s\".", data.username);

    strlcpy(client->username, data.username, sizeof(client->username));
  }

  return (status);
}


/*
 * 'serverAuthorizeUser()' - Authorize access for an authenticated user.
 */

int					/* O - 1 if authorized, 0 otherwise */
serverAuthorizeUser(
    server_client_t *client,		/* I - Client connection */
    const char      *owner,		/* I - Object owner or @code NULL@ if none/not applicable */
    const char      *scope)		/* I - Access scope */
{
  struct passwd	*pw;			/* User account information */
  int		i,			/* Looping var */
		ngroups;		/* Number of groups for user */
#ifdef __APPLE__
  int		groups[2048];		/* Group list */
#else
  gid_t		groups[2048];		/* Group list */
#endif /* __APPLE__ */


 /*
  * If the scope is "all" or "none", then we are authorized or not regardless
  * of the authenticated user.
  */

  if (!strcmp(scope, SERVER_SCOPE_ALL))
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" is authorized because scope is \"all\".", client->username);
    return (1);
  }
  else if (!strcmp(scope, SERVER_SCOPE_NONE))
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" not authorized because scope is \"none\".", client->username);
    return (0);
  }

 /*
  * If the request is not authenticated for any other scope, it cannot be
  * authorized.
  */

  if (!client->username[0])
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "No authenticated user name, not authorized.");
    return (0);
  }

 /*
  * The owner is always authorized...
  */

  if (owner && !strcmp(client->username, owner) && strcmp(scope, SERVER_SCOPE_ADMIN))
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" is authorized because they are the owner.", client->username);
    return (1);
  }

 /*
  * If the user does not exist, it cannot be authorized against a group...
  */

  if ((pw = getpwnam(client->username)) == NULL)
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" does not have a local account.", client->username);
    return (0);
  }

 /*
  * Check group membership...
  */

  ngroups = (int)(sizeof(groups) / sizeof(groups[0]));

#ifdef __APPLE__
  if (getgrouplist(client->username, (int)pw->pw_gid, groups, &ngroups))
#else
  if (getgrouplist(client->username, pw->pw_gid, groups, &ngroups))
#endif /* __APPLE__ */
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" not authorized because the group list could not be retrieved: %s", client->username, strerror(errno));
    return (0);
  }

  if (!strcmp(scope, SERVER_SCOPE_ADMIN))
  {
   /*
    * Authenticate as admin only...
    */

    for (i = 0; i < ngroups; i ++)
      if ((gid_t)groups[i] == AuthAdminGroup)
        break;
  }
  else
  {
   /*
    * Authenticate as admin or operator...
    */

    for (i = 0; i < ngroups; i ++)
      if ((gid_t)groups[i] == AuthAdminGroup || (gid_t)groups[i] == AuthOperatorGroup)
        break;
  }

  if (i < ngroups)
  {
    if ((gid_t)groups[i] == AuthAdminGroup)
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" is authorized because they are an administrator.", client->username);
    else
      serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" is authorized because they are an operator.", client->username);

    return (1);
  }
  else
  {
    serverLogClient(SERVER_LOGLEVEL_DEBUG, client, "User \"%s\" not authorized because they failed the group test.", client->username);
    return (0);
  }
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


 /*
  * Allocate memory for the responses...
  */

  if ((replies = calloc((size_t)num_msg, sizeof(struct pam_response))) == NULL)
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


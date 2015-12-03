/*
 * Configuration file support for sample IPP server implementation.
 *
 * Copyright 2015 by Apple Inc.
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
#include <cups/file.h>
#include <cups/dir.h>
#include <fnmatch.h>


/*
 * Local functions...
 */

static int		compare_printers(server_printer_t *a, server_printer_t *b);
static ipp_t		*get_collection(FILE *fp, const char *filename, int *linenum);
static char		*get_token(FILE *fp, char *buf, int buflen, int *linenum);
static server_printer_t	*load_printer(const char *conf, const char *icon);
static int		load_system(const char *conf);


/*
 * 'serverDNSSDInit()' - Initialize DNS-SD registrations.
 */

void
serverDNSSDInit(void)
{
#ifdef HAVE_DNSSD
  if (DNSServiceCreateConnection(&DNSSDMaster) != kDNSServiceErr_NoError)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

#elif defined(HAVE_AVAHI)
  int error;			/* Error code, if any */

  if ((DNSSDMaster = avahi_threaded_poll_new()) == NULL)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

  if ((DNSSDClient = avahi_client_new(avahi_threaded_poll_get(DNSSDMaster), AVAHI_CLIENT_NO_FAIL, dnssd_client_cb, NULL, &error)) == NULL)
  {
    fputs("Error: Unable to initialize Bonjour.\n", stderr);
    exit(1);
  }

  avahi_threaded_poll_start(DNSSDMaster);
#endif /* HAVE_DNSSD */
}


/*
 * 'serverLoadConfiguration()' - Load the server configuration file.
 */

int					/* O - 1 if successful, 0 on error */
serverLoadConfiguration(
    const char *directory)		/* I - Configuration directory */
{
  cups_dir_t	*dir;			/* Directory pointer */
  cups_dentry_t	*dent;			/* Directory entry */
  char		filename[260],		/* Configuration file/directory */
                *ptr;			/* Pointer into filename */
  server_printer_t *printer;		/* Printer */


 /*
  * First read the system configuration file, if any...
  */

  snprintf(filename, sizeof(filename), "%s/system.conf", directory);
  if (!load_system(filename))
    return (0);

 /*
  * Then see if there are any print queues...
  */

  snprintf(filename, sizeof(filename), "%s/print", directory);
  if ((dir = cupsDirOpen(filename)) != NULL)
  {
    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if (fnmatch("*.conf", dent->filename, 0))
      {
       /*
        * Load the conf file, with any associated icon image.
        */

        strlcpy(filename, dent->filename, sizeof(filename));
        if ((ptr = filename + strlen(filename) - 5) > filename)
          strlcpy(ptr, ".png", sizeof(filename) - (ptr - filename));

        if ((printer = load_printer(dent->filename, access(filename, R_OK) ? NULL : filename)) == NULL)
          continue;

        if (!Printers)
          Printers = cupsArrayNew((cups_array_func_t)compare_printers, NULL);

        cupsArrayAdd(Printers, printer);
      }
    }

    cupsDirClose(dir);
  }

  return (1);
}


/*
 * 'compare_printers()' - Compare two printers.
 */

static int				/* O - Result of comparison */
compare_printers(server_printer_t *a,	/* I - First printer */
                 server_printer_t *b)	/* I - Second printer */
{
  return (strcmp(a->resource, b->resource));
}


/*
 * 'get_collection()' - Get a collection value from a file.
 */

static ipp_t *				/* O  - Collection value */
get_collection(FILE       *fp,		/* I  - File to read from */
               const char *filename,	/* I  - Attributes filename */
	       int        *linenum)	/* IO - Line number */
{
  char		token[1024],		/* Token from file */
		attr[128];		/* Attribute name */
  ipp_tag_t	value;			/* Current value type */
  ipp_t		*col = ippNew();	/* Collection value */
  ipp_attribute_t *lastcol = NULL;	/* Last collection attribute */


  while (get_token(fp, token, sizeof(token), linenum) != NULL)
  {
    if (!strcmp(token, "}"))
      break;
    else if (!strcmp(token, "{") && lastcol)
    {
     /*
      * Another collection value
      */

      ipp_t	*subcol = get_collection(fp, filename, linenum);
					/* Collection value */

      if (subcol)
        ippSetCollection(col, &lastcol, ippGetCount(lastcol), subcol);
      else
	goto col_error;
    }
    else if (!_cups_strcasecmp(token, "MEMBER"))
    {
     /*
      * Attribute...
      */

      lastcol = NULL;

      if (!get_token(fp, token, sizeof(token), linenum))
      {
	fprintf(stderr, "ippserver: Missing MEMBER value tag on line %d of \"%s\".\n", *linenum, filename);
	goto col_error;
      }

      if ((value = ippTagValue(token)) == IPP_TAG_ZERO)
      {
	fprintf(stderr, "ippserver: Bad MEMBER value tag \"%s\" on line %d of \"%s\".\n", token, *linenum, filename);
	goto col_error;
      }

      if (!get_token(fp, attr, sizeof(attr), linenum))
      {
	fprintf(stderr, "ippserver: Missing MEMBER name on line %d of \"%s\".\n", *linenum, filename);
	goto col_error;
      }

      if (!get_token(fp, token, sizeof(token), linenum))
      {
	fprintf(stderr, "ippserver: Missing MEMBER value on line %d of \"%s\".\n", *linenum, filename);
	goto col_error;
      }

      switch (value)
      {
	case IPP_TAG_BOOLEAN :
	    if (!_cups_strcasecmp(token, "true"))
	      ippAddBoolean(col, IPP_TAG_ZERO, attr, 1);
	    else
	      ippAddBoolean(col, IPP_TAG_ZERO, attr, (char)atoi(token));
	    break;

	case IPP_TAG_INTEGER :
	case IPP_TAG_ENUM :
	    ippAddInteger(col, IPP_TAG_ZERO, value, attr, atoi(token));
	    break;

	case IPP_TAG_RESOLUTION :
	    {
	      int	xres,		/* X resolution */
			yres;		/* Y resolution */
	      char	units[6];	/* Units */

	      if (sscanf(token, "%dx%d%5s", &xres, &yres, units) != 3 ||
		  (_cups_strcasecmp(units, "dpi") &&
		   _cups_strcasecmp(units, "dpc") &&
		   _cups_strcasecmp(units, "dpcm") &&
		   _cups_strcasecmp(units, "other")))
	      {
		fprintf(stderr, "ippserver: Bad resolution value \"%s\" on line %d of \"%s\".\n", token, *linenum, filename);
		goto col_error;
	      }

	      if (!_cups_strcasecmp(units, "dpi"))
		ippAddResolution(col, IPP_TAG_ZERO, attr, IPP_RES_PER_INCH, xres, yres);
	      else if (!_cups_strcasecmp(units, "dpc") ||
	               !_cups_strcasecmp(units, "dpcm"))
		ippAddResolution(col, IPP_TAG_ZERO, attr, IPP_RES_PER_CM, xres, yres);
	      else
		ippAddResolution(col, IPP_TAG_ZERO, attr, (ipp_res_t)0, xres, yres);
	    }
	    break;

	case IPP_TAG_RANGE :
	    {
	      int	lowers[4],	/* Lower value */
			uppers[4],	/* Upper values */
			num_vals;	/* Number of values */


	      num_vals = sscanf(token, "%d-%d,%d-%d,%d-%d,%d-%d",
				lowers + 0, uppers + 0,
				lowers + 1, uppers + 1,
				lowers + 2, uppers + 2,
				lowers + 3, uppers + 3);

	      if ((num_vals & 1) || num_vals == 0)
	      {
		fprintf(stderr, "ippserver: Bad rangeOfInteger value \"%s\" on line %d of \"%s\".\n", token, *linenum, filename);
		goto col_error;
	      }

	      ippAddRanges(col, IPP_TAG_ZERO, attr, num_vals / 2, lowers,
			   uppers);
	    }
	    break;

	case IPP_TAG_BEGIN_COLLECTION :
	    if (!strcmp(token, "{"))
	    {
	      ipp_t	*subcol = get_collection(fp, filename, linenum);
				      /* Collection value */

	      if (subcol)
	      {
		lastcol = ippAddCollection(col, IPP_TAG_ZERO, attr, subcol);
		ippDelete(subcol);
	      }
	      else
		goto col_error;
	    }
	    else
	    {
	      fprintf(stderr, "ippserver: Bad collection value on line %d of \"%s\".\n", *linenum, filename);
	      goto col_error;
	    }
	    break;
	case IPP_TAG_STRING :
	    ippAddOctetString(col, IPP_TAG_ZERO, attr, token, (int)strlen(token));
	    break;

	default :
	    if (!strchr(token, ','))
	      ippAddString(col, IPP_TAG_ZERO, value, attr, NULL, token);
	    else
	    {
	     /*
	      * Multiple string values...
	      */

	      int	num_values;	/* Number of values */
	      char	*values[100],	/* Values */
			*ptr;		/* Pointer to next value */


	      values[0]  = token;
	      num_values = 1;

	      for (ptr = strchr(token, ','); ptr; ptr = strchr(ptr, ','))
	      {
		*ptr++ = '\0';
		values[num_values] = ptr;
		num_values ++;
		if (num_values >= (int)(sizeof(values) / sizeof(values[0])))
		  break;
	      }

	      ippAddStrings(col, IPP_TAG_ZERO, value, attr, num_values,
			    NULL, (const char **)values);
	    }
	    break;
      }
    }
  }

  return (col);

 /*
  * If we get here there was a parse error; free memory and return.
  */

  col_error:

  ippDelete(col);

  return (NULL);
}


/*
 * 'get_token()' - Get a token from a file.
 */

static char *				/* O  - Token from file or NULL on EOF */
get_token(FILE *fp,			/* I  - File to read from */
          char *buf,			/* I  - Buffer to read into */
	  int  buflen,			/* I  - Length of buffer */
	  int  *linenum)		/* IO - Current line number */
{
  int	ch,				/* Character from file */
	quote;				/* Quoting character */
  char	*bufptr,			/* Pointer into buffer */
	*bufend;			/* End of buffer */


  for (;;)
  {
   /*
    * Skip whitespace...
    */

    while (isspace(ch = getc(fp)))
    {
      if (ch == '\n')
        (*linenum) ++;
    }

   /*
    * Read a token...
    */

    if (ch == EOF)
      return (NULL);
    else if (ch == '\'' || ch == '\"')
    {
     /*
      * Quoted text or regular expression...
      */

      quote  = ch;
      bufptr = buf;
      bufend = buf + buflen - 1;

      while ((ch = getc(fp)) != EOF)
      {
        if (ch == '\\')
	{
	 /*
	  * Escape next character...
	  */

	  if (bufptr < bufend)
	    *bufptr++ = (char)ch;

	  if ((ch = getc(fp)) != EOF && bufptr < bufend)
	    *bufptr++ = (char)ch;
	}
	else if (ch == quote)
          break;
	else if (bufptr < bufend)
          *bufptr++ = (char)ch;
      }

      *bufptr = '\0';

      return (buf);
    }
    else if (ch == '#')
    {
     /*
      * Comment...
      */

      while ((ch = getc(fp)) != EOF)
	if (ch == '\n')
          break;

      (*linenum) ++;
    }
    else if (ch == '{' || ch == '}' || ch == ',')
    {
      buf[0] = (char)ch;
      buf[1] = '\0';

      return (buf);
    }
    else
    {
     /*
      * Whitespace delimited text...
      */

      ungetc(ch, fp);

      bufptr = buf;
      bufend = buf + buflen - 1;

      while ((ch = getc(fp)) != EOF)
	if (isspace(ch) || ch == '#')
          break;
	else if (bufptr < bufend)
          *bufptr++ = (char)ch;

      if (ch == '#')
        ungetc(ch, fp);
      else if (ch == '\n')
        (*linenum) ++;

      *bufptr = '\0';

      return (buf);
    }
  }
}


/*
 * 'load_attributes()' - Load printer attributes from a file.
 *
 * Syntax is based on ipptool format:
 *
 *    ATTR value-tag name value
 */

static void
load_attributes(const char *filename,	/* I - File to load */
                ipp_t      *attrs)	/* I - Printer attributes */
{
  int		linenum = 0;		/* Current line number */
  FILE		*fp = NULL;		/* Test file */
  char		attr[128],		/* Attribute name */
		token[1024],		/* Token from file */
		*tokenptr;		/* Pointer into token */
  ipp_tag_t	value;			/* Current value type */
  ipp_attribute_t *attrptr,		/* Attribute pointer */
		*lastcol = NULL;	/* Last collection attribute */


  if ((fp = fopen(filename, "r")) == NULL)
  {
    fprintf(stderr, "ippserver: Unable to open \"%s\": %s\n", filename, strerror(errno));
    exit(1);
  }

  while (get_token(fp, token, sizeof(token), &linenum) != NULL)
  {
    if (!_cups_strcasecmp(token, "ATTR"))
    {
     /*
      * Attribute...
      */

      if (!get_token(fp, token, sizeof(token), &linenum))
      {
	fprintf(stderr, "ippserver: Missing ATTR value tag on line %d of \"%s\".\n", linenum, filename);
        exit(1);
      }

      if ((value = ippTagValue(token)) == IPP_TAG_ZERO)
      {
	fprintf(stderr, "ippserver: Bad ATTR value tag \"%s\" on line %d of \"%s\".\n", token, linenum, filename);
	exit(1);
      }

      if (!get_token(fp, attr, sizeof(attr), &linenum))
      {
	fprintf(stderr, "ippserver: Missing ATTR name on line %d of \"%s\".\n", linenum, filename);
	exit(1);
      }

      if (!get_token(fp, token, sizeof(token), &linenum))
      {
	fprintf(stderr, "ippserver: Missing ATTR value on line %d of \"%s\".\n", linenum, filename);
	exit(1);
      }

      attrptr = NULL;

      switch (value)
      {
	case IPP_TAG_BOOLEAN :
	    if (!_cups_strcasecmp(token, "true"))
	      attrptr = ippAddBoolean(attrs, IPP_TAG_PRINTER, attr, 1);
	    else
	      attrptr = ippAddBoolean(attrs, IPP_TAG_PRINTER, attr, (char)atoi(token));
	    break;

	case IPP_TAG_INTEGER :
	case IPP_TAG_ENUM :
	    if (!strchr(token, ','))
	      attrptr = ippAddInteger(attrs, IPP_TAG_PRINTER, value, attr, (int)strtol(token, &tokenptr, 0));
	    else
	    {
	      int	values[100],	/* Values */
			num_values = 1;	/* Number of values */

	      values[0] = (int)strtol(token, &tokenptr, 10);
	      while (tokenptr && *tokenptr &&
		     num_values < (int)(sizeof(values) / sizeof(values[0])))
	      {
		if (*tokenptr == ',')
		  tokenptr ++;
		else if (!isdigit(*tokenptr & 255) && *tokenptr != '-')
		  break;

		values[num_values] = (int)strtol(tokenptr, &tokenptr, 0);
		num_values ++;
	      }

	      attrptr = ippAddIntegers(attrs, IPP_TAG_PRINTER, value, attr, num_values, values);
	    }

	    if (!tokenptr || *tokenptr)
	    {
	      fprintf(stderr, "ippserver: Bad %s value \"%s\" on line %d of \"%s\".\n", ippTagString(value), token, linenum, filename);
	      exit(1);
	    }
	    break;

	case IPP_TAG_RESOLUTION :
	    {
	      int	xres,		/* X resolution */
			yres;		/* Y resolution */
	      ipp_res_t	units;		/* Units */
	      char	*start,		/* Start of value */
			*ptr,		/* Pointer into value */
			*next = NULL;	/* Next value */

	      for (start = token; start; start = next)
	      {
		xres = yres = (int)strtol(start, (char **)&ptr, 10);
		if (ptr > start && xres > 0)
		{
		  if (*ptr == 'x')
		    yres = (int)strtol(ptr + 1, (char **)&ptr, 10);
		}

		if (ptr && (next = strchr(ptr, ',')) != NULL)
		  *next++ = '\0';

		if (ptr <= start || xres <= 0 || yres <= 0 || !ptr ||
		    (_cups_strcasecmp(ptr, "dpi") &&
		     _cups_strcasecmp(ptr, "dpc") &&
		     _cups_strcasecmp(ptr, "dpcm") &&
		     _cups_strcasecmp(ptr, "other")))
		{
		  fprintf(stderr, "ippserver: Bad resolution value \"%s\" on line %d of \"%s\".\n", token, linenum, filename);
		  exit(1);
		}

		if (!_cups_strcasecmp(ptr, "dpc") || !_cups_strcasecmp(ptr, "dpcm"))
		  units = IPP_RES_PER_CM;
		else
		  units = IPP_RES_PER_INCH;

                if (attrptr)
		  ippSetResolution(attrs, &attrptr, ippGetCount(attrptr), units, xres, yres);
		else
		  attrptr = ippAddResolution(attrs, IPP_TAG_PRINTER, attr, units, xres, yres);
	      }
	    }
	    break;

	case IPP_TAG_RANGE :
	    {
	      int	lowers[4],	/* Lower value */
			uppers[4],	/* Upper values */
			num_vals;	/* Number of values */


	      num_vals = sscanf(token, "%d-%d,%d-%d,%d-%d,%d-%d",
				lowers + 0, uppers + 0,
				lowers + 1, uppers + 1,
				lowers + 2, uppers + 2,
				lowers + 3, uppers + 3);

	      if ((num_vals & 1) || num_vals == 0)
	      {
		fprintf(stderr, "ippserver: Bad rangeOfInteger value \"%s\" on line %d of \"%s\".", token, linenum, filename);
		exit(1);
	      }

	      attrptr = ippAddRanges(attrs, IPP_TAG_PRINTER, attr, num_vals / 2, lowers,
				     uppers);
	    }
	    break;

	case IPP_TAG_BEGIN_COLLECTION :
	    if (!strcmp(token, "{"))
	    {
	      ipp_t	*col = get_collection(fp, filename, &linenum);
				    /* Collection value */

	      if (col)
	      {
		attrptr = lastcol = ippAddCollection(attrs, IPP_TAG_PRINTER, attr, col);
		ippDelete(col);
	      }
	      else
		exit(1);
	    }
	    else
	    {
	      fprintf(stderr, "ippserver: Bad ATTR collection value on line %d of \"%s\".\n", linenum, filename);
	      exit(1);
	    }

	    do
	    {
	      ipp_t	*col;			/* Collection value */
	      long	pos = ftell(fp);	/* Save position of file */

	      if (!get_token(fp, token, sizeof(token), &linenum))
		break;

	      if (strcmp(token, ","))
	      {
		fseek(fp, pos, SEEK_SET);
		break;
	      }

	      if (!get_token(fp, token, sizeof(token), &linenum) || strcmp(token, "{"))
	      {
		fprintf(stderr, "ippserver: Unexpected \"%s\" on line %d of \"%s\".\n", token, linenum, filename);
		exit(1);
	      }

	      if ((col = get_collection(fp, filename, &linenum)) == NULL)
		break;

	      ippSetCollection(attrs, &attrptr, ippGetCount(attrptr), col);
	      lastcol = attrptr;
	    }
	    while (!strcmp(token, "{"));
	    break;

	case IPP_TAG_STRING :
	    attrptr = ippAddOctetString(attrs, IPP_TAG_PRINTER, attr, token, (int)strlen(token));
	    break;

	default :
	    fprintf(stderr, "ippserver: Unsupported ATTR value tag %s on line %d of \"%s\".\n", ippTagString(value), linenum, filename);
	    exit(1);

	case IPP_TAG_TEXTLANG :
	case IPP_TAG_NAMELANG :
	case IPP_TAG_TEXT :
	case IPP_TAG_NAME :
	case IPP_TAG_KEYWORD :
	case IPP_TAG_URI :
	case IPP_TAG_URISCHEME :
	case IPP_TAG_CHARSET :
	case IPP_TAG_LANGUAGE :
	case IPP_TAG_MIMETYPE :
	    if (!strchr(token, ','))
	      attrptr = ippAddString(attrs, IPP_TAG_PRINTER, value, attr, NULL, token);
	    else
	    {
	     /*
	      * Multiple string values...
	      */

	      int	num_values;	/* Number of values */
	      char	*values[100],	/* Values */
			*ptr;		/* Pointer to next value */


	      values[0]  = token;
	      num_values = 1;

	      for (ptr = strchr(token, ','); ptr; ptr = strchr(ptr, ','))
	      {
		if (ptr > token && ptr[-1] == '\\')
		  _cups_strcpy(ptr - 1, ptr);
		else
		{
		  *ptr++ = '\0';
		  values[num_values] = ptr;
		  num_values ++;
		  if (num_values >= (int)(sizeof(values) / sizeof(values[0])))
		    break;
		}
	      }

	      attrptr = ippAddStrings(attrs, IPP_TAG_PRINTER, value, attr, num_values, NULL, (const char **)values);
	    }
	    break;
      }

      if (!attrptr)
      {
        fprintf(stderr, "ippserver: Unable to add attribute on line %d of \"%s\": %s\n", linenum, filename, cupsLastErrorString());
        exit(1);
      }
    }
    else
    {
      fprintf(stderr, "ippserver: Unknown directive \"%s\" on line %d of \"%s\".\n", token, linenum, filename);
      exit(1);
    }
  }

  fclose(fp);
}


/*
 * 'load_printer()' - Load a printer configuration file and create a printer.
 */

static server_printer_t	*		/* O - New printer or NULL */
load_printer(const char *conf,		/* I - Configuration file */
             const char *icon)		/* I - Icon file */
{
  (void)conf;
  (void)icon;

  return (NULL);
}


/*
 * 'load_system()' - Load the system configuration file.
 */

static int				/* O - 1 on success, 0 on failure */
load_system(const char *conf)		/* I - Configuration file */
{
  (void)conf;

  return (0);
}

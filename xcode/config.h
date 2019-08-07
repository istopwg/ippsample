/*
 * Configuration file for the IPP sample code.
 *
 * Copyright © 2014-2019 by the IEEE-ISTO Printer Working Group.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _CUPS_CONFIG_H_
#define _CUPS_CONFIG_H_

/*
 * Version of software...
 */

#define CUPS_SVERSION "IPPSAMPLE v1.0b1"
#define CUPS_MINIMAL "IPPSAMPLE/1.0b1"


/*
 * Default IPP port...
 */

#define CUPS_DEFAULT_IPP_PORT	631


/*
 * Do we have domain socket support, and if so what is the default one?
 */

#define CUPS_DEFAULT_DOMAINSOCKET "${prefix}/var/run/cupsd"


/*
 * Where are files stored?
 *
 * Note: These are defaults, which can be overridden by environment
 *       variables at run-time...
 */

#define CUPS_DATADIR "/usr/local/share/cups"
#define CUPS_LOCALEDIR "/usr/local/share/locale"
#define CUPS_SERVERBIN "/usr/local/bin"
#define CUPS_SERVERROOT "/usr/local/etc/cups"
#define CUPS_STATEDIR "/usr/local/etc/cups"


/*
 * Do we have posix_spawn?
 */

#define HAVE_POSIX_SPAWN 1


/*
 * Do we have ZLIB?
 */

#define HAVE_LIBZ 1
#define HAVE_INFLATECOPY 1


/*
 * Do we have PAM stuff?
 */

#define HAVE_LIBPAM 1
#define HAVE_SECURITY_PAM_APPL_H 1
/* #undef HAVE_PAM_PAM_APPL_H */
#define DEFAULT_PAM_SERVICE "cups"


/*
 * Use <stdint.h>?
 */

#define HAVE_STDINT_H 1


/*
 * Use <string.h>, <strings.h>, and/or <bstring.h>?
 */

#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
/* #undef HAVE_BSTRING_H */


/*
 * Do we have the long long type?
 */

#define HAVE_LONG_LONG 1

#ifdef HAVE_LONG_LONG
#  define CUPS_LLFMT	"%lld"
#  define CUPS_LLCAST	(long long)
#else
#  define CUPS_LLFMT	"%ld"
#  define CUPS_LLCAST	(long)
#endif /* HAVE_LONG_LONG */


/*
 * Do we have the strtoll() function?
 */

#define HAVE_STRTOLL 1

#ifndef HAVE_STRTOLL
#  define strtoll(nptr,endptr,base) strtol((nptr), (endptr), (base))
#endif /* !HAVE_STRTOLL */


/*
 * Do we have the strXXX() functions?
 */

#define HAVE_STRDUP 1
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1


/*
 * Do we have the (v)snprintf() functions?
 */

#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1


/*
 * What signal functions to use?
 */

#define HAVE_SIGSET 1
#define HAVE_SIGACTION 1


/*
 * What wait functions to use?
 */

#define HAVE_WAITPID 1
#define HAVE_WAIT3 1


/*
 * Do we have the langinfo.h header file?
 */

#define HAVE_LANGINFO_H 1


/*
 * Which encryption libraries do we have?
 */

#define HAVE_CDSASSL 1
/* #undef HAVE_GNUTLS */
/* #undef HAVE_SSPISSL */
#define HAVE_SSL 1


/*
 * Do we have the gnutls_fips140_set_mode function?
 */

/* #undef HAVE_GNUTLS_FIPS140_SET_MODE */


/*
 * Do we have the gnutls_transport_set_pull_timeout_function function?
 */

/* #undef HAVE_GNUTLS_TRANSPORT_SET_PULL_TIMEOUT_FUNCTION */


/*
 * Do we have the gnutls_priority_set_direct function?
 */

/* #undef HAVE_GNUTLS_PRIORITY_SET_DIRECT */


/*
 * What Security framework headers do we have?
 */

#define HAVE_SECCERTIFICATE_H 1
#define HAVE_SECITEM_H 1
#define HAVE_SECPOLICY_H 1


/*
 * Do we have the SecGenerateSelfSignedCertificate function?
 */

/* #undef HAVE_SECGENERATESELFSIGNEDCERTIFICATE */


/*
 * Do we have mDNSResponder for DNS Service Discovery (aka Bonjour)?
 */

#define HAVE_DNSSD 1


/*
 * Do we have Avahi for DNS Service Discovery (aka Bonjour)?
 */

/* #undef HAVE_AVAHI */


/*
 * Does the "stat" structure contain the "st_gen" member?
 */

#define HAVE_ST_GEN 1


/*
 * Does the "tm" structure contain the "tm_gmtoff" member?
 */

#define HAVE_TM_GMTOFF 1


/*
 * Do we have getaddrinfo()?
 */

#define HAVE_GETADDRINFO 1


/*
 * Do we have getnameinfo()?
 */

#define HAVE_GETNAMEINFO 1


/*
 * Do we have hstrerror()?
 */

#define HAVE_HSTRERROR 1


/*
 * Do we have res_init()?
 */

#define HAVE_RES_INIT 1


/*
 * Do we have <resolv.h>
 */

#define HAVE_RESOLV_H 1


/*
 * Do we have the <sys/sockio.h> header file?
 */

#define HAVE_SYS_SOCKIO_H 1


/*
 * Does the sockaddr structure contain an sa_len parameter?
 */

/* #undef HAVE_STRUCT_SOCKADDR_SA_LEN */


/*
 * Do we have pthread support?
 */

#define HAVE_PTHREAD_H 1


/*
 * Do we have CoreFoundation public headers?
 */

#define HAVE_COREFOUNDATION_H 1


/*
 * Do we have CoreGraphics?
 */

#define HAVE_COREGRAPHICS


/*
 * Do we have the MuPDF library?
 */

/* #undef HAVE_MUPDF */
/* #undef HAVE_FZ_MAKE_MATRIX */
/* #undef HAVE_FZ_NEW_PIXMAP_5_ARG */
/* #undef HAVE_FZ_CMM_ENGINE_LCMS */


/*
 * Select/poll interfaces...
 */

#define HAVE_POLL 1
/* #undef HAVE_EPOLL */
#define HAVE_KQUEUE 1


/*
 * Do we have <sys/param.h>?
 */

#define HAVE_SYS_PARAM_H 1


/*
 * Do we have <sys/ucred.h>?
 */

#define HAVE_SYS_UCRED_H 1


/*
 * Do we have removefile()?
 */

/* #undef HAVE_REMOVEFILE */


/*
 * Which random number generator function to use...
 */

#define HAVE_ARC4RANDOM 1
#define HAVE_RANDOM 1
#define HAVE_LRAND48 1

#ifdef HAVE_ARC4RANDOM
#  define CUPS_RAND() arc4random()
#  define CUPS_SRAND(v)
#elif defined(HAVE_RANDOM)
#  define CUPS_RAND() random()
#  define CUPS_SRAND(v) srandom(v)
#elif defined(HAVE_LRAND48)
#  define CUPS_RAND() lrand48()
#  define CUPS_SRAND(v) srand48(v)
#else
#  define CUPS_RAND() rand()
#  define CUPS_SRAND(v) srand(v)
#endif /* HAVE_ARC4RANDOM */


/*
 * Do we have <iconv.h>?
 */

#define HAVE_ICONV_H 1


/*
 * Do we have statfs or statvfs and one of the corresponding headers?
 */

#define HAVE_STATFS 1
#define HAVE_STATVFS 1
#define HAVE_SYS_MOUNT_H 1
/* #undef HAVE_SYS_STATFS_H */
#define HAVE_SYS_STATVFS_H 1
/* #undef HAVE_SYS_VFS_H */


/*
 * Location of macOS localization bundle, if any.
 */

/* #undef CUPS_BUNDLEDIR */


/*
 * Do we have the C99 abs() function?
 */

#define HAVE_ABS 1
#if !defined(HAVE_ABS) && !defined(abs)
#  if defined(__GNUC__) || __STDC_VERSION__ >= 199901L
#    define abs(x) _cups_abs(x)
static inline int _cups_abs(int i) { return (i < 0 ? -i : i); }
#  elif defined(_MSC_VER)
#    define abs(x) _cups_abs(x)
static __inline int _cups_abs(int i) { return (i < 0 ? -i : i); }
#  else
#    define abs(x) ((x) < 0 ? -(x) : (x))
#  endif /* __GNUC__ || __STDC_VERSION__ */
#endif /* !HAVE_ABS && !abs */


/*
 * Do we have the Cura software?
 */

#define CURAENGINE "/Applications/Ultimaker Cura.app/Contents/Resources/CuraEngine"


#endif /* !_CUPS_CONFIG_H_ */

//
// Visual Studio configuration file for the IPP sample code.
//
// Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef IPPSAMPLE_CONFIG_H
#define IPPSAMPLE_CONFIG_H


// Include standard headers first...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include <direct.h>


//
// Microsoft renames the POSIX functions to _name, and introduces
// a broken compatibility layer using the original names.  As a result,
// random crashes can occur when, for example, strdup() allocates memory
// from a different heap than used by malloc() and free().
//
// To avoid moronic problems like this, we #define the POSIX function
// names to the corresponding non-standard Microsoft names.
//

#define access		_access
#define close		_close
#define fileno		_fileno
#define lseek		_lseek
#define lstat		stat
#define mkdir(d,p)	_mkdir(d)
#define open		_open
#define read	        _read
#define rmdir		_rmdir
#define strdup		_strdup
#define unlink		_unlink
#define write		_write


// Microsoft "safe" functions use a different argument order than POSIX...
#define gmtime_r(t,tm)	gmtime_s(tm,t)
#define localtime_r(t,tm) localtime_s(tm,t)


// Map the POSIX strcasecmp() and strncasecmp() functions to the Win32
// _stricmp() and _strnicmp() functions...
#define strcasecmp	_stricmp
#define strncasecmp	_strnicmp


// Map the POSIX sleep() and usleep() functions to the Win32 Sleep() function...
typedef unsigned long useconds_t;
#define sleep(X)	Sleep(1000 * (X))
#define usleep(X)	Sleep((X)/1000)


// Map various parameters to Posix style system calls...
#  define F_OK		00
#  define W_OK		02
#  define R_OK		04
#  define X_OK		0

#  define O_RDONLY	_O_RDONLY
#  define O_WRONLY	_O_WRONLY
#  define O_CREAT	_O_CREAT
#  define O_TRUNC	_O_TRUNC
#  define O_CLOEXEC	0
#  define O_NOFOLLOW	0

#  define S_ISDIR(m)	((m) & _S_IFDIR)
#  define S_ISREG(m)	(!((m) & _S_IFDIR))


// Compiler stuff...
#undef const
#undef __CHAR_UNSIGNED__


// Version number
#define IPPSAMPLE_VERSION "1.0b1"


// PAM support
/* #undef HAVE_LIBPAM */
/* #undef HAVE_SECURITY_PAM_APPL_H */
/* #undef HAVE_PAM_PAM_APPL_H */


// strlcpy support
/* #undef HAVE_STRLCPY */


// DNS-SD support
#define HAVE_DNSSD 1
#define HAVE_MDNSRESPONDER 1
/* #undef HAVE_AVAHI */


// CoreGraphics support
/* #undef HAVE_COREGRAPHICS */


// pdftoppm path
/* #undef PDFTOPPM */


// CuraEngine path
/* #undef CURAENGINE */


#endif // !IPPSAMPLE_CONFIG_H

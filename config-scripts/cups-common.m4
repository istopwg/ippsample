dnl
dnl Common configuration stuff for CUPS.
dnl
dnl Copyright © 2021 by OpenPrinting.
dnl Copyright © 2007-2019 by Apple Inc.
dnl Copyright © 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Set the name of the config header file...
AC_CONFIG_HEADERS([config.h])

dnl Version number information...
CUPS_VERSION="AC_PACKAGE_VERSION"
CUPS_API_VERSION="$(echo AC_PACKAGE_VERSION | awk -F. '{print $1 "." $2}')"
CUPS_BUILD="cups-$CUPS_VERSION"

AC_ARG_WITH([cups_build], AS_HELP_STRING([--with-cups-build], [set "pkg-config --variable=build" string]), [
    CUPS_BUILD="$withval"
])

AC_SUBST([CUPS_API_VERSION])
AC_SUBST([CUPS_BUILD])
AC_SUBST([CUPS_VERSION])
AC_DEFINE_UNQUOTED([CUPS_SVERSION], ["AC_PACKAGE_NAME v$CUPS_VERSION"], [Version number])
AC_DEFINE_UNQUOTED([CUPS_MINIMAL], ["AC_PACKAGE_NAME/$CUPS_VERSION"], [Version for HTTP headers])

dnl Default compiler flags...
CFLAGS="${CFLAGS:=}"
CPPFLAGS="${CPPFLAGS:=}"
CXXFLAGS="${CXXFLAGS:=}"
LDFLAGS="${LDFLAGS:=}"

dnl Checks for programs...
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_RANLIB
AC_PATH_PROG([AR], [ar])
AC_PATH_PROG([CHMOD], [chmod])
AC_PATH_PROG([GZIPPROG], [gzip])
AC_MSG_CHECKING([for install-sh script])
INSTALL="`pwd`/install-sh"
AC_SUBST([INSTALL])
AC_MSG_RESULT([using $INSTALL])
AC_PATH_PROG([LD], [ld])
AC_PATH_PROG([LN], [ln])
AC_PATH_PROG([MKDIR], [mkdir])
AC_PATH_PROG([MV], [mv])
AC_PATH_PROG([RM], [rm])
AC_PATH_PROG([RMDIR], [rmdir])
AC_PATH_PROG([SED], [sed])

AS_IF([test "x$AR" = x], [
    AC_MSG_ERROR([Unable to find required library archive command.])
])
AS_IF([test "x$CC" = x], [
    AC_MSG_ERROR([Unable to find required C compiler command.])
])

dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_TOOL([PKGCONFIG], [pkg-config])

dnl Check for libraries...
AC_SEARCH_LIBS([abs], [m], [AC_DEFINE(HAVE_ABS)])
AC_SEARCH_LIBS([fmod], [m])

dnl Checks for header files.
AC_CHECK_HEADER([langinfo.h], AC_DEFINE([HAVE_LANGINFO_H], [1], [Have <langinfo.h> header?]))
AC_CHECK_HEADER([malloc.h], AC_DEFINE([HAVE_MALLOC_H], [1], [Have <malloc.h> header?]))
AC_CHECK_HEADER([shadow.h], AC_DEFINE([HAVE_SHADOW_H], [1], [Have <shadow.h> header?]))
AC_CHECK_HEADER([stdint.h], AC_DEFINE([HAVE_STDINT_H], [1], [Have <stdint.h> header?]))
AC_CHECK_HEADER([sys/ioctl.h], AC_DEFINE([HAVE_SYS_IOCTL_H], [1], [Have <sys/ioctl.h> header?]))
AC_CHECK_HEADER([sys/param.h], AC_DEFINE([HAVE_SYS_PARAM_H], [1], [Have <sys/param.h> header?]))
AC_CHECK_HEADER([sys/ucred.h], AC_DEFINE([HAVE_SYS_UCRED_H], [1], [Have <sys/ucred.h> header?]))

dnl Checks for iconv.h and iconv_open
AC_CHECK_HEADER([iconv.h], [
    SAVELIBS="$LIBS"
    LIBS=""
    AC_SEARCH_LIBS([iconv_open], [iconv], [
        AC_DEFINE([HAVE_ICONV_H], [1], [Have <iconv.h> header?])
	SAVELIBS="$SAVELIBS $LIBS"
    ])
    AC_SEARCH_LIBS([libiconv_open], [iconv], [
	AC_DEFINE([HAVE_ICONV_H], [1], [Have <iconv.h> header?])
	SAVELIBS="$SAVELIBS $LIBS"
    ])
    LIBS="$SAVELIBS"
])

dnl Checks for statfs and its many headers...
AC_CHECK_HEADER([sys/mount.h], AC_DEFINE([HAVE_SYS_MOUNT_H], [1], [Have <sys/mount.h> header?]))
AC_CHECK_HEADER([sys/statfs.h], AC_DEFINE([HAVE_SYS_STATFS_H], [1], [Have <sys/statfs.h> header?]))
AC_CHECK_HEADER([sys/statvfs.h], AC_DEFINE([HAVE_SYS_STATVFS_H], [1], [Have <sys/statvfs.h> header?]))
AC_CHECK_HEADER([sys/vfs.h], AC_DEFINE([HAVE_SYS_VFS_H], [1], [Have <sys/vfs.h> header?]))
AC_CHECK_FUNCS([statfs statvfs])

dnl Checks for string functions.
dnl TODO: Remove strdup, snprintf, and vsnprintf checks since they are C99?
AC_CHECK_FUNCS([strdup snprintf vsnprintf])
AC_CHECK_FUNCS([strlcat strlcpy])

dnl Check for random number functions...
AC_CHECK_FUNCS([random lrand48 arc4random])

dnl Check for geteuid function.
AC_CHECK_FUNCS([geteuid])

dnl Check for setpgid function.
AC_CHECK_FUNCS([setpgid])

dnl Check for vsyslog function.
AC_CHECK_FUNCS([vsyslog])

dnl Checks for signal functions.
AS_CASE(["$host_os_name"], [linux* | gnu*], [
    # Do not use sigset on Linux or GNU HURD
], [*], [
    # Use sigset on other platforms, if available
    AC_CHECK_FUNCS([sigset])
])

AC_CHECK_FUNCS([sigaction])

dnl Checks for wait functions.
AC_CHECK_FUNCS([waitpid wait3])

dnl Check for posix_spawn
AC_CHECK_FUNCS([posix_spawn])

dnl Check for getgrouplist
AC_CHECK_FUNCS([getgrouplist])

dnl See if the tm structure has the tm_gmtoff member...
AC_MSG_CHECKING([for tm_gmtoff member in tm structure])
AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[#include <time.h>]], [[
        struct tm t;
	int o = t.tm_gmtoff;
    ]])
], [
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_TM_GMTOFF], [1], [Have tm_gmtoff member in struct tm?])
], [
    AC_MSG_RESULT([no])
])

dnl See if the stat structure has the st_gen member...
AC_MSG_CHECKING([for st_gen member in stat structure])
AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[#include <sys/stat.h>]], [[
        struct stat t;
	int o = t.st_gen;
    ]])
], [
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_ST_GEN], [1], [Have st_gen member in struct stat?])
], [
    AC_MSG_RESULT([no])
])

dnl See if we have the removefile(3) function for securely removing files
AC_CHECK_FUNCS([removefile])

dnl ZLIB
INSTALL_GZIP=""
LIBZ=""
AC_CHECK_HEADER([zlib.h], [
    AC_CHECK_LIB([z], [gzgets], [
	AC_DEFINE([HAVE_LIBZ], [1], [Have zlib library?])
	LIBZ="-lz"
	LIBS="$LIBS -lz"
	AC_CHECK_LIB([z], [inflateCopy], [
	    AC_DEFINE([HAVE_INFLATECOPY], [1], [Have inflateCopy function?])
	])
	AS_IF([test "x$GZIPPROG" != x], [
	    INSTALL_GZIP="-z"
	])
    ])
])
AC_SUBST([INSTALL_GZIP])
AC_SUBST([LIBZ])

PKGCONFIG_LIBS_STATIC="$PKGCONFIG_LIBS_STATIC $LIBZ"

dnl Flags for "ar" command...
AS_CASE([host_os_name], [darwin* | *bsd*], [
    ARFLAGS="-rcv"
], [*], [
    ARFLAGS="crvs"
])
AC_SUBST([ARFLAGS])

dnl Extra platform-specific libraries...
AS_CASE([$host_os_name], [darwin*], [
    LIBS="-framework CoreFoundation -framework Security $LIBS"

    dnl Check for framework headers...
    AC_CHECK_HEADER([ApplicationServices/ApplicationServices.h], [
        AC_DEFINE([HAVE_APPLICATIONSERVICES_H], [1], [Have <ApplicationServices/ApplicationServices.h>?])
    ])
    AC_CHECK_HEADER([CoreFoundation/CoreFoundation.h], [
        AC_DEFINE([HAVE_COREFOUNDATION_H], [1], [Have <CoreFoundation/CoreFoundation.h>?])
    ])

    dnl Check for dynamic store function...
    SAVELIBS="$LIBS"
    LIBS="-framework SystemConfiguration $LIBS"
    AC_CHECK_FUNCS([SCDynamicStoreCopyComputerName], [
	AC_DEFINE([HAVE_SCDYNAMICSTORECOPYCOMPUTERNAME], [1], [Have SCDynamicStoreCopyComputerName function?])
    ],[
	LIBS="$SAVELIBS"
    ])
])

dnl Transform utility...
IPPTRANSFORM_BIN=""
IPPTRANSFORM_HTML=""
IPPTRANSFORM_MAN=""
AC_SUBST([IPPTRANSFORM_BIN])
AC_SUBST([IPPTRANSFORM_HTML])
AC_SUBST([IPPTRANSFORM_MAN])

AC_ARG_WITH([pdfrip], AS_HELP_STRING([--with-pdfrip=...], [set PDF RIP to use (auto, coregraphics, mupdf, none)]))

AS_IF([test "x$with_pdfrip" = x -o "x$with_pdfrip" = xauto], [
    AS_CASE([$host_os_name], [darwin*], [
	use_pdfrip=coregraphics
    ], [*], [
	use_pdfrip=mupdf
    ])
], [
    use_pdfrip="$with_pdfrip"
])

AS_CASE(["$use_pdfrip"], [coregraphics], [
    SAVELIBS="$LIBS"
    LIBS="-framework CoreGraphics -framework ImageIO $LIBS"
    AC_CHECK_HEADER([CoreGraphics/CoreGraphics.h], [
	AC_MSG_RESULT([    Using CoreGraphics for PDF RIP])

	AC_DEFINE([HAVE_COREGRAPHICS], [1], [Have CoreGraphics APIs?])
	IPPTRANSFORM_BIN="ipptransform"
	IPPTRANSFORM_HTML="ipptransform.html"
	IPPTRANSFORM_MAN="ipptransform.7"
    ], [
	LIBS="$SAVELIBS"
	AS_IF([test "x$with_pdfrip" = xcoregraphics], [
	    AC_MSG_ERROR([Unable to enable PDF RIP support.])
	], [
	    AC_MSG_RESULT([    PDF RIP is not available.])
	])
    ])
], [mupdf], [
    AC_SEARCH_LIBS([FT_Init_FreeType], [mupdf-third mupdfthird freetype])
    AC_SEARCH_LIBS([jpeg_std_error], [mupdf-third mupdfthird jpeg])
    AC_SEARCH_LIBS([jbig2_ctx_new], [mupdf-third mupdfthird jbig2dec])
    # Need to find mupdf-third symbol that doesn't have dependencies on libmupdf
    AC_SEARCH_LIBS([opj_set_info_handler], [mupdf-third mupdfthird openjp2])
    # Need to find mupdf-third symbol that doesn't have dependencies on libmupdf
    AC_SEARCH_LIBS([hb_tag_from_string], [mupdf-third mupdfthird harfbuzz])
    AC_SEARCH_LIBS([js_pop], [mupdf-third mupdfthird])
    AC_CHECK_LIB([mupdf], [fz_drop_document], [
	AC_MSG_RESULT([    Using MuPDF for PDF RIP])

	AC_DEFINE([HAVE_MUPDF], [1], [Have MuPDF APIs?])
	LIBS="-lmupdf $LIBS"
	IPPTRANSFORM_BIN="ipptransform"
	IPPTRANSFORM_HTML="ipptransform.html"
	IPPTRANSFORM_MAN="ipptransform.7"

	AC_MSG_CHECKING([for version of fz_new_pixmap function])
	AC_COMPILE_IFELSE([
	    AC_LANG_PROGRAM([[#include <mupdf/fitz.h>]], [[
		fz_pixmap *p = fz_new_pixmap(0,0,100,100,1);
	    ]])
	], [
	    AC_MSG_RESULT([5 argument])
	    AC_DEFINE([HAVE_FZ_NEW_PIXMAP_5_ARG], [1], [Have five argument fz_new_pixmap()?])
	], [
	    AC_MSG_RESULT([6 argument])
	])

	AC_MSG_CHECKING([for fz_make_matrix function])
	AC_COMPILE_IFELSE([
	    AC_LANG_PROGRAM([[#include <mupdf/fitz.h>]], [[
		fz_matrix m = fz_make_matrix(0.0f,1.0f,1.0f,0.0f,0.0f,0.0f);
	    ]])
	], [
	    AC_MSG_RESULT([yes])
	    AC_DEFINE([HAVE_FZ_MAKE_MATRIX], [1], [Have fz_make_matrix()?])
	], [
	    AC_MSG_RESULT([no])
	])

	AC_MSG_CHECKING([whether MuPDF has ICC support])
	AC_LINK_IFELSE([
	    AC_LANG_PROGRAM([[#include <mupdf/fitz.h>]], [[
		fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
		fz_set_cmm_engine(ctx, &fz_cmm_engine_lcms);
	    ]])
	], [
	    AC_MSG_RESULT([yes])
	    AC_DEFINE([HAVE_FZ_CMM_ENGINE_LCMS], [1], [Does MuPDF have ICC support?])
	], [
	    AC_MSG_RESULT([no])
	])
    ], [
        AS_IF([test "x$with_pdfrip" = xmupdf], [
	    AC_MSG_ERROR([Unable to enable PDF RIP support.])
	], [
	    AC_MSG_RESULT([    PDF RIP is not available.])
	])
    ])
], [none], [
    AC_MSG_RESULT([    PDF RIP is disabled.])
], [*], [
    AC_MSG_ERROR([Unknown --with-pdfrip value.])
])

# 3D support
IPPTRANSFORM3D_BIN=""
IPPTRANSFORM3D_HTML=""
IPPTRANSFORM3D_MAN=""
AC_SUBST([IPPTRANSFORM3D_BIN])
AC_SUBST([IPPTRANSFORM3D_HTML])
AC_SUBST([IPPTRANSFORM3D_MAN])

SAVEPATH="$PATH"
PATH="$PATH:/Applications/Ultimaker Cura.app/Contents/MacOS:/Applications/Cura/Cura.app/Contents/Resources:/Applications/Cura.app/Contents/MacOS"
AC_PATH_PROG([CURAENGINE], [CuraEngine])
PATH="$SAVEPATH"

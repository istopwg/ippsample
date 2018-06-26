dnl
dnl Common configuration stuff for CUPS.
dnl
dnl Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group.
dnl Copyright © 2007-2018 by Apple Inc.
dnl Copyright © 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Set the name of the config header file...
AC_CONFIG_HEADER(config.h)

dnl Version number information...
CUPS_VERSION="AC_PACKAGE_VERSION"
CUPS_REVISION=""

AC_SUBST(CUPS_VERSION)
AC_SUBST(CUPS_REVISION)
AC_DEFINE_UNQUOTED(CUPS_SVERSION, "AC_PACKAGE_NAME v$CUPS_VERSION$CUPS_REVISION")
AC_DEFINE_UNQUOTED(CUPS_MINIMAL, "AC_PACKAGE_NAME/$CUPS_VERSION$CUPS_REVISION")

dnl Default compiler flags...
CFLAGS="${CFLAGS:=}"
CPPFLAGS="${CPPFLAGS:=}"
CXXFLAGS="${CXXFLAGS:=}"
LDFLAGS="${LDFLAGS:=}"

dnl Checks for programs...
AC_PROG_AWK
AC_PROG_CC(clang cc gcc)
AC_PROG_CPP
AC_PROG_RANLIB
AC_PATH_PROG(AR,ar)
AC_PATH_PROG(CHMOD,chmod)
AC_PATH_PROG(GZIP,gzip)
AC_PATH_PROG(RM,rm)
AC_PATH_PROG(RMDIR,rmdir)
AC_PATH_PROG(SED,sed)

if test "x$AR" = x; then
	AC_MSG_ERROR([Unable to find required library archive command.])
fi
if test "x$CC" = x; then
	AC_MSG_ERROR([Unable to find required C compiler command.])
fi

AC_MSG_CHECKING(for install-sh script)
INSTALL="`pwd`/install-sh"
AC_SUBST(INSTALL)
AC_MSG_RESULT(using $INSTALL)

dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_TOOL(PKGCONFIG, pkg-config)

dnl Check for libraries...
AC_SEARCH_LIBS(abs, m, AC_DEFINE(HAVE_ABS))
AC_SEARCH_LIBS(fmod, m)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER(stdlib.h,AC_DEFINE(HAVE_STDLIB_H))
AC_CHECK_HEADER(langinfo.h,AC_DEFINE(HAVE_LANGINFO_H))
AC_CHECK_HEADER(malloc.h,AC_DEFINE(HAVE_MALLOC_H))
AC_CHECK_HEADER(stdint.h,AC_DEFINE(HAVE_STDINT_H))
AC_CHECK_HEADER(string.h,AC_DEFINE(HAVE_STRING_H))
AC_CHECK_HEADER(strings.h,AC_DEFINE(HAVE_STRINGS_H))
AC_CHECK_HEADER(bstring.h,AC_DEFINE(HAVE_BSTRING_H))
AC_CHECK_HEADER(sys/ioctl.h,AC_DEFINE(HAVE_SYS_IOCTL_H))
AC_CHECK_HEADER(sys/param.h,AC_DEFINE(HAVE_SYS_PARAM_H))
AC_CHECK_HEADER(sys/ucred.h,AC_DEFINE(HAVE_SYS_UCRED_H))

dnl Checks for iconv.h and iconv_open
AC_CHECK_HEADER(iconv.h,
	SAVELIBS="$LIBS"
	LIBS=""
	AC_SEARCH_LIBS(iconv_open,iconv,
		AC_DEFINE(HAVE_ICONV_H)
		SAVELIBS="$SAVELIBS $LIBS")
	AC_SEARCH_LIBS(libiconv_open,iconv,
		AC_DEFINE(HAVE_ICONV_H)
		SAVELIBS="$SAVELIBS $LIBS")
	LIBS="$SAVELIBS")

dnl Checks for statfs and its many headers...
AC_CHECK_HEADER(sys/mount.h,AC_DEFINE(HAVE_SYS_MOUNT_H))
AC_CHECK_HEADER(sys/statfs.h,AC_DEFINE(HAVE_SYS_STATFS_H))
AC_CHECK_HEADER(sys/statvfs.h,AC_DEFINE(HAVE_SYS_STATVFS_H))
AC_CHECK_HEADER(sys/vfs.h,AC_DEFINE(HAVE_SYS_VFS_H))
AC_CHECK_FUNCS(statfs statvfs)

dnl Checks for string functions.
AC_CHECK_FUNCS(strdup strlcat strlcpy)
if test "$uname" = "HP-UX" -a "$uversion" = "1020"; then
	echo Forcing snprintf emulation for HP-UX.
else
	AC_CHECK_FUNCS(snprintf vsnprintf)
fi

dnl Check for random number functions...
AC_CHECK_FUNCS(random lrand48 arc4random)

dnl Checks for signal functions.
case "$uname" in
	Linux | GNU)
		# Do not use sigset on Linux or GNU HURD
		;;
	*)
		# Use sigset on other platforms, if available
		AC_CHECK_FUNCS(sigset)
		;;
esac

AC_CHECK_FUNCS(sigaction)

dnl Checks for wait functions.
AC_CHECK_FUNCS(waitpid wait3)

dnl Check for posix_spawn
AC_CHECK_FUNCS(posix_spawn)

dnl See if the tm structure has the tm_gmtoff member...
AC_MSG_CHECKING(for tm_gmtoff member in tm structure)
AC_TRY_COMPILE([#include <time.h>],[struct tm t;
	int o = t.tm_gmtoff;],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TM_GMTOFF),
	AC_MSG_RESULT(no))

dnl See if the stat structure has the st_gen member...
AC_MSG_CHECKING(for st_gen member in stat structure)
AC_TRY_COMPILE([#include <sys/stat.h>],[struct stat t;
	int o = t.st_gen;],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_ST_GEN),
	AC_MSG_RESULT(no))

dnl ZLIB
INSTALL_GZIP=""
LIBZ=""
AC_CHECK_HEADER(zlib.h,
    AC_CHECK_LIB(z, gzgets,
	AC_DEFINE(HAVE_LIBZ)
	LIBZ="-lz"
	LIBS="$LIBS -lz"
	AC_CHECK_LIB(z, inflateCopy, AC_DEFINE(HAVE_INFLATECOPY))
	if test "x$GZIP" != z; then
		INSTALL_GZIP="-z"
	fi))
AC_SUBST(INSTALL_GZIP)
AC_SUBST(LIBZ)

dnl Flags for "ar" command...
case $uname in
        Darwin* | *BSD*)
                ARFLAGS="-rcv"
                ;;
        *)
                ARFLAGS="crvs"
                ;;
esac

AC_SUBST(ARFLAGS)

dnl Extra platform-specific libraries...
if test "$uname" = Darwin; then
	LIBS="-framework SystemConfiguration -framework CoreFoundation -framework Security $LIBS"

	dnl Check for framework headers...
	AC_CHECK_HEADER(CoreFoundation/CoreFoundation.h,AC_DEFINE(HAVE_COREFOUNDATION_H))
	AC_CHECK_HEADER(CoreFoundation/CFPriv.h,AC_DEFINE(HAVE_CFPRIV_H))
	AC_CHECK_HEADER(CoreFoundation/CFBundlePriv.h,AC_DEFINE(HAVE_CFBUNDLEPRIV_H))
fi

dnl Transform utility...
IPPTRANSFORM_BIN=""
IPPTRANSFORM_HTML=""
IPPTRANSFORM_MAN=""
AC_SUBST(IPPTRANSFORM_BIN)
AC_SUBST(IPPTRANSFORM_HTML)
AC_SUBST(IPPTRANSFORM_MAN)

AC_ARG_WITH(pdfrip, [  --with-pdfrip=...        set PDF RIP to use (auto, coregraphics, mupdf, none)])

if test "x$with_pdfrip" = x -o "x$with_pdfrip" = xauto; then
	case $uname in
		Darwin*)
			use_pdfrip=coregraphics
			;;
		*)
			use_pdfrip=mupdf
			;;
	esac
else
	use_pdfrip="$with_pdfrip"
fi

case "$use_pdfrip" in
	coregraphics)
		SAVELIBS="$LIBS"
		LIBS="-framework CoreGraphics -framework ImageIO $LIBS"
		AC_CHECK_HEADER(CoreGraphics/CoreGraphics.h,[
			AC_MSG_RESULT([    Using CoreGraphics for PDF RIP])

			AC_DEFINE(HAVE_COREGRAPHICS)
			IPPTRANSFORM_BIN="ipptransform"
			IPPTRANSFORM_HTML="ipptransform.html"
			IPPTRANSFORM_MAN="ipptransform.1"
		],[
			LIBS="$SAVELIBS"
			if test "x$with_pdfrip" = xcoregraphics; then
				AC_MSG_ERROR([Unable to enable PDF RIP support.])
			else
				AC_MSG_RESULT([    PDF RIP is not available.])
			fi
		])
		;;

	mupdf)
		AC_SEARCH_LIBS(FT_Init_FreeType, mupdfthird freetype)
		AC_SEARCH_LIBS(jpeg_destroy_decompress, mupdfthird jpeg)
		AC_SEARCH_LIBS(jbig2_ctx_new, mupdfthird jbig2dec)
		AC_SEARCH_LIBS(opj_create_decompress, mupdfthird openjp2)
		AC_SEARCH_LIBS(js_getcontext, mupdfthird)
		AC_SEARCH_LIBS(hb_buffer_create, mupdfthird harfbuzz)
		AC_CHECK_LIB(mupdf, fz_drop_document,[
			AC_MSG_RESULT([    Using MuPDF for PDF RIP])

			AC_DEFINE(HAVE_MUPDF)
			LIBS="-lmupdf $LIBS"
			IPPTRANSFORM_BIN="ipptransform"
			IPPTRANSFORM_HTML="ipptransform.html"
			IPPTRANSFORM_MAN="ipptransform.1"

                        AC_MSG_CHECKING(for version of fz_new_pixmap function)
                        AC_TRY_COMPILE([#include <mupdf/fitz.h>],[
                                fz_pixmap *p = fz_new_pixmap(0,0,100,100,1);],
                             	[AC_MSG_RESULT(5 argument)
	                         AC_DEFINE(HAVE_FZ_NEW_PIXMAP_5_ARG)],
	                        [AC_MSG_RESULT(6 argument)])

                        AC_MSG_CHECKING(for fz_make_matrix function)
                        AC_TRY_COMPILE([#include <mupdf/fitz.h>],[
                                fz_matrix m = fz_make_matrix(0.0f,1.0f,1.0f,0.0f,0.0f,0.0f);],
                             	[AC_MSG_RESULT(yes)
	                         AC_DEFINE(HAVE_FZ_MAKE_MATRIX)],
	                        [AC_MSG_RESULT(no)])

                        AC_MSG_CHECKING(whether MuPDF has ICC support)
                        AC_TRY_LINK([#include <mupdf/fitz.h>],[
                                fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
                                fz_set_cmm_engine(ctx, &fz_cmm_engine_lcms);],
                             	[AC_MSG_RESULT(yes)
	                         AC_DEFINE(HAVE_FZ_CMM_ENGINE_LCMS)],
	                        [AC_MSG_RESULT(no)])
		],[
			if test "x$with_pdfrip" = xmupdf; then
				AC_MSG_ERROR([Unable to enable PDF RIP support.])
			else
				AC_MSG_RESULT([    PDF RIP is not available.])
			fi
		])
		;;

	none)
		AC_MSG_RESULT([    PDF RIP is disabled.])
		;;

	*)
		AC_MSG_ERROR([Unknown --with-pdfrip value.])
		;;
esac

# 3D support
IPPTRANSFORM3D_BIN=""
IPPTRANSFORM3D_HTML=""
IPPTRANSFORM3D_MAN=""
AC_SUBST(IPPTRANSFORM3D_BIN)
AC_SUBST(IPPTRANSFORM3D_HTML)
AC_SUBST(IPPTRANSFORM3D_MAN)

SAVEPATH="$PATH"
PATH="$PATH:/Applications/Cura/Cura.app/Contents/Resources:/Applications/Cura.app/Contents/MacOS"
AC_PATH_PROG(CURAENGINE,CuraEngine)
PATH="$SAVEPATH"

if test "x$CURAENGINE" != x; then
	IPPTRANSFORM3D_BIN="ipptransform3d"
	IPPTRANSFORM3D_HTML="ipptransform3d.html"
	IPPTRANSFORM3D_MAN="ipptransform3d.1"

	AC_DEFINE_UNQUOTED(CURAENGINE, "$CURAENGINE")
fi

dnl
dnl Configuration script for the IPP sample code.
dnl
dnl Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl
dnl ***********************************************************************
dnl
dnl Note: Using autoheader or automake on this project will break the build
dnl system.  Use "autoconf -f" to regenerate the configure script if you
dnl make changes to this file.
dnl
dnl ***********************************************************************

dnl We need at least autoconf 2.70 for --runstatedir...
AC_PREREQ([2.70])


dnl Package name and version...
AC_INIT([IPPSAMPLE], [1.0b1], [https://github.com/istopwg/ippsample/issues], [ippsample], [http://istopwg.github.io/ippsample])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SUBDIRS([libcups])

IPPSAMPLE_VERSION="AC_PACKAGE_VERSION"

AC_SUBST([IPPSAMPLE_VERSION])

AC_DEFINE_UNQUOTED([IPPSAMPLE_VERSION], ["$IPPSAMPLE_VERSION"], [Version number])


dnl This line is provided to ensure that you don't run the autoheader program
dnl against this project.  Doing so is completely unsupported and WILL cause
dnl problems!
AH_TOP([#error "Somebody ran autoheader on this project which is unsupported and WILL cause problems."])


dnl Get the build and host platforms and split the host_os value
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

[host_os_name="$(echo $host_os | sed -e '1,$s/[0-9.]*$//g')"]
[host_os_version="$(echo $host_os | sed -e '1,$s/^[^0-9.]*//g' | awk -F. '{print $1 $2}')"]
# Linux often does not yield an OS version we can use...
AS_IF([test "x$host_os_version" = x], [
    host_os_version="0"
])


dnl Compiler options...
CFLAGS="${CFLAGS:=}"
CPPFLAGS="${CPPFLAGS:=}"
LDFLAGS="${LDFLAGS:=}"
LIBS="${LIBS:=}"
OPTIM="${OPTIM:=}"

AC_SUBST([OPTIM])


dnl Programs...
AC_PROG_CC
AC_PATH_PROGS([CODE_SIGN], [codesign true])
AC_PATH_PROG([MKDIR], [mkdir])
AC_PATH_PROG([RM], [rm])
AC_PATH_PROG([RMDIR], [rmdir])
AC_PATH_PROG([LN], [ln])


dnl PDF support
dnl Transform utility...
IPPTRANSFORM_BIN=""
IPPTRANSFORM_HTML=""
IPPTRANSFORM_MAN=""
AC_SUBST([IPPTRANSFORM_BIN])
AC_SUBST([IPPTRANSFORM_HTML])
AC_SUBST([IPPTRANSFORM_MAN])

AC_ARG_WITH([pdfrip], AS_HELP_STRING([--with-pdfrip=...], [set PDF RIP to use (auto, coregraphics, pdftoppm, none)]))

AS_IF([test "x$with_pdfrip" = x -o "x$with_pdfrip" = xauto], [
    AS_CASE([$host_os_name], [darwin*], [
	use_pdfrip=coregraphics
    ], [*], [
	use_pdfrip=pdftoppm
    ])
], [
    use_pdfrip="$with_pdfrip"
])

AS_CASE(["$use_pdfrip"], [coregraphics], [
    SAVELIBS="$LIBS"
    LIBS="-framework CoreGraphics -framework ImageIO $LIBS"
    AC_CHECK_HEADER([CoreGraphics/CoreGraphics.h], [
	AC_DEFINE([HAVE_COREGRAPHICS], [1], [CoreGraphics support])
	IPPTRANSFORM_BIN="ipptransform"
	IPPTRANSFORM_HTML="ipptransform.html"
	IPPTRANSFORM_MAN="ipptransform.7"
    ], [
	LIBS="$SAVELIBS"
	AS_IF([test "x$with_pdfrip" = xcoregraphics], [
	    AC_MSG_ERROR([Unable to find CoreGraphic framework.])
	])
    ])
], [pdftoppm], [
    AC_PATH_PROG([PDFTOPPM], [pdftoppm])

    AS_IF([test "x$PDFTOPPM" != x], [
	AC_DEFINE_UNQUOTED([PDFTOPPM], ["$PDFTOPPM"], [pdftoppm path])
    ], [test "x$with_pdfrip" = xpdftoppm], [
	AC_MSG_ERROR([Unable to find pdftoppm program.])
    ])
], [none], [
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
PATH="$PATH:/Applications/Ultimaker Cura.app/Contents/MacOS"
AC_PATH_PROG([CURAENGINE], [CuraEngine])
PATH="$SAVEPATH"

AS_IF([test "x$CURAENGINE" != x], [
    AC_DEFINE_UNQUOTED([CURAENGINE], ["$CURAENGINE"], [CuraEngine path])
])


dnl install-sh
AC_MSG_CHECKING([for install-sh script])
INSTALL="$(pwd)/install-sh"
AC_SUBST([INSTALL])
AC_MSG_RESULT([using $INSTALL])


dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_TOOL([PKGCONFIG], [pkg-config])


dnl String functions...
AC_CHECK_FUNCS([strlcpy])


dnl DNS-SD support...
AC_ARG_WITH([dnssd], AS_HELP_STRING([--with-dnssd=LIBRARY], [set DNS-SD library (auto, avahi, mdnsresponder)]))

AS_IF([test "x$with_dnssd" != xmdnsresponder -a "x$with_dnssd" != xno -a "x$PKGCONFIG" != x -a x$host_os_name != xdarwin], [
    AC_MSG_CHECKING([for Avahi])
    AS_IF([$PKGCONFIG --exists avahi-client], [
	AC_MSG_RESULT([yes])
	CPPFLAGS="$CPPFLAGS $($PKGCONFIG --cflags avahi-client)"
	LIBS="$LIBS $($PKGCONFIG --libs avahi-client)"
	AC_DEFINE([HAVE_DNSSD], 1, [Have DNS-SD support?])
	AC_DEFINE([HAVE_AVAHI], 1, [Have Avahi?])
    ], [
	AC_MSG_RESULT([no])
	AS_IF([test x$with_dnssd = xavahi],
	    AC_MSG_ERROR([libavahi-client-dev needed for --with-dnssd=avahi.]))
    ])
], [test x$with_dnssd = xavahi], [
    AC_MSG_ERROR([pkgconfig and libavahi-client-dev needed for --with-dnssd=avahi.])
], [test x$with_dnssd != xavahi -a "x$with_dnssd" != xno], [
    AC_CHECK_HEADER(dns_sd.h, [
        AS_CASE(["$host_os_name"],
	[darwin*], [
	    # Darwin and macOS...
	    AC_DEFINE([HAVE_DNSSD], 1, [Have DNS-SD support?])
	    AC_DEFINE([HAVE_MDNSRESPONDER], 1, [Have mDNSResponder?)])
	    LIBS="$LIBS -framework CoreFoundation -framework SystemConfiguration"
	], [*], [
	    # All others...
	    AC_MSG_CHECKING(for current version of dns_sd library)
	    SAVELIBS="$LIBS"
	    LIBS="$LIBS -ldns_sd"
	    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <dns_sd.h>]], [[
		int constant = kDNSServiceFlagsShareConnection;
		unsigned char txtRecord[100];
		uint8_t valueLen;
		TXTRecordGetValuePtr(sizeof(txtRecord), txtRecord, "value", &valueLen);]])
	    ],[
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_DNSSD], 1, [Have DNS-SD support?])
		AC_DEFINE([HAVE_MDNSRESPONDER], 1, [Have mDNSResponder?)])
	    ],[
		AC_MSG_RESULT([no])
		LIBS="$SAVELIBS"
		AS_IF([test x$with_dnssd = xmdnsresponder],
		    AC_MSG_ERROR([mDNSResponder required for --with-dnssd=mdnsresponder.]))
	    ])
	])
    ])
])


dnl libjpeg...
AC_ARG_ENABLE([libjpeg], AS_HELP_STRING([--enable-libjpeg], [use libjpeg for JPEG printing, default=auto]))

AS_IF([test x$enable_libjpeg != xno], [
    have_jpeg=no

    AS_IF([test "x$PKGCONFIG" != x], [
	AC_MSG_CHECKING([for libjpeg via pkg-config])
	AS_IF([$PKGCONFIG --exists libjpeg], [
	    AC_MSG_RESULT([yes]);
	    AC_DEFINE([HAVE_LIBJPEG], 1, [Have JPEG library?])
	    CPPFLAGS="$CPPFLAGS `$PKGCONFIG --cflags libjpeg`"
	    LIBS="$LIBS `$PKGCONFIG --libs libjpeg`"
	    have_jpeg=yes
	], [
	    AC_MSG_RESULT([no]);
	])
    ])

    AS_IF([test $have_jpeg = no], [
	AC_SEARCH_LIBS(jpeg_start_decompress, turbojpeg jpeg, [
	    AC_DEFINE([HAVE_LIBJPEG], 1, [Have JPEG library?])
	    have_jpeg=yes
	])
    ])

    AS_IF([test x$enable_libjpeg = xyes -a $have_jpeg = no], [
	AC_MSG_ERROR([libjpeg-dev 8 or later required for --enable-libjpeg.])
    ])
])


dnl libpng...
AC_ARG_ENABLE([libpng], AS_HELP_STRING([--enable-libpng], [use libpng for PNG printing, default=auto]))

PKGCONFIG_LIBPNG=""
AC_SUBST([PKGCONFIG_LIBPNG])

AS_IF([test "x$PKGCONFIG" != x -a x$enable_libpng != xno], [
    AC_MSG_CHECKING([for libpng-1.6.x])
    AS_IF([$PKGCONFIG --exists libpng16], [
	AC_MSG_RESULT([yes]);
	AC_DEFINE([HAVE_LIBPNG], 1, [Have PNG library?])
	CPPFLAGS="$CPPFLAGS `$PKGCONFIG --cflags libpng16`"
	LIBS="$LIBS `$PKGCONFIG --libs libpng16` -lz"
    ], [
	AC_MSG_RESULT([no]);
	AS_IF([test x$enable_libpng = xyes], [
	    AC_MSG_ERROR([libpng-dev 1.6 or later required for --enable-libpng.])
	])
    ])
], [test x$enable_libpng = xyes], [
    AC_MSG_ERROR([libpng-dev 1.6 or later required for --enable-libpng.])
])


dnl PAM support...
AC_ARG_ENABLE([pam], AS_HELP_STRING([--enable-libpam], [use libpam for authentication, default=auto]))

AS_IF([test x$enable_libpam != xno], [
    dnl PAM needs dlopen...
    AC_CHECK_LIB([dl], [dlopen])
    AC_CHECK_LIB([pam], [pam_start], [
	AC_DEFINE([HAVE_LIBPAM], 1, [Have PAM library?])
	LIBS="$LIBS -lpam"
    ])

    dnl PAM has two "standard" locations for its header...
    AC_CHECK_HEADER(security/pam_appl.h, AC_DEFINE([HAVE_SECURITY_PAM_APPL_H], 1, [Have <security/pam_appl.h> header?]))
    AC_CHECK_HEADER(pam/pam_appl.h, AC_DEFINE([HAVE_PAM_PAM_APPL_H], 1, [Have <pam/pam_appl.h> header?]))

    AS_IF([test x$ac_pam_start = xno -a x$enable_libpam = xyes], [
	AC_MSG_ERROR([libpam-dev required for --enable-libpam.])
    ])
])


dnl Extra compiler options...
AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [turn on debugging, default=no]))
AC_ARG_ENABLE([maintainer], AS_HELP_STRING([--enable-maintainer], [turn on maintainer mode, default=no]))
AC_ARG_ENABLE([sanitizer], AS_HELP_STRING([--enable-sanitizer], [build with AddressSanitizer, default=no]))

AS_IF([test x$enable_debug = xyes], [
    OPTIM="$OPTIM -g"
    CSFLAGS=""
], [
    OPTIM="$OPTIM -g -Os"
    CSFLAGS="-o runtime"
])

AC_SUBST([CSFLAGS])

WARNINGS=""
AC_SUBST([WARNINGS])

AS_IF([test -n "$GCC"], [
    AS_IF([test x$enable_sanitizer = xyes], [
	# Use -fsanitize=address with debugging...
	OPTIM="$OPTIM -fsanitize=address"
    ], [
	# Otherwise use the Fortify enhancements to catch any unbounded
	# string operations...
	CPPFLAGS="$CPPFLAGS -D_FORTIFY_SOURCE=2"
    ])

    dnl Show all standard warnings + unused variables when compiling...
    WARNINGS="-Wall -Wunused"

    dnl Drop some not-useful/unreliable warnings...
    for warning in char-subscripts format-truncation format-y2k switch unused-result; do
	AC_MSG_CHECKING([whether compiler supports -Wno-$warning])

	OLDCFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Wno-$warning -Werror"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [
	    AC_MSG_RESULT(yes)
	    WARNINGS="$WARNINGS -Wno-$warning"
        ], [
	    AC_MSG_RESULT(no)
	])

	CFLAGS="$OLDCFLAGS"
    done

    dnl Maintainer mode enables -Werror...
    AS_IF([test x$enable_maintainer = xyes], [
	WARNINGS="$WARNINGS -Werror"
    ])

    dnl See if PIE options are supported...
    AC_MSG_CHECKING(whether compiler supports -fPIE)
    OLDCFLAGS="$CFLAGS"
    AS_CASE(["$host_os_name"],
    [darwin*], [
	CFLAGS="$CFLAGS -fPIC -fPIE -Wl,-pie"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[
	    OLDCFLAGS="-fPIC $OLDCFLAGS"
	    LDFLAGS="-fPIE -Wl,-pie $LDFLAGS"
	    AC_MSG_RESULT(yes)
	],[
	    AC_MSG_RESULT(no)
	])
    ], [*], [
	CFLAGS="$CFLAGS -fPIC -fPIE -pie"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[
	    OLDCFLAGS="-fPIC $OLDCFLAGS"
	    LDFLAGS="-fPIE -pie $LDFLAGS"
	    AC_MSG_RESULT(yes)
	],[
	    AC_MSG_RESULT(no)
	])
    ])
    CFLAGS="$OLDCFLAGS"

    dnl OS-specific compiler options...
    AS_CASE(["$host_os_name"], [linux*], [
	CPPFLAGS="$CPPFLAGS -D__USE_MISC -D_GNU_SOURCE"
    ], [darwin*], [
        AS_IF([test "$host_os_version" -ge 200 -a x$enable_debug != xyes], [
            # macOS 11.0 and higher support the Apple Silicon (arm64) CPUs
	    OPTIM="$OPTIM -mmacosx-version-min=10.14 -arch x86_64 -arch arm64"
	], [test x$enable_debug != xyes], [
	    OPTIM="$OPTIM -mmacosx-version-min=10.14 -arch x86_64"
	])
    ])
])


dnl Extra linker options...
AC_ARG_WITH([ldflags], AS_HELP_STRING([--with-ldflags=...], [Specify additional LDFLAGS]), [
    LDFLAGS="$withval $LDFLAGS"
])

dnl State and run directories for root servers...
AS_IF([test "$prefix" = NONE], [
    # Default prefix isn't bound until AC_OUTPUT...
    realprefix="/usr/local"
], [
    realprefix="$prefix"
])


dnl Generate a bunch of files...
AC_CONFIG_FILES([
    Makedefs
])
AC_OUTPUT

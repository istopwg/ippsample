dnl
dnl Configuration script for the IPP sample code.
dnl
dnl Copyright © 2014-2023 by the Printer Working Group
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


dnl Check for submodules...
AS_IF([test ! -d libcups -o ! -d pdfio], [
    AS_IF([test -d .git], [
        AC_MSG_ERROR([Run "git submodule init && git submodule update" to get the libcups and PDFio submodules.])
    ], [
        AC_MSG_ERROR([The ZIP archive you downloaded via the Github "Code" button is missing the libcups and PDFio submodules. Please download a dated release or use Git.])
    ])
])


dnl Package name and version...
AC_INIT([IPPSAMPLE], [YYYY.MM], [https://github.com/istopwg/ippsample/issues], [ippsample], [http://istopwg.github.io/ippsample])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SUBDIRS([libcups])

IPPSAMPLE_VERSION="AC_PACKAGE_VERSION"
AS_IF([test "$IPPSAMPLE_VERSION" = YYYY.DD], [
    IPPSAMPLE_VERSION="$(date '+%Y.%m.%d')"
])

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
    LIBS="-framework CoreGraphics -framework ImageIO -framework CoreFoundation $LIBS"
    AC_CHECK_HEADER([CoreGraphics/CoreGraphics.h], [
	AC_DEFINE([HAVE_COREGRAPHICS], [1], [CoreGraphics support])
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


dnl 3D support
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
    IPPTRANSFORM3D_BIN="ipptransform3d"
    IPPTRANSFORM3D_HTML="ipptransform3d.html"
    IPPTRANSFORM3D_MAN="ipptransform3d.7"
])


dnl install-sh
AC_MSG_CHECKING([for install-sh script])
INSTALL="$(pwd)/install-sh"
AC_SUBST([INSTALL])
AC_MSG_RESULT([using $INSTALL])


dnl Check for pkg-config, which is used for some other tests later on...
AC_PATH_TOOL([PKGCONFIG], [pkg-config])


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
AC_ARG_WITH([sanitizer], AS_HELP_STRING([--with-sanitizer], [build with address, leak, memory, thread, or undefined sanitizer, default=no]), [], [with_sanitizer=no])
AS_IF([test "x$with_sanitizer" = xyes], [
    with_sanitizer="address"
], [test "$with_sanitizer" != address -a "$with_sanitizer" != leak -a "$with_sanitizer" != memory -a "$with_sanitizer" != no -a "$with_sanitizer" != thread -a "$with_sanitizer" != undefined], [
    AC_MSG_ERROR([Unsupported --with-sanitizer value "$with_sanitizer" specified.])
])

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
    AS_IF([test x$with_sanitizer != xno], [
	# Use -fsanitize=FOO with debugging...
	OPTIM="$OPTIM -fsanitize=$with_sanitizer"
    ], [echo "$CPPFLAGS $CFLAGS" | grep -q _FORTIFY_SOURCE], [
        # Don't add _FORTIFY_SOURCE if it is already there
    ], [
	# Otherwise use the Fortify enhancements to catch any unbounded
	# string operations...
	CPPFLAGS="$CPPFLAGS -D_FORTIFY_SOURCE=3"
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
    AC_MSG_CHECKING([for OS-specific compiler options])
    AS_CASE(["$host_os_name"], [linux*], [
    	# Make sure we get the full set of Linux APIs from the headers...
	CPPFLAGS="$CPPFLAGS -D__USE_MISC -D_GNU_SOURCE"

	# Mark read-only sections as relocatable to random addresses...
	LDFLAGS="$LDFLAGS -Wl,-z,relro,-z,now"

        AC_MSG_RESULT([-D__USE_MISC -D_GNU_SOURCE -Wl,-z,relro,-z,now])
    ], [darwin*], [
        # When not building for debug, target macOS 10.14 or later, "universal"
        # binaries when possible...
        AS_IF([echo "$CPPFLAGS $CFLAGS $LDFLAGS $OPTIM" | grep -q "\\-arch "], [
            # Don't add architecture/min-version flags if they are already present
            AC_MSG_RESULT([none])
        ], [echo "$CPPFLAGS $CFLAGS $LDFLAGS $OPTIM" | grep -q "\\-mmacosx-version-"], [
            # Don't add architecture/min-version flags if they are already present
            AC_MSG_RESULT([none])
        ], [test "$host_os_version" -ge 200 -a x$enable_debug != xyes], [
	    # macOS 11.0 and higher support the Apple Silicon (arm64) CPUs
	    OPTIM="$OPTIM -mmacosx-version-min=10.14 -arch x86_64 -arch arm64"
            AC_MSG_RESULT([-mmacosx-version-min=10.14 -arch x86_64 -arch arm64])
	], [test x$enable_debug != xyes], [
	    OPTIM="$OPTIM -mmacosx-version-min=10.14 -arch x86_64"
            AC_MSG_RESULT([-mmacosx-version-min=10.14 -arch x86_64])
	], [
	    # Don't add architecture/min-version flags if debug enabled
            AC_MSG_RESULT([none])
	])
    ], [*], [
        AC_MSG_RESULT([none])
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

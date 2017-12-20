dnl
dnl Compiler stuff for the IPP sample code.
dnl
dnl Copyright 2007-2016 by Apple Inc.
dnl Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Clear the debugging and non-shared library options unless the user asks
dnl for them...
INSTALL_STRIP=""
OPTIM=""
AC_SUBST(INSTALL_STRIP)
AC_SUBST(OPTIM)

AC_ARG_WITH(optim, [  --with-optim            set optimization flags ])
AC_ARG_ENABLE(debug, [  --disable-debug          build without debugging symbols])
AC_ARG_ENABLE(debug_guards, [  --enable-debug-guards   build with memory allocation guards])
AC_ARG_ENABLE(debug_printfs, [  --disable-debug-printfs  disable CUPS_DEBUG_LOG support])
AC_ARG_ENABLE(unit_tests, [  --enable-unit-tests     build and run unit tests])

dnl For debugging, keep symbols, otherwise strip them...
if test x$enable_debug != xno; then
	OPTIM="-g"
else
	INSTALL_STRIP="-s"
fi

dnl Debug printfs can slow things down, so provide a separate option for that
if test x$enable_debug_printfs != xno; then
	CFLAGS="$CFLAGS -DDEBUG"
	CXXFLAGS="$CXXFLAGS -DDEBUG"
fi

dnl Debug guards use an extra 4 bytes for some structures like strings in the
dnl string pool, so provide a separate option for that
if test x$enable_debug_guards = xyes; then
	CFLAGS="$CFLAGS -DDEBUG_GUARDS"
	CXXFLAGS="$CXXFLAGS -DDEBUG_GUARDS"
fi

dnl Unit tests take up time during a compile...
if test x$enable_unit_tests = xyes; then
	UNITTESTS="unittests"
else
	UNITTESTS=""
fi
AC_SUBST(UNITTESTS)

dnl Setup general architecture flags...
AC_ARG_WITH(archflags, [  --with-archflags        set default architecture flags ])
AC_ARG_WITH(ldarchflags, [  --with-ldarchflags      set program architecture flags ])

if test -z "$with_archflags"; then
	ARCHFLAGS=""
else
	ARCHFLAGS="$with_archflags"
fi

if test -z "$with_ldarchflags"; then
	if test "$uname" = Darwin; then
		# Only create Intel programs by default
		LDARCHFLAGS="`echo $ARCHFLAGS | sed -e '1,$s/-arch ppc64//'`"
	else
		LDARCHFLAGS="$ARCHFLAGS"
	fi
else
	LDARCHFLAGS="$with_ldarchflags"
fi

AC_SUBST(ARCHFLAGS)
AC_SUBST(LDARCHFLAGS)

dnl Read-only data/program support on Linux...
AC_ARG_ENABLE(relro, [  --enable-relro          build with the GCC relro option])

dnl Update compiler options...
CXXLIBS="${CXXLIBS:=}"
AC_SUBST(CXXLIBS)

PIEFLAGS=""
AC_SUBST(PIEFLAGS)

RELROFLAGS=""
AC_SUBST(RELROFLAGS)

if test -n "$GCC"; then
	# Add GCC-specific compiler options...
	if test -z "$OPTIM"; then
		if test "x$with_optim" = x; then
			# Default to optimize-for-size and debug
       			OPTIM="-Os -g"
		else
			OPTIM="$with_optim $OPTIM"
		fi
	fi

	# The -fstack-protector option is available with some versions of
	# GCC and adds "stack canaries" which detect when the return address
	# has been overwritten, preventing many types of exploit attacks.
	AC_MSG_CHECKING(whether compiler supports -fstack-protector)
	OLDCFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -fstack-protector"
	AC_TRY_LINK(,,
		OPTIM="$OPTIM -fstack-protector"
		AC_MSG_RESULT(yes),
		AC_MSG_RESULT(no))
	CFLAGS="$OLDCFLAGS"

	# The -fPIE option is available with some versions of GCC and
	# adds randomization of addresses, which avoids another class of
	# exploits that depend on a fixed address for common functions.
	AC_MSG_CHECKING(whether compiler supports -fPIE)
	OLDCFLAGS="$CFLAGS"
	case "$uname" in
		Darwin*)
			CFLAGS="$CFLAGS -fPIE -Wl,-pie"
			AC_TRY_COMPILE(,,[
				PIEFLAGS="-fPIE -Wl,-pie"
				AC_MSG_RESULT(yes)],
				AC_MSG_RESULT(no))
			;;

		*)
			CFLAGS="$CFLAGS -fPIE -pie"
			AC_TRY_COMPILE(,,[
				PIEFLAGS="-fPIE -pie"
				AC_MSG_RESULT(yes)],
				AC_MSG_RESULT(no))
			;;
	esac
	CFLAGS="$OLDCFLAGS"

	if test "x$with_optim" = x; then
		# Add useful warning options for tracking down problems...
		OPTIM="-Wall -Wno-format-y2k -Wunused $OPTIM"

		AC_MSG_CHECKING(whether compiler supports -Wno-unused-result)
		OLDCFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS -Werror -Wno-unused-result"
		AC_TRY_COMPILE(,,
			[OPTIM="$OPTIM -Wno-unused-result"
			AC_MSG_RESULT(yes)],
			AC_MSG_RESULT(no))
		CFLAGS="$OLDCFLAGS"

		AC_MSG_CHECKING(whether compiler supports -Wsign-conversion)
		OLDCFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS -Werror -Wsign-conversion"
		AC_TRY_COMPILE(,,
			[OPTIM="$OPTIM -Wsign-conversion"
			AC_MSG_RESULT(yes)],
			AC_MSG_RESULT(no))
		CFLAGS="$OLDCFLAGS"

		AC_MSG_CHECKING(whether compiler supports -Wno-tautological-compare)
		OLDCFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS -Werror -Wno-tautological-compare"
		AC_TRY_COMPILE(,,
			[OPTIM="$OPTIM -Wno-tautological-compare"
			AC_MSG_RESULT(yes)],
			AC_MSG_RESULT(no))
		CFLAGS="$OLDCFLAGS"

		# Error out on any warnings...
		#OPTIM="-Werror $OPTIM"
	fi

	case "$uname" in
		Darwin*)
			# -D_FORTIFY_SOURCE=2 adds additional object size
			# checking, basically wrapping all string functions
			# with buffer-limited ones.  Not strictly needed for
			# CUPS since we already use buffer-limited calls, but
			# this will catch any additions that are broken.
			CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2"
			;;

		Linux*)
			# The -z relro option is provided by the Linux linker command to
			# make relocatable data read-only.
			if test x$enable_relro = xyes; then
				RELROFLAGS="-Wl,-z,relro,-z,now"
			fi
			;;
	esac
else
	# Add vendor-specific compiler options...
	case $uname in
		SunOS*)
			# Solaris
			if test -z "$OPTIM"; then
				if test "x$with_optim" = x; then
					OPTIM="-xO2"
				else
					OPTIM="$with_optim $OPTIM"
				fi
			fi
			;;
		*)
			# Running some other operating system...
			echo "Building with default compiler optimizations; use the"
			echo "--with-optim option to override these."
			;;
	esac
fi

# Add general compiler options per platform...
case $uname in
	Linux*)
		# glibc 2.8 and higher breaks peer credentials unless you
		# define _GNU_SOURCE...
		OPTIM="$OPTIM -D_GNU_SOURCE"
		;;
esac

dnl
dnl Directory stuff for CUPS.
dnl
dnl Copyright 2018 by the IEEE-ISTO Printer Working Group.
dnl Copyright 2007-2018 by Apple Inc.
dnl Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Fix "prefix" variable if it hasn't been specified...
if test "$prefix" = "NONE"; then
	prefix="/usr/local"
fi

dnl Fix "exec_prefix" variable if it hasn't been specified...
if test "$exec_prefix" = "NONE"; then
	exec_prefix="$prefix"
fi

dnl Fix "bindir" variable...
if test "$bindir" = "\${exec_prefix}/bin"; then
	bindir="$exec_prefix/bin"
fi

AC_DEFINE_UNQUOTED(CUPS_SERVERBIN, "$bindir")

dnl Fix "sbindir" variable...
if test "$sbindir" = "\${exec_prefix}/sbin"; then
	sbindir="$exec_prefix/sbin"
fi

dnl Fix "datarootdir" variable if it hasn't been specified...
if test "$datarootdir" = "\${prefix}/share"; then
	if test "$prefix" = "/"; then
		datarootdir="/usr/share"
	else
		datarootdir="$prefix/share"
	fi
fi

dnl Fix "datadir" variable if it hasn't been specified...
if test "$datadir" = "\${prefix}/share"; then
	if test "$prefix" = "/"; then
		datadir="/usr/share"
	else
		datadir="$prefix/share"
	fi
elif test "$datadir" = "\${datarootdir}"; then
	datadir="$datarootdir"
fi

dnl Fix "includedir" variable if it hasn't been specified...
if test "$includedir" = "\${prefix}/include" -a "$prefix" = "/"; then
	includedir="/usr/include"
fi

dnl Fix "localstatedir" variable if it hasn't been specified...
if test "$localstatedir" = "\${prefix}/var"; then
	if test "$prefix" = "/"; then
		if test "$host_os_name" = darwin; then
			localstatedir="/private/var"
		else
			localstatedir="/var"
		fi
	else
		localstatedir="$prefix/var"
	fi
fi

dnl Fix "sysconfdir" variable if it hasn't been specified...
if test "$sysconfdir" = "\${prefix}/etc"; then
	if test "$prefix" = "/"; then
		if test "$host_os_name" = darwin; then
			sysconfdir="/private/etc"
		else
			sysconfdir="/etc"
		fi
	else
		sysconfdir="$prefix/etc"
	fi
fi

dnl Fix "libdir" variable...
if test "$libdir" = "\${exec_prefix}/lib"; then
	case "$host_os_name" in
		linux*)
			if test -d /usr/lib64 -a ! -d /usr/lib64/fakeroot; then
				libdir="$exec_prefix/lib64"
			fi
			;;
	esac
fi

# Data files
CUPS_DATADIR="$datadir/cups"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$datadir/cups")
AC_SUBST(CUPS_DATADIR)

# Locale data
if test "$localedir" = "\${datarootdir}/locale"; then
	case "$host_os_name" in
		linux* | gnu* | *bsd* | darwin*)
			CUPS_LOCALEDIR="$datarootdir/locale"
			;;

		*)
			# This is the standard System V location...
			CUPS_LOCALEDIR="$exec_prefix/lib/locale"
			;;
	esac
else
	CUPS_LOCALEDIR="$localedir"
fi

AC_DEFINE_UNQUOTED(CUPS_LOCALEDIR, "$CUPS_LOCALEDIR")
AC_SUBST(CUPS_LOCALEDIR)

# Configuration files
CUPS_SERVERROOT="$sysconfdir/cups"
AC_DEFINE_UNQUOTED(CUPS_SERVERROOT, "$sysconfdir/cups")
AC_SUBST(CUPS_SERVERROOT)

# Transient run-time state
AC_ARG_WITH(rundir, [  --with-rundir           set transient run-time state directory],CUPS_STATEDIR="$withval",[
	case "$host_os_name" in
		darwin*)
			# Darwin (macOS)
			CUPS_STATEDIR="$CUPS_SERVERROOT"
			;;
		*)
			# All others
			CUPS_STATEDIR="$localstatedir/run/cups"
			;;
	esac])
AC_DEFINE_UNQUOTED(CUPS_STATEDIR, "$CUPS_STATEDIR")
AC_SUBST(CUPS_STATEDIR)

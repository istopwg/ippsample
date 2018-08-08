dnl
dnl Operating system stuff for CUPS.
dnl
dnl Copyright 2007-2012 by Apple Inc.
dnl Copyright 1997-2006 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Get the operating system, version number, and architecture...
uname=`uname`
uversion=`uname -r | sed -e '1,$s/^[[^0-9]]*\([[0-9]]*\)\.\([[0-9]]*\).*/\1\2/'`
uarch=`uname -m`

case "$uname" in
	Darwin*)
		uname="Darwin"
		if test $uversion -lt 120; then
			AC_MSG_ERROR([Sorry, this version of CUPS requires macOS 10.8 or higher.])
		fi
		;;

	GNU* | GNU/*)
		uname="GNU"
		;;
	Linux*)
		uname="Linux"
		;;
esac

dnl
dnl PAM stuff for the IPP Sample project.
dnl
dnl Copyright © 2018 by the IEEE-ISTO Printer Working Group.
dnl Copyright © 2007-2018 by Apple Inc.
dnl Copyright @ 1997-2005 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

AC_ARG_ENABLE(pam, [  --disable-pam           disable PAM support])

PAMLIBS=""

if test x$enable_pam != xno; then
	SAVELIBS="$LIBS"

	AC_CHECK_LIB(dl,dlopen)
	AC_CHECK_LIB(pam,pam_start)
	AC_CHECK_HEADER(security/pam_appl.h, AC_DEFINE(HAVE_SECURITY_PAM_APPL_H))
	AC_CHECK_HEADER(pam/pam_appl.h, AC_DEFINE(HAVE_PAM_PAM_APPL_H))

	if test x$ac_cv_lib_pam_pam_start != xno; then
		# Set the necessary libraries for PAM...
		if test x$ac_cv_lib_dl_dlopen != xno; then
			PAMLIBS="-lpam -ldl"
		else
			PAMLIBS="-lpam"
		fi

		# Find the PAM configuration directory, if any...
		for dir in /private/etc/pam.d /etc/pam.d; do
			if test -d $dir; then
				PAMDIR=$dir
				break;
			fi
		done
	fi

	LIBS="$SAVELIBS"

	case "$host_os_name" in
		darwin*)
			# Darwin/macOS
			AC_DEFINE(DEFAULT_PAM_SERVICE, "cups")
			;;

		*)
			# All others; this test might need to be updated
			# as Linux distributors move things around...
			AC_DEFINE(DEFAULT_PAM_SERVICE, "other")
			;;
	esac
fi

AC_SUBST(PAMLIBS)

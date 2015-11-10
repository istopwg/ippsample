dnl
dnl Configuration script for the IPP Everywhere Printer Self-Certification
dnl tools.
dnl
dnl Copyright 2015 by the ISTO Printer Working Group.
dnl Copyright 2007-2015 by Apple Inc.
dnl Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl These coded instructions, statements, and computer programs are the
dnl property of Apple Inc. and are protected by Federal copyright
dnl law.  Distribution and use rights are outlined in the file "LICENSE.txt"
dnl which should have been included with this file.  If this file is
dnl file is missing or damaged, see the license at "http://www.cups.org/".
dnl

dnl We need at least autoconf 2.60...
AC_PREREQ(2.60)

dnl Package name and version...
AC_INIT([IPPEVESELFCERT], [20151110], [https://github.com/istopwg/ippeveselfcert/issues], [ippeveselfcert], [http://www.pwg.org/ipp/everywhere.html])

dnl IPP Everywhere Printer Self-Certification Manual Version; this is the
dnl filename prefix...
IPPEVESELFCERT_VERSION="ippeveselfcert10"
AC_SUBST(IPPEVESELFCERT_VERSION)

dnl PWG Raster Sample File Date/Version; this comes from:
dnl
dnl     http://ftp.pwg.org/pub/pwg/ipp/examples
PWGRASTER_VERSION="20150616"
AC_SUBST(PWGRASTER_VERSION)

dnl Include common configuration scripts...
sinclude(config-scripts/cups-opsys.m4)
sinclude(config-scripts/cups-common.m4)
sinclude(config-scripts/cups-compiler.m4)
sinclude(config-scripts/cups-network.m4)
sinclude(config-scripts/cups-poll.m4)
sinclude(config-scripts/cups-threads.m4)
sinclude(config-scripts/cups-ssl.m4)
sinclude(config-scripts/cups-largefile.m4)
sinclude(config-scripts/cups-dnssd.m4)

dnl Generate a bunch of files...
AC_OUTPUT(Makedefs
	  tests/bonjour-access-tests.test
	  tests/bonjour-tests.bat
	  tests/bonjour-tests.sh
	  tests/bonjour-value-tests.test
	  tests/document-tests.bat
	  tests/document-tests.sh
	  tests/document-tests.test
	  tests/ipp-tests.bat
	  tests/ipp-tests.sh
	  tests/ipp-tests.test)

chmod +x tests/*.sh

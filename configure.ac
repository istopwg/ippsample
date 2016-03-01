dnl
dnl Configuration script for the IPP Everywhere Printer Self-Certification
dnl tools.
dnl
dnl Copyright 2015-2016 by the ISTO Printer Working Group.
dnl Copyright 2007-2016 by Apple Inc.
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
AC_INIT([IPPSAMPLE], [20160229], [https://github.com/istopwg/ippsample/issues], [ippsample], [http://www.pwg.org/ipp/])

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
AC_OUTPUT(Makedefs)

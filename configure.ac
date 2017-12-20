dnl
dnl Configuration script for the IPP sample code.
dnl
dnl Copyright 2014-2017 by the IEEE-ISTO Printer Working Group
dnl Copyright 2007-2017 by Apple Inc.
dnl Copyright 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl We need at least autoconf 2.60...
AC_PREREQ(2.60)

dnl Package name and version...
AC_INIT([IPPSAMPLE], [1.0b1], [https://github.com/istopwg/ippsample/issues], [ippsample], [http://www.pwg.org/ipp/])

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

dnl IPP Sample-specific options
IPP="ipp"
AC_SUBST(IPP)
AC_ARG_WITH(name-prefix, [  --with-name-prefix=...  set prefix on command names (default is "ipp")],
    IPP="$withval",)

dnl Generate a bunch of files...
AC_OUTPUT(Makedefs)

#
# API library Makefile for CUPS.
#
# Copyright 2007-2015 by Apple Inc.
# Copyright 1997-2006 by Easy Software Products, all rights reserved.
#
# These coded instructions, statements, and computer programs are the
# property of Apple Inc. and are protected by Federal copyright
# law.  Distribution and use rights are outlined in the file "LICENSE.txt"
# which should have been included with this file.  If this file is
# file is missing or damaged, see the license at "http://www.cups.org/".
#
# This file is subject to the Apple OS-Developed Software exception.
#

include ../Makedefs


#
# Options to build libcups without the use of deprecated APIs...
#

OPTIONS	=	-D_CUPS_NO_DEPRECATED=1


#
# Object files...
#

OBJS	=	\
		array.o \
		auth.o \
		debug.o \
		dest.o \
		dest-job.o \
		dest-localization.o \
		dest-options.o \
		dir.o \
		encode.o \
		error.o \
		file.o \
		getputfile.o \
		globals.o \
		hash.o \
		http.o \
		http-addr.o \
		http-addrlist.o \
		http-support.o \
		ipp.o \
		ipp-support.o \
		langprintf.o \
		language.o \
		md5.o \
		md5passwd.o \
		notify.o \
		options.o \
		pwg-media.o \
		raster.o \
		request.o \
		snprintf.o \
		string.o \
		tempfile.o \
		thread.o \
		tls.o \
		transcode.o \
		usersys.o \
		util.o


#
# Targets in this directory...
#

TARGETS =	libcups.a


#
# Make all targets...
#

all:		$(TARGETS)


#
# Remove object and target files...
#

clean:
	$(RM) $(OBJS) $(TARGETS)


#
# Update dependencies (without system header dependencies...)
#

depend:
	$(CC) -MM $(ALL_CFLAGS) $(OBJS:.o=.c) >Dependencies


#
# Install the client library...
#

install:	all
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(libdir)
	$(INSTALL_LIB) -m 755 libcups.a $(BUILDROOT)$(libdir)
	$(RANLIB) $(BUILDROOT)$(libdir)/libcups.a
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(includedir)/cups
	for file in $(PUBLIC_HEADERS); do \
		$(INSTALL_DATA) $$file $(BUILDROOT)$(includedir)/cups; \
	done


#
# libcups.a
#

libcups.a:	$(OBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(OBJS)
	$(RANLIB) $@


#
# libcups2.def (Windows DLL exports file...)
#

libcups2.def: $(OBJS) Makefile
	echo Generating $@...
	echo "LIBRARY libcups2" >libcups2.def
	echo "VERSION 2.12" >>libcups2.def
	echo "EXPORTS" >>libcups2.def
	(nm $(OBJS) 2>/dev/null | grep "T _" | awk '{print $$3}'; \
	 echo __cups_strcpy; echo __cups_strlcat; echo __cups_strlcpy) | \
		grep -v -E \
		    -e 'cups_debug|Apple|BackChannel|Backend|FileCheck|Filter|GSSService|SetNegotiate|SideChannel' \
		    -e 'Block$$' | \
		sed -e '1,$$s/^_//' | sort >>libcups2.def


#
# Dependencies...
#

include Dependencies
tls.o: tls-darwin.c tls-gnutls.c tls-sspi.c

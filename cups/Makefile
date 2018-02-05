#
# API library Makefile for CUPS.
#
# Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group.
# Copyright © 2007-2018 by Apple Inc.
# Copyright © 1997-2006 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
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
		ipp-file.o \
		ipp-support.o \
		ipp-vars.o \
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
HEADERS =       \
                array.h \
                cups.h \
                dir.h \
                file.h \
                http.h \
                ipp.h \
                language.h \
                pwg.h \
                raster.h \
                transcode.h \
                versioning.h


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
	for file in $(HEADERS); do \
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
# Automatic API help files...
#

APISOURCES =	\
		auth.c \
		cups.h \
		dest.c \
		dest-job.c \
		dest-localization.c \
		dest-options.c \
		encode.c \
		error.c \
		getputfile.c \
		hash.c \
		http.c \
		http.h \
		http-addr.c \
		http-addrlist.c \
		http-support.c \
		ipp.c \
		ipp.h \
		ipp-support.c \
		md5passwd.c \
		notify.c \
		options.c \
		pwg.h \
		pwg-media.c \
		raster.c \
		raster.h \
		request.c \
		tls.c \
		usersys.c \
		util.c

apihelp:
	echo Generating CUPS API help files...
	mxmldoc --title "CUPS API Programming Guide" \
		--author "Michael R Sweet" \
		--copyright "Copyright 2007-2017 by Apple Inc." \
		--docversion $(IPPSAMPLE_VERSION) \
		--intro api.intro \
		api.xml $(APISOURCES) >../doc/api.html
	mxmldoc --title "CUPS API Programming Guide" \
		--author "Michael R Sweet" \
		--copyright "Copyright 2007-2017 by Apple Inc." \
		--docversion $(IPPSAMPLE_VERSION) \
		--intro api.intro \
		--epub ../doc/api.epub \
		api.xml
	if test `uname` = Darwin; then \
		mxmldoc --title "CUPS API Programming Guide" \
			--author "Michael R Sweet" \
			--copyright "Copyright 2007-2017 by Apple Inc." \
			--docversion $(IPPSAMPLE_VERSION) \
			--intro api.intro \
			--docset org.pwg.ippsample.docset \
			--feedname org.pwg.ippsample \
			--feedurl https://istopwg.github.io/ippsample/org.pwg.ippsample.atom \
			api.xml; \
		$(RM) org.pwg.ippsample.atom; \
		xcrun docsetutil package --output org.pwg.ippsample.xar \
			--atom org.pwg.ippsample.atom \
			--download-url https://istopwg.github.io/ippsample/org.pwg.ippsample.xar \
			org.pwg.ippsample.docset || exit 1; \
	fi
	$(RM) api.xml


#
# Dependencies...
#

include Dependencies
tls.o: tls-darwin.c tls-gnutls.c tls-sspi.c

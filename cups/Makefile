#
# API library Makefile for the CUPS library.
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

LIBOBJS	=	\
		array.o \
		auth.o \
		debug.o \
		dest.o \
		dest-job.o \
		dest-localization.o \
		dest-options.o \
		dir.o \
		encode.o \
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
		raster-error.o \
		raster-stream.o \
		raster-stubs.o \
		request.o \
		snprintf.o \
		string.o \
		tempfile.o \
		thread.o \
		tls.o \
		transcode.o \
		usersys.o \
		util.o
TESTOBJS =	\
		testarray.o \
		testclient.o \
		testdest.o \
		testfile.o \
		testhttp.o \
		testi18n.o \
		testipp.o \
		testoptions.o \
		testraster.o
OBJS	=	\
		$(LIBOBJS) \
		$(TESTOBJS)
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
TESTS	=	$(TESTOBJS:.o=)


#
# Make all targets...
#

all:		$(TARGETS)


#
# Remove object and target files...
#

clean:
	$(RM) $(OBJS) $(TARGETS) $(TESTS)


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
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(datadir)/cups
	for file in cupspm.epub cupspm.html cupspm.png; do \
		$(INSTALL_DATA) $$file $(BUILDROOT)$(datadir)/cups; \
	done


#
# Test the library.
#

test:	$(TESTS)
	echo Running unit tests...
	for test in $(TESTS); do \
		echo ""; \
		echo Running $$test...; \
		./$$test || exit 1; \
	done


#
# libcups.a
#

libcups.a:	$(LIBOBJS)
	echo Archiving $@...
	$(RM) $@
	$(AR) $(ARFLAGS) $@ $(LIBOBJS)
	$(RANLIB) $@


#
# libcups2.def (Windows DLL exports file...)
#

libcups2.def: $(OBJS) Makefile
	echo Generating $@...
	echo "LIBRARY libcups2" >libcups2.def
	echo "VERSION 2.14" >>libcups2.def
	echo "EXPORTS" >>libcups2.def
	(nm $(LIBOBJS) 2>/dev/null | grep "T _" | awk '{print $$3}'; \
	 echo __cups_strcpy; echo __cups_strlcat; echo __cups_strlcpy; \
	 echo __cups_snprintf; echo __cups_vsnprintf; echo __cups_gettimeofday) | \
		grep -v -E \
		    -e 'cups_debug|Apple|FileCheck|GSSService|SetNegotiate' \
		    -e 'Block$$' | \
		sed -e '1,$$s/^_//' | sort >>libcups2.def


#
# Unit tests
#

testarray: testarray.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testarray.o $(LIBS)
testclient: testclient.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testclient.o $(LIBS)
testdest: testdest.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testdest.o $(LIBS)
testfile: testfile.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testfile.o $(LIBS)
testhttp: testhttp.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testhttp.o $(LIBS)
testi18n: testi18n.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testi18n.o $(LIBS)
testipp: testipp.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testipp.o $(LIBS)
testoptions: testoptions.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testoptions.o $(LIBS)
testraster: testraster.o
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ testraster.o $(LIBS)


#
# Automatic API help files...
#

APISOURCES =	$(LIBOBJS:.o=.c) $(HEADERS)

apihelp:
	echo Generating CUPS API help files...
	$(RM) cupspm.xml
	codedoc --section "Programming" --body cupspm.md \
		cupspm.xml \
		auth.c cups.h dest*.c encode.c http.h http*.c ipp.h ipp*.c \
		options.c tls-darwin.c usersys.c util.c \
		--coverimage cupspm.png \
		--epub cupspm.epub
	codedoc --section "Programming" --body cupspm.md \
		cupspm.xml > cupspm.html
	$(RM) cupspm.xml


#
# Dependencies...
#

include Dependencies
tls.o: tls-darwin.c tls-gnutls.c tls-sspi.c

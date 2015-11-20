#
# Sample Infrastructure Printer and Proxy Makefile.
#
# Copyright 2015 by Apple Inc.
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
# Object files...
#

OBJS 	=	\
		ippproxy.o


#
# Targets in this directory...
#

TARGETS =	ippproxy


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
# ippproxy
#

ippproxy:	$(PROXYOBJS) ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ $(PROXYOBJS) $(LIBS)


#
# Dependencies...
#

include Dependencies

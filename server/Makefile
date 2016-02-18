#
# Makefile for sample IPP server implementation.
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

OBJS =	\
		client.o \
		conf.o \
		device.o \
		ipp.o \
		job.o \
		log.o \
		main.o \
		printer.o \
		subscription.o \
		transform.o
		

#
# Targets in this directory...
#

TARGETS =	ippserver


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
# ippserver
#

ippserver:	$(OBJS) ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)


#
# Dependencies...
#

include Dependencies
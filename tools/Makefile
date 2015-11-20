#
# IPP tools makefile.
#
# Copyright 2015 by the ISTO Printer Working Group.
# Copyright 2007-2015 by Apple Inc.
# Copyright 1997-2006 by Easy Software Products, all rights reserved.
#
# These coded instructions, statements, and computer programs are the
# property of Apple Inc. and are protected by Federal copyright
# law.  Distribution and use rights are outlined in the file "LICENSE.txt"
# which should have been included with this file.  If this file is
# file is missing or damaged, see the license at "http://www.cups.org/".
#

include ../Makedefs


OBJS		=	\
			ippfind.o \
			ippserver.o \
			ipptool.o
TARGETS		=	\
			ippfind \
			ippserver \
			ipptool


#
# Make all targets...
#

all:		$(TARGETS)


#
# Clean all object files...
#

clean:
	$(RM) $(TARGETS) $(OBJS)


#
# Update dependencies (without system header dependencies...)
#

depend:
	$(CC) -MM $(ALL_CFLAGS) $(OBJS:.o=.c) >Dependencies


#
# ippfind
#

ippfind:	ippfind.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippfind.o $(LIBS)


#
# ippserver
#

ippserver:	ippserver.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippserver.o $(LIBS)


#
# ipptool
#

ipptool:	ipptool.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptool.o $(LIBS)


#
# Dependencies...
#

include Dependencies

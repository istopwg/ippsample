#
# IPP tools makefile.
#
# Copyright 2007-2016 by Apple Inc.
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
			ippproxy.o \
			ipptool.o \
			ipptransform.o \
			ipptransform3d.o
TARGETS         =       \
                        $(BIN_TARGETS) \
                        $(SBIN_TARGETS)
BIN_TARGETS	=	\
			$(IPPFIND_BIN) \
			ipptool \
			$(IPPTRANSFORM_BIN) \
			$(IPPTRANSFORM3D_BIN)
SBIN_TARGETS	=	\
			ippproxy


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
# Install all tools.
#

install:	all
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(bindir)
	for file in $(BIN_TARGETS); do \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir); \
	done
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(sbindir)
	for file in $(SBIN_TARGETS); do \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(sbindir); \
	done


#
# ippfind
#

ippfind:	ippfind.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippfind.o $(LIBS)


#
# ippproxy
#

ippproxy:	ippproxy.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippproxy.o $(LIBS)


#
# ipptool
#

ipptool:	ipptool.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptool.o $(LIBS)


#
# ipptransform
#

ipptransform:	ipptransform.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform.o $(LIBS)


#
# ipptransform3d
#

ipptransform3d:	ipptransform3d.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform3d.o $(LIBS)


#
# Dependencies...
#

include Dependencies

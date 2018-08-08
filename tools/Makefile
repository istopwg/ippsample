#
# IPP tools makefile.
#
# Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group.
# Copyright © 2007-2018 by Apple Inc.
# Copyright © 1997-2006 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


OBJS		=	\
			ippdoclint.o \
			ippfind.o \
			ippproxy.o \
			ipptool.o \
			ipptransform.o \
			ipptransform3d.o
TARGETS         =       \
                        $(BIN_TARGETS) \
                        $(SBIN_TARGETS)
BIN_TARGETS	=	\
			ippdoclint \
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
		destfile=`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`; \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir)/$$destfile; \
	done
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(sbindir)
	for file in $(SBIN_TARGETS); do \
		destfile=`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`; \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(sbindir)/$$destfile; \
	done


#
# Test all tools.
#

test:


#
# ippdoclint
#

ippdoclint:	ippdoclint.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippdoclint.o $(LIBS)


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

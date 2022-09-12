#
# Tools makefile for the IPP sample code.
#
# Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


OBJS		=	\
			ipp-options.o \
			ipp3dprinter.o \
			ippdoclint.o \
			ippproxy.o \
			ipptransform.o \
			ipptransform3d.o
TARGETS         =       \
                        $(BIN_TARGETS) \
                        $(COMMAND_TARGETS) \
                        $(SBIN_TARGETS)
COMMAND_TARGETS	=	\
			ippdoclint \
			ipptransform \
			$(IPPTRANSFORM3D_BIN)
BIN_TARGETS	=	\
			ipp3dprinter
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
	$(CC) -MM $(CPPFLAGS) $(OBJS:.o=.c) >Dependencies


#
# Install all tools.
#

install:	all
	echo "Installing user programs to $(BUILDROOT)$(bindir)..."
	$(INSTALL_DIR) $(BUILDROOT)$(bindir)
	for file in $(BIN_TARGETS); do \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir)/$$file; \
	done
	echo "Installing printer commands to $(BUILDROOT)$(libexecdir)/cups/command..."
	$(INSTALL_DIR) $(BUILDROOT)$(libexecdir)/cups/command
	for file in $(COMMAND_TARGETS); do \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir)/$$file; \
		$(LN) $(bindir)/$$file $(BUILDROOT)$(libexecdir)/cups/command/$$file; \
	done
	echo "Installing admin programs to $(BUILDROOT)$(sbindir)..."
	$(INSTALL_DIR) $(BUILDROOT)$(sbindir)
	for file in $(SBIN_TARGETS); do \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(sbindir)/$$file; \
	done


#
# Test all tools.
#

test:


#
# ipp3dprinter
#

ipp3dprinter:	ipp3dprinter.o ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipp3dprinter.o $(LIBS)


#
# ippdoclint
#

ippdoclint:	ippdoclint.o ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippdoclint.o $(LIBS)


#
# ippproxy
#

ippproxy:	ippproxy.o ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippproxy.o $(LIBS)


#
# ipptransform
#

ipptransform:	ipptransform.o ipp-options.o ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform.o ipp-options.o $(LIBS) -lz


#
# ipptransform3d
#

ipptransform3d:	ipptransform3d.o ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform3d.o $(LIBS)


#
# Dependencies...
#

include Dependencies

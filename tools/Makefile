#
# Tools makefile.
#
# Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


OBJS		=	\
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
			$(IPPTRANSFORM_BIN) \
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
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(bindir)
	for file in $(BIN_TARGETS); do \
		destfile=`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`; \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir)/$$destfile; \
	done
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(libexecdir)/cups/command
	for file in $(COMMAND_TARGETS); do \
		destfile=`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`; \
		$(INSTALL_BIN) $$file $(BUILDROOT)$(bindir)/$$destfile; \
		ln -sf ../../../bin/$$destfile $(BUILDROOT)$(libexecdir)/cups/command/$$file; \
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
# ipp3dprinter
#

ipp3dprinter:	ipp3dprinter.o ../libcups/cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipp3dprinter.o $(LIBS)


#
# ippdoclint
#

ippdoclint:	ippdoclint.o ../libcups/cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippdoclint.o $(LIBS)


#
# ippproxy
#

ippproxy:	ippproxy.o ../libcups/cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ippproxy.o $(LIBS)


#
# ipptransform
#

ipptransform:	ipptransform.o ../libcups/cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform.o $(LIBS)


#
# ipptransform3d
#

ipptransform3d:	ipptransform3d.o ../libcups/cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ ipptransform3d.o $(LIBS)


#
# Dependencies...
#

include Dependencies

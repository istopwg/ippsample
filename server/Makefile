#
# Makefile for sample IPP server.
#
# Copyright © 2014-2022 by the Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


#
# Object files...
#

OBJS 	=	\
		auth.o \
		client.o \
		conf.o \
		device.o \
		ipp.o \
		job.o \
		log.o \
		main.o \
		printer.o \
		resource.o \
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
	$(CC) -MM $(CPPFLAGS) $(OBJS:.o=.c) >Dependencies


#
# Install the server.
#

install:	all
	echo "Installing server to $(BUILDROOT)$(sbindir)..."
	$(INSTALL_DIR) $(BUILDROOT)$(sbindir)
	$(INSTALL_BIN) ippserver $(BUILDROOT)$(sbindir)/ippserver


#
# Test the server.
#

test:


#
# ippserver
#

ippserver:	$(OBJS) ../libcups/cups/libcups3.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)


#
# Embedded PNG headers...
#

pngheaders:
	echo "Generating printer-png.h from printer.png..."
	echo "static const unsigned char printer_png[] =" >printer-png.h
	echo "{" >>printer-png.h
	od -t x1 printer.png | cut -b12- | awk '{printf("  "); for (i = 1; i <= NF; i ++) printf("0x%s,", $$i); print "";}' >>printer-png.h
	echo "};" >>printer-png.h
	echo "Generating printer3d-png.h from printer3d.png..."
	echo "static const unsigned char printer3d_png[] =" >printer3d-png.h
	echo "{" >>printer3d-png.h
	od -t x1 printer3d.png | cut -b12- | awk '{printf("  "); for (i = 1; i <= NF; i ++) printf("0x%s,", $$i); print "";}' >>printer3d-png.h
	echo "};" >>printer3d-png.h


#
# Dependencies...
#

include Dependencies

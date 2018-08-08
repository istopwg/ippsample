#
# IPP documentation makefile.
#
# Copyright 2014-2017 by the IEEE-ISTO Printer Working Group.
# Copyright 2015-2017 by Apple Inc.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


OBJS		=	\
			mantohtml.o
TARGETS		=	\
			mantohtml
MAN1		=	\
			ippdoclint.1 \
			$(IPPFIND_MAN) \
			ipptool.1 \
			$(IPPTRANSFORM_MAN) \
			$(IPPTRANSFORM3D_MAN)
MAN5		=	\
			ipptoolfile.5

MAN8		=	\
			ippproxy.8 \
			ippserver.8
HTML		=	\
			ippdoclint.html \
			$(IPPFIND_HTML) \
			ippproxy.html \
			ippserver.html \
			ipptool.html \
			ipptoolfile.html \
			$(IPPTRANSFORM_HTML) \
			$(IPPTRANSFORM3D_HTML)


#
# Rules for converting man pages to HTML...
#

.SUFFIXES:	.1 .5 .8 .html
.1.html .5.html .8.html:	mantohtml
	echo Generating $@...
	./mantohtml $< >$@


#
# Make all targets...
#

all:		$(TARGETS) $(HTML)


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
# Install man pages...
#

install:
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(mandir)/man1
	for file in $(MAN1); do \
		man="`echo $$file .| sed -e '1,$$s/^ipp/$(IPP)/'`"; \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man1/$$man; \
	done
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(mandir)/man5
	for file in $(MAN5); do \
		man="`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`"; \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man5/$$man; \
	done
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(mandir)/man8
	for file in $(MAN8); do \
		man="`echo $$file | sed -e '1,$$s/^ipp/$(IPP)/'`"; \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man8/$$man; \
	done


#
# mantohtml
#

mantohtml:	mantohtml.o ../cups/libcups.a
	echo Linking $@...
	$(CC) $(LDFLAGS) -o $@ mantohtml.o $(LIBS)


#
# Dependencies...
#

include Dependencies

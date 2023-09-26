#
# IPP documentation makefile.
#
# Copyright © 2014-2023 by the Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


MAN1		=	\
			ipp3dprinter.1
MAN7		=	\
			ippdoclint.7 \
			ipptransform.7 \
			$(IPPTRANSFORM3D_MAN)
MAN8		=	\
			ippproxy.8 \
			ippserver.8
HTML		=	\
			ipp3dprinter.html \
			ippdoclint.html \
			ippproxy.html \
			ippserver.html \
			ipptransform.html \
			$(IPPTRANSFORM3D_HTML)


#
# Rules for converting man pages to HTML...
#

.SUFFIXES:	.1 .7 .8 .html
.1.html .7.html .8.html:
	echo Generating $@...
	mantohtml $< >$@


#
# Make all targets...
#

all:		$(HTML)


#
# Clean all object files...
#

clean:


#
# Update dependencies (without system header dependencies...)
#

depend:


#
# Install man pages...
#

install:
	echo "Installing man pages to $(BUILDROOT)/$(mandir)..."
	$(INSTALL_DIR) $(BUILDROOT)$(mandir)/man1
	for file in $(MAN1); do \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man1/$$man; \
	done
	$(INSTALL_DIR) $(BUILDROOT)$(mandir)/man7
	for file in $(MAN7); do \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man7/$$man; \
	done
	$(INSTALL_DIR) $(BUILDROOT)$(mandir)/man8
	for file in $(MAN8); do \
		$(INSTALL_MAN) $$file $(BUILDROOT)$(mandir)/man8/$$man; \
	done

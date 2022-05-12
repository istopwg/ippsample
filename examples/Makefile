#
# IPP examples makefile.
#
# Copyright © 2021-2022 by the IEEE-ISTO Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


DOCFILES	=	\
			box.3mf \
			cube_gears.3mf \
			cylinder.3mf \
			dodeca_chain_loop.3mf \
			heartgears.3mf \
			ipp-3d-with-grommet.stl \
			ipp-3d.3mf \
			ipp-3d.stl \
			sphere.3mf \
			torus.3mf \
			vector.pdf

TESTFILES	=	\
			create-job-3d.test \
			create-resource-strings.test \
			delete-printer.test \
			get-jobs-fetchable.test \
			get-system-attributes.test \
			ipp-3d.test \
			pwg5100.1.test \
			pwg5100.2.test \
			pwg5100.3.test \
			pwg5100.5.test \
			pwg5100.6.test \
			pwg5100.7.test \
			pwg5100.8.test \
			pwg5100.9.test \
			pwg5100.11.test \
			pwg5100.15.test \
			pwg5100.16.test \
			pwg5100.17.test \
			rfc3380.test \
			rfc3995-3996.test \
			rfc3998.test \
			set-system-attributes.test


#
# Make all targets...
#

all:


#
# Clean all object files...
#

clean:


#
# Update dependencies (without system header dependencies...)
#

depend:


#
# Install...
#

install:
	echo "Installing ipptool files to $(BUILDROOT)$(datadir)/ipptool..."
	$(INSTALL_DIR) $(BUILDROOT)$(datadir)/ipptool
	for file in $(TESTFILES) $(DOCFILES); do \
		$(INSTALL_DATA) $$file $(BUILDROOT)$(datadir)/ipptool; \
	done

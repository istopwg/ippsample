#
# IPP examples makefile.
#
# Copyright © 2019 by the IEEE-ISTO Printer Working Group.
# Copyright © 2019 by Apple Inc.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include ../Makedefs


DOCFILES	=	\
			box.3mf \
			color.jpg \
			cube_gears.3mf \
			cylinder.3mf \
			document-a4.pdf \
			document-a4.ps \
			document-letter.pdf \
			document-letter.ps \
			dodeca_chain_loop.3mf \
			gray.jpg \
			heartgears.3mf \
			ipp-3d-with-grommet.stl \
			ipp-3d.3mf \
			ipp-3d.stl \
			onepage-a4-300-black-1.pwg \
			onepage-a4.pdf \
			onepage-a4.ps \
			onepage-a4.sla \
			onepage-letter-300-black-1.pwg \
			onepage-letter.pdf \
			onepage-letter.ps \
			onepage-letter.sla \
			sphere.3mf \
			testfile.jpg \
			testfile.pcl \
			testfile.pdf \
			testfile.ps \
			testfile.txt \
			torus.3mf \
			vector.pdf

TESTFILES	=	\
			cancel-current-job.test \
			cancel-job.test \
			create-job-3d.test \
			create-job-format.test \
			create-job-sheets.test \
			create-job-timeout.test \
			create-job.test \
			create-printer-pcl.test \
			create-printer-pwg.test \
			create-printer-subscription.test \
			create-resource-strings.test \
			delete-printer.test \
			disable-printer.test \
			enable-printer.test \
			get-completed-jobs.test \
			get-job-attributes.test \
			get-job-template-attributes.test \
			get-jobs.test \
			get-notifications.test \
			get-printer-attributes-2.0.test \
			get-printer-attributes.test \
			get-printer-description-attributes.test \
			get-printers.test \
			get-subscriptions.test \
			get-system-attributes.test \
			identify-printer-display.test \
			identify-printer-multiple.test \
			identify-printer.test \
			ipp-1.1.test \
			ipp-2.0.test \
			ipp-2.1.test \
			ipp-2.2.test \
			ipp-3d.test \
			ipp-everywhere.test \
			pause-printer.test \
			print-job-and-wait.test \
			print-job-deflate.test \
			print-job-draft.test \
			print-job-gzip.test \
			print-job-hold.test \
			print-job-media-col.test \
			print-job-password.test \
			print-job.test \
			print-uri.test \
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
			resume-printer.test \
			rfc3380.test \
			rfc3995-3996.test \
			rfc3998.test \
			set-attrs-hold.test \
			set-system-attributes.test \
			validate-job.test


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
	$(INSTALL_DIR) -m 755 $(BUILDROOT)$(datadir)/ipptool
	for file in $(TESTFILES) $(DOCFILES); do \
		$(INSTALL_DATA) $$file $(BUILDROOT)$(datadir)/ipptool; \
	done

#
# Top-level Makefile for IPP sample implementations.
#
# Copyright © 2014-2024 by the Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include Makedefs


# Source directories...
DIRS	=	\
		libcups/cups \
		libcups/pdfio \
		libcups/tools \
		server \
		tools


#
# Make all targets...
#

all:
	for dir in $(DIRS); do \
		if test -f $$dir/Makefile; then \
			echo "======== all in $$dir ========"; \
			(cd $$dir; $(MAKE) $(MFLAGS) all) || exit 1; \
		fi; \
	done


#
# Remove object and target files...
#

clean:
	for dir in $(DIRS); do \
		if test -f $$dir/Makefile; then \
			echo "======== clean in $$dir ========"; \
			(cd $$dir; $(MAKE) $(MFLAGS) clean) || exit 1; \
		fi; \
	done


#
# Remove all non-distribution files...
#

distclean:	clean
	$(RM) Makedefs config.h config.log config.status
	-$(RM) -r autom4te*.cache
	(cd libcups; make distclean)


#
# Make dependencies
#

depend:
	for dir in server tools; do \
		echo "======== depend in $$dir ========"; \
		(cd $$dir; $(MAKE) $(MFLAGS) depend) || exit 1; \
	done


#
# Install everything...
#

install:
	for dir in $(DIRS) examples man; do \
		if test -f $$dir/Makefile; then \
			echo "======== install in $$dir ========"; \
			(cd $$dir; $(MAKE) $(MFLAGS) prefix=$(prefix) install) || exit 1; \
		fi; \
	done


#
# Test everything...
#

.PHONY: test

test:
	for dir in $(DIRS); do \
		if test -f $$dir/Makefile; then \
			echo "======== test in $$dir ========"; \
			(cd $$dir; $(MAKE) $(MFLAGS) CFLAGS="$(CFLAGS)" COMMONFLAGS="$(OPTIM)" test) || exit 1; \
		fi; \
	done
	echo "Running integration tests..."
	test/run-tests.sh


#
# Don't run top-level build targets in parallel...
#

.NOTPARALLEL:

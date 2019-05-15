#
# Top-level Makefile for IPP sample implementations.
#
# Copyright © 2014-2019 by the IEEE-ISTO Printer Working Group.
# Copyright © 2007-2019 by Apple Inc.
# Copyright © 1997-2007 by Easy Software Products, all rights reserved.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

include Makedefs


# Source directories...
DIRS	=	\
		cups \
		server \
		tools


#
# Make all targets...
#

all:
	for dir in $(DIRS); do \
		echo Making all in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) all) || exit 1; \
	done


#
# Remove object and target files...
#

clean:
	for dir in $(DIRS); do \
		echo Cleaning all in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) clean) || exit 1; \
	done


#
# Remove all non-distribution files...
#

distclean:	clean
	$(RM) Makedefs config.h config.log config.status
	-$(RM) -r autom4te*.cache


#
# Make dependencies
#

depend:
	for dir in $(DIRS); do \
		echo Updating dependencies in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) depend) || exit 1; \
	done


#
# Install everything...
#

install:
	for dir in $(DIRS) examples man; do \
		echo Installing in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) install) || exit 1; \
	done


#
# Test everything...
#

.PHONY: test

test:
	for dir in $(DIRS); do \
		echo Testing in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) test) || exit 1; \
	done
	echo Running integration tests...
	test/run-tests.sh


#
# Don't run top-level build targets in parallel...
#

.NOTPARALLEL:

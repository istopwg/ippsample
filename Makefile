#
# Top-level Makefile for IPP sample implementations.
#
# Copyright 2007-2016 by Apple Inc.
# Copyright 1997-2007 by Easy Software Products, all rights reserved.
#
# These coded instructions, statements, and computer programs are the
# property of Apple Inc. and are protected by Federal copyright
# law.  Distribution and use rights are outlined in the file "LICENSE.txt"
# which should have been included with this file.  If this file is
# file is missing or damaged, see the license at "http://www.cups.org/".
#

include Makedefs


# Source directories...
DIRS	=	\
		cups \
		proxy \
		server \
		tools


#
# Make all targets...
#

all:
	for dir in $(DIRS); do \
		echo Making all in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) all); \
	done


#
# Remove object and target files...
#

clean:
	for dir in $(DIRS); do \
		echo Cleaning all in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) clean); \
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
		(cd $$dir; $(MAKE) $(MFLAGS) depend); \
	done


#
# Install everything...
#

install:
	for dir in $(DIRS) doc; do \
		echo Installing in $$dir...; \
		(cd $$dir; $(MAKE) $(MFLAGS) install); \
	done


#
# Run the Clang static code analysis tool on the sources, available here:
#
#    http://clang-analyzer.llvm.org
#
# At least checker-231 is required.
#

.PHONY: clang clang-changes
clang:
	$(RM) -r clang
	scan-build -V -k -o `pwd`/clang $(MAKE) $(MFLAGS) clean all
clang-changes:
	scan-build -V -k -o `pwd`/clang $(MAKE) $(MFLAGS) all


#
# Make distribution files for the web site.
#

.PHONEY:	dist
dist:	all
#	scripts/make-ippeveselfcert.sh $(IPPEVESELFCERT_VERSION) $(SELFCERTVERSION)


#
# Don't run top-level build targets in parallel...
#

.NOTPARALLEL:


#
# End of "$Id: Makefile 12414 2015-01-21 00:02:04Z msweet $".
#

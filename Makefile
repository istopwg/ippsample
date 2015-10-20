#
# Top-level Makefile for IPP Everywhere Printer Self-Certification tools.
#
# Copyright 2015 by the ISTO Printer Working Group.
# Copyright 2007-2015 by Apple Inc.
# Copyright 1997-2007 by Easy Software Products, all rights reserved.
#
# These coded instructions, statements, and computer programs are the
# property of Apple Inc. and are protected by Federal copyright
# law.  Distribution and use rights are outlined in the file "LICENSE.txt"
# which should have been included with this file.  If this file is
# file is missing or damaged, see the license at "http://www.cups.org/".
#

include Makedefs


#
# Make all targets...
#

all:
	(cd cups; $(MAKE) $(MFLAGS) all)
	(cd tools; $(MAKE) $(MFLAGS) all)


#
# Remove object and target files...
#

clean:
	(cd cups; $(MAKE) $(MFLAGS) clean)
	(cd tools; $(MAKE) $(MFLAGS) clean)


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
	(cd cups; $(MAKE) $(MFLAGS) depend)
	(cd tools; $(MAKE) $(MFLAGS) depend)


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
	scripts/make-ippeveselfcert.sh $(SELFCERTVERSION)


#
# Don't run top-level build targets in parallel...
#

.NOTPARALLEL:


#
# End of "$Id: Makefile 12414 2015-01-21 00:02:04Z msweet $".
#

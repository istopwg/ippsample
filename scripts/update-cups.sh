#!/bin/sh
#
# Script to merge changes from CUPS 2.3.x upstream into the "cups" directory.
#

if test ! -x scripts/update-cups.sh; then
	echo Must run this script from the top directory.
	exit 1
fi

if test ! -d ../cups; then
	echo Must have CUPS 2.3.x checked out in ../cups.
	exit 1
fi

oldrev=`cat .cups-upstream`
newrev=`cd ../cups; git show | head -1 | awk '{print $2}'`

(cd ../cups; git diff $oldrev cups ':!cups/Dependencies' ':!cups/Makefile' ':!cups/libcups2.def' ':!cups/adminutil*' ':!cups/backchannel*' ':!cups/interpret*' ':!cups/ppd*' ':!cups/raster-inter*' ':!cups/sidechannel*' ':!cups/test*' ':!cups/snmp*' ':!cups/tlscheck.c') >$newrev.patch
git apply $newrev.patch && (echo $newrev >.cups-upstream; git commit -a -m "Sync up libcups changes from CUPS master@$newrev"; rm -f $newrev.patch) || echo "$newrev.patch did not apply."

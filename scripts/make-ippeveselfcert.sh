#!/bin/sh
#
# Make an IPP Everywhere Printer self-certification package.
#
# Copyright 2014-2015 by the ISTO Printer Working Group.
# Copyright 2007-2013 by Apple Inc.
# Copyright 1997-2007 by Easy Software Products, all rights reserved.
#
# These coded instructions, statements, and computer programs are the
# property of Apple Inc. and are protected by Federal copyright
# law.  Distribution and use rights are outlined in the file "LICENSE.txt"
# which should have been included with this file.  If this file is
# file is missing or damaged, see the license at "http://www.cups.org/".
#

# Make sure we are running in the right directory...
if test ! -f scripts/make-ippeveselfcert.sh; then
        echo "Run this script from the top-level source directory, e.g.:"
        echo ""
        echo "    scripts/make-ippeveselfcert.sh $*"
        echo ""
        exit 1
fi

if test $# -lt 1 -o $# -gt 2; then
	echo "Usage: everywhere/make-ippeveselfcert.sh version [platform]"
	exit 1
fi

fileversion="$1"
if test $# = 2; then
	platform="$2"
else
	case `uname` in
		Darwin)
			platform="osx"
			;;

		Linux)
			if test -x /usr/bin/dpkg; then
				platform="ubuntu"
			else
				platform="rhel"
			fi
			;;

		*)
			platform=`uname`
			;;
	esac
fi

if test x$platform = xosx -a "x$CODESIGN_IDENTITY" = x; then
	echo "Please set the CODESIGN_IDENTITY environment variable before running."
	exit 1
fi

if test x$platform = xosx -a "x$HDIUTIL_CERT" = x; then
	echo "Please set the HDIUTIL_CERT environment variable before running."
	exit 1
fi

echo Creating package directory...
pkgdir="sw-ippeveselfcert10-$fileversion"

test -d $pkgdir && rm -r $pkgdir
mkdir $pkgdir || exit 1

echo Copying package files
cp LICENSE.txt $pkgdir
cp doc/man-ipp*.html $pkgdir
cp tests/README.txt $pkgdir
cp tests/*.jpg $pkgdir
cp tests/*.pdf $pkgdir
cp tests/*.sh $pkgdir
cp tests/*.test $pkgdir
cp tools/ippfind $pkgdir/ippfind
cp tools/ippserver $pkgdir
cp tools/ipptool $pkgdir/ipptool
cp tools/printer.png $pkgdir

chmod +x $pkgdir/*.sh

if test x$platform = xosx; then
	# Sign executables...
	codesign -s "$CODESIGN_IDENTITY" -fv $pkgdir/ippfind
	codesign -s "$CODESIGN_IDENTITY" -fv $pkgdir/ippserver
	codesign -s "$CODESIGN_IDENTITY" -fv $pkgdir/ipptool

	# Make disk image...
	pkgfile="$pkgdir-osx.dmg"
	echo Creating disk image $pkgfile...
	test -f $pkgfile && rm $pkgfile
	hdiutil create -srcfolder $pkgdir -certificate "$HDIUTIL_CERT" $pkgfile
else
	# Make archive...
	pkgfile="$pkgdir-$platform.tar.gz"
	echo Creating archive $pkgfile...
	tar czf $pkgfile $pkgdir || exit 1
fi

echo Removing temporary files...
rm -r $pkgdir

echo Done.

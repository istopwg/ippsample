#!/bin/sh
#
# Script to make the PWG sample files using the test script.
#
# Usage: ./make-pwg-samples.sh resolution
#

if test ! -f examples/color.jpg; then
	echo "Run this script from the root source directory."
	exit 1
fi

if test ! -x tools/ipptransform; then
	echo "The ipptransform tool does not appear to be built."
	exit 1
fi

case "$1" in
	*dpi)
		resolution="$1"
		;;
	*)
		echo "Usage: ./make-pwg-samples.sh resolution"
		echo ""
		echo "Resolution must be of the form 'NNNdpi' or 'NNNxNNNdpi'."
		exit 1
		;;
esac

# Date for this version of the script...
date="`date '+%Y%m%d'`"
year="`date '+%Y'`"

# Output directory...
dir="pwg-raster-samples-$resolution"

# Files to render...
files="document-a4.pdf onepage-a4.pdf document-letter.pdf onepage-letter.pdf color.jpg gray.jpg"

# Log file...
log="$dir.log"

# Remove any existing raster directory for this resolution and recreate it...
test -d $dir && rm -rf $dir
test -f $log && rm -f $log

mkdir -p $dir/originals
for file in $files; do
	cp examples/$file $dir/originals
done

lastmedia=""
lasttypes=""

for file in $files; do
	echo Generating raster data for $file...
        quality="4"
        mimetype="application/pdf"

	case $file in
		*color*.jpg)
			base="`basename $file .jpg`-4x6"
			types="black_1 black_8 sgray_8 srgb_8 adobe-rgb_8 adobe-rgb_16 cmyk_8"
			media="na_index_4x6in"
			quality="5"
			mimetype="image/jpeg"
			;;
		*gray*.jpg)
			base="`basename $file .jpg`-4x6"
			types="black_1 black_8 sgray_8"
			media="na_index_4x6in"
			mimetype="image/jpeg"
			;;
		*-a4.pdf)
			base="`basename $file .pdf`"
			types="black_1 black_8 sgray_8 srgb_8 adobe-rgb_8 adobe-rgb_16 cmyk_8"
			media="iso_a4_210x297mm"
			;;
		*-letter.pdf)
			base="`basename $file .pdf`"
			types="black_1 black_8 sgray_8 srgb_8 adobe-rgb_8 adobe-rgb_16 cmyk_8"
			media="na_letter_8.5x11in"
			;;
	esac

	for type in $types; do
		typename=`echo $type | sed -e '1,$s/_/-/g'`

		test -d $dir/$typename || mkdir $dir/$typename

		output="$base-$typename-$resolution.pwg"

		echo "$output: \c"
		echo "$output:" >>$log
		tools/ipptransform -m image/pwg-raster -t $type -r $resolution -o media=$media -o print-quality=$quality -i $mimetype -vvv examples/$file  >"$dir/$typename/$output" 2>>"$log"
		ls -l "$dir/$typename/$output" | awk '{if ($5 > 1048575) printf "%.1fMiB\n", $5 / 1048576; else printf "%.0fkiB\n", $5 / 1024;}'
	done
done

cat >$dir/README.txt <<EOF
README.txt - $date
-----------------------

This directory contains sample PWG Raster files for different raster types at
$resolution.  The original documents used to generate the raster files are
located in the "originals" directory.


TEST FILES

- color.jpg

    A 6 megapixel color photograph with a 3:2 aspect ratio.

- document-a4.pdf

    A multi-page PDF document containing a mix of text, graphics, and images
    sized for ISO A4 media.

- document-letter.pdf

    A multi-page PDF document containing a mix of text, graphics, and images
    sized for US Letter media.

- gray.jpg

    A 5 megapixel grayscale photograph with a 5:4 aspect ratio.

- onepage-a4.pdf

    A single-page PDF document containing a mix of text and graphics sized for
    ISO A4 media.

- onepage-letter.pdf

    A single-page PDF document containing a mix of text and graphics sized for
    US Letter media.


VIEWING PWG RASTER FILES

The free RasterView application can be used to view PWG Raster files and is
available at:

    https://www.msweet.org/rasterview


DOCUMENTATION AND RESOURCES

The PWG Raster format is documented in PWG 5102.4: PWG Raster Format, available
at:

    https://ftp.pwg.org/pub/pwg/candidates/cs-ippraster10-20120420-5102.4.pdf

Please contact the PWG Webmaster (webmaster AT pwg.org) to report problems with
these sample files.  Questions about the PWG Raster format should be addressed
to the IPP working group mailing list (ipp AT pwg.org).


LEGAL STUFF

All original PDF files are Copyright 2011 Apple Inc.  All rights reserved.

All original JPEG files are Copyright 2003, 2007 Michael R Sweet.  All rights
reserved.

These sample PWG Raster files are Copyright $year by The Printer Working Group.
All rights reserved.
EOF

echo "Creating $dir.zip... \c"
zip -qr9 $dir-$date.zip $dir
ls -l "$dir-$date.zip" | awk '{printf "%.1fMiB\n", $5 / 1048576;}'

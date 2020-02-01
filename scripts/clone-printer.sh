#!/bin/sh
#
# Clone the attributes, icons, and strings files for the named printer.
#
# Usage: clone-printer "Printer DNS-SD Name"
#

if test $# = 0; then
	echo "Usage: 'Printer DNS-SD Name'"
	exit 1
fi

for printer in "$@"; do
	echo "Gathering files for $printer:"
	name="`echo $printer | tr ' ' '_' | tr '[' '_' | tr ']' '_'`"

	ippfind "$printer" --exec ipptool --ippserver $name.conf '{}' get-printer-attributes.test \; && echo "    Wrote $name.conf" || echo "    Failed to get attributes."

	url="`grep printer-icons $name.conf | awk -F '"' '{if (NF < 5) print $2; else print $(NF-3);}'`"
	if test "x$url" != x; then
		curl "$url" -k -s -o $name.png && echo "    Wrote $name.png" || echo "    Failed to get icon from $url."
	else
		echo "    No icon file."
	fi

	url="`grep printer-strings-uri $name.conf | awk -F '"' '{print $2}'`"
	if test "x$url" != x; then
		curl "$url" -k -s -o $name.strings && echo "    Wrote $name.strings" || echo "    Failed to get strings file from $url."
	else
		echo "    No strings file."
	fi
done

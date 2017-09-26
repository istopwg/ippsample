#!/bin/sh
#
# Copyright 2017 by The Printer Working Group.
#
# This program may be copied and furnished to others, and derivative works
# that comment on, or otherwise explain it or assist in its implementation may
# be prepared, copied, published and distributed, in whole or in part, without
# restriction of any kind, provided that the above copyright notice and this
# paragraph are included on all such copies and derivative works.
#
# The IEEE-ISTO and the Printer Working Group DISCLAIM ANY AND ALL WARRANTIES,
# WHETHER EXPRESS OR IMPLIED INCLUDING (WITHOUT LIMITATION) ANY IMPLIED
# WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
#
# Usage:
#
#   test/start-server.sh [options]
#

# Verify we have been run from the correct location...
if test ! -d test; then
        echo "Usage: test/start-server.sh [options]"
        exit 1
fi

# Find where the ippserver binary resides
IPPSERVER=""
for file in server/ippserver xcode/DerivedData/ippsample/Build/Products/Debug/ippserver xcode/DerivedData/ippsample/Build/Products/Release/ippserver; do
        if test -x $file; then
                IPPSERVER="$file"
                break
        fi
done

if test "x$IPPSERVER" = x; then
        echo "You must build ippserver before running this script."
        exit 1
fi

# Run server with base options:
#
# - Be very verbose
# - Use configuration directory "test"
# - Specify IPP Everywhere sub-type
echo "Running $IPPSERVER -vvv -C test -r _print $@"

exec $IPPSERVER -vvv -C test -r _print "$@"

#!/bin/sh
#
# Copyright © 2017-2018 by The Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
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

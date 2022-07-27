#!/bin/sh
#
# Integration test script for ippsample.
#
# Copyright © 2018-2022 by The Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#
# Usage:
#
#   test/run-tests.sh
#

status=0

# Run ippserver...
echo "Running ippserver..."
cd ..
CUPS_DEBUG_LOG=test-cups.log CUPS_DEBUG_LEVEL=4 CUPS_DEBUG_FILTER='^(http|_http|ipp|_ipp|cupsDo|cupsGet|cupsSend)' ./server/ippserver -vvv -C test 2>test-ippserver.log &
ippserver=$!

echo "ippserver has PID $ippserver, waiting for server to come up..."
sleep 10
echo ""

# Test the instance...
echo "Running ippfind + ipptool..."
./libcups/tools/ippfind-static -T 5 --literal-name "ipp-everywhere-pdf" --exec ./libcups/tools/ipptool-static -V 2.0 -tIf libcups/examples/document-letter.pdf '{}' libcups/examples/ipp-2.0.test \; || status=1

echo ""
echo "Running IPP System Service tests..."
./libcups/tools/ippfind-static -T 5 --literal-name "ipp-everywhere-pdf" --exec ./libcups/tools/ipptool-static -V 2.0 -tI 'ipp://{service_hostname}:{service_port}/ipp/system' examples/pwg5100.22.test \; || status=1

# Clean up
kill $ippserver

exit $status

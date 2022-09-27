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
#   ./run-tests.sh
#
# Set IPPEVEPRINTERPORT and IPPSERVERPORT environment variable to override the
# default 8xxx and 9xxx port numbers.
status=0

# Determine port numbers to use for tests...
ippeveprinterport=${IPPEVEPRINTERPORT:=$((8000 + ( $(id -u) % 1000 ) ))}
ippserverport=${IPPSERVERPORT:=$((9000 + ( $(id -u) % 1000 ) ))}

# Run ippserver, ippeveprinter, and ippproxy...
echo "Running ippeveprinter on port $ippeveprinterport..."
libcups/tools/ippeveprinter-static -vvv -p "$ippeveprinterport" -a libcups/tools/test.conf "Test Printer $(date +%H%M%S)" 2>test/test-ippeveprinter.log &
ippeveprinter=$!
echo "ippeveprinter has PID $ippeveprinter..."

echo ""
echo "Running ippserver on port $ippserverport..."
server/ippserver -vvv -p "$ippserverport" -C test 2>test/test-ippserver.log &
ippserver=$!
echo "ippserver has PID $ippserver, waiting for server to come up..."
sleep 10

echo ""
echo "Running ippproxy..."
tools/ippproxy -vvv -d "ipp://localhost:$ippeveprinterport/ipp/print" "ipp://localhost:$ippserverport/ipp/print/infra" 2>test/test-ippproxy.log &
ippproxy=$!
echo "ippproxy has PID $ippproxy..."

# Test the instance...
echo ""
echo "Running IPP 2.0 tests against IPP Everywhere PDF printer..."
libcups/tools/ipptool-static -V 2.0 -tIf libcups/examples/document-letter.pdf "ipp://localhost:$ippserverport/ipp/print/ipp-everywhere-pdf" libcups/examples/ipp-2.0.test || status=1

echo ""
echo "Running IPP 2.0 tests against infra printer..."
libcups/tools/ipptool-static -V 2.0 -tIf libcups/examples/document-letter.pdf "ipp://localhost:$ippserverport/ipp/print/infra" libcups/examples/ipp-2.0.test || status=1

echo ""
echo "Running IPP System Service tests..."
libcups/tools/ipptool-static -V 2.0 -tI "ipp://localhost:$ippserverport/ipp/system" examples/pwg5100.22.test || status=1

# Clean up
kill $ippeveprinter $ippproxy $ippserver

exit $status

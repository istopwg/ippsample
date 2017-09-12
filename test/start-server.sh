#!/usr/bin/env bash
#
#

# Specify where the ippserver binary resides
IPPSERVER="../server/ippserver"

# be very verbose
ARGS="-vvv"

# Configuration is in the current directory
ARGS="${ARGS} -C ."

# mDNS subtype for _ipp._tcp or _ipps._tcp (type depends on server config)
# _print == IPP Everywhere
# _universal == AirPrint
# Cannot be called multiple times (as of 2017-09-12)
ARGS="${ARGS} -r _print"



##########
#
# Run the configuration

echo ""
echo "Starting ippserver with the following arguments:"
echo ""
echo ${IPPSERVER} ${ARGS}
echo ""

${IPPSERVER} ${ARGS}



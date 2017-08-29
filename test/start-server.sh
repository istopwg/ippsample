#!/usr/bin/env bash
#
#

WHEREAMI=$(dirname "$0")
echo "WHEREAMI = \"${WHEREAMI}\""

# Check to make sure the spool directory is there
[ ! -d "${WHEREAMI}/spool" ] && mkdir "${WHEREAMI}/spool"

# Specify where the ippserver binary resides
IPPSERVER="${WHEREAMI}/../xcode/DerivedData/ippsample/Build/Products/Debug/ippserver"

# be very verbose
ARGS="-v"

# Configuration is in the current directory
ARGS="${ARGS} -C ."

# Bonjour subtype for _ipp._tcp or _ipps._tcp (type depends on server config)
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



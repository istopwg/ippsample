#!/bin/sh
#
# Script for running test scripts in the "tests" directory using the tools in
# this project.
#

if test $# != 2; then
	echo "Usage: ./runtests.sh xxx-tests.sh 'Name of Printer'"
	exit 1
fi

PATH="`pwd`/tools:$PATH"; export PATH
cd tests
sh "$@"

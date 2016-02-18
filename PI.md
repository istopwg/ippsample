# Raspberry Pi Build Instructions

All of this sample code will build on a Raspberry Pi, which can then be used as an
inexpensive print server or as part of an automated test framework.

Run the following command to install the software you'll need to build and use the
ippsample code:

    sudo apt-get install build-essentials libnss-mdns avahi-daemon avahi-utils avahi-client-devel gnutls-dev zlib-devel

If you haven't already done so, you can download the sources with Git using:

    git clone https://github.com/istopwg/ippsample.git ippsample

Then from the ippsample source directory, run the following commands to build everything:

    ./configure
    make

Right now there is no install target, but you'll find the programs in the "tools" and
"server" directories, and the documentation in the "doc" directory.

# Raspberry Pi Build/Usage Instructions

All of this sample code will build on a Raspberry Pi, which can then be used
as an inexpensive print server or as part of an automated test framework.


## Prerequisites

Run the following command to install the core software you'll need to build and
use the ippsample code:

    sudo apt-get install libnss-mdns avahi-daemon avahi-utils \
        libavahi-client-dev libgnutls28-dev zlib1g-dev

If you want to support local conversions of PDF and JPEG files to Apple/PWG
Raster or HP PCL, you'll also need version 1.11 of the MuPDF software and *not*
the much older version that has been packaged for Raspbian.  You can download
the MuPDF 1.11 software from:

    http://www.mupdf.com

For 3D support you'll need CuraEngine (part of Cura).  The easiest way to build
this is to head over to the Ultimaker Cura-build project:

    https://github.com/Ultimaker/cura-build

and mostly follow the Ubuntu/Linux instructions, doing a `make install` at the
end instead of `make package`.


## Building the IPP Sample Code

If you haven't already done so, you can download the latested IPP sample sources
with Git using:

    git clone https://github.com/istopwg/ippsample.git ippsample

Then, from the ippsample source directory, run the following commands to
build and install everything:

    ./configure
    make
    sudo make install


## Using the IPP Server

The `sample-configs` directory contains sample configuration directories for
`ippserver` to do different things.

> Work in progress here...

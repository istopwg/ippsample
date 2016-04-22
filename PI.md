# Raspberry Pi Build Instructions

All of this sample code will build on a Raspberry Pi, which can then be used as an
inexpensive print server or as part of an automated test framework.

Run the following command to install the software you'll need to build and use the
ippsample code:

    sudo apt-get install libnss-mdns avahi-daemon avahi-utils \
        libavahi-client-dev libgnutls28-dev zlib1g-dev

You'll also need version 1.8 or later of the MuPDF software (*not* the one that has been packaged for Raspbian which is much older) if you want to support local conversions of PDF and JPEG files to PWG Raster or HP PCL. You can download the MuPDF software from:

    http://www.mupdf.com

If you haven't already done so, you can download the IPP sample sources with Git using:

    git clone https://github.com/istopwg/ippsample.git ippsample

Then from the ippsample source directory, run the following commands to build and install everything:

    ./configure
    make
    sudo make install

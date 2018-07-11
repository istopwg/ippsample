Build Instructions for the IPP Sample Code
==========================================

This file describes how to compile and install the IPP sample code. For more
information on the IPP sample code see the file called `README.md`.  Changes are
summarized in the file `CHANGES.md`.


Prerequisites
-------------

You'll need an ANSI-compliant C compiler plus a make program and POSIX-compliant
shell (/bin/sh).  The GNU compiler tools and Bash work well and we have tested
the current IPP sample code against several versions of Clang and GCC with
excellent results.

The makefiles used by the project should work with most versions of make.  We've
tested them with GNU make as well as the make programs shipped by Compaq, HP,
SGI, and Sun.  BSD users should use GNU make (gmake) since BSD make does not
support "include".

Besides these tools you'll want the following libraries:

- Avahi (Linux) or mDNSResponder (all others) for Bonjour (DNS-SD) support
- CuraEngine for 3D support
- GNU TLS for encryption support on platforms other than iOS, macOS, or Windows
- LIBJPEG for JPEG support
- LIBPNG for PNG support
- MuPDF (1.11 or later) for PDF support on Linux
- ZLIB for compression support

On a stock Debian/Ubuntu install, the following command will install most of the
required prerequisites:

    sudo apt-get install build-essential autoconf avahi-daemon avahi-utils \
        cura-engine libavahi-client-dev libfreetype6-dev libgnutls28-dev \
        libharfbuzz-dev libjbig2dec0-dev libjpeg-dev libmupdf-dev libnss-mdns \
        libopenjp2-7-dev libpng-dev zlib1g-dev


Configuration
-------------

CUPS uses GNU autoconf, so you should find the usual `configure` script in the
main CUPS source directory.  To configure the code for your system, type:

    ./configure

The default installation will put the software in the `/usr/local` directory on
your system.  Use the `--prefix` option to install the software in another
location:

    ./configure --prefix=/some/directory

To see a complete list of configuration options, use the `--help` option:

    ./configure --help

If any of the dependent libraries are not installed in a system default location
(typically `/usr/include` and `/usr/lib`) you'll need to set the CFLAGS,
CPPFLAGS, CXXFLAGS, DSOFLAGS, and LDFLAGS environment variables prior to running
configure:

    CFLAGS="-I/some/directory" \
    CPPFLAGS="-I/some/directory" \
    CXXFLAGS="-I/some/directory" \
    DSOFLAGS="-L/some/directory" \
    LDFLAGS="-L/some/directory" \
    ./configure ...

Once you have configured things, just type:

    make ENTER

or if you have FreeBSD, NetBSD, or OpenBSD type:

    gmake ENTER

to build the software.


Installing the Software
-----------------------

Once you have built the software you need to install it.  The `install` target
provides a quick way to install the software on your local system:

    make install ENTER

or for FreeBSD, NetBSD, or OpenBSD:

    gmake install ENTER

Use the BUILDROOT variable to install to an alternate root directory:

    make BUILDROOT=/some/other/root/directory install ENTER


Getting Debug Logging
---------------------

The following environment variables are used to enable and control debug
logging:

- `CUPS_DEBUG_FILTER`: Specifies a POSIX regular expression to control which
  messages are logged.
- `CUPS_DEBUG_LEVEL`: Specifies a number from 0 to 9 to control the verbosity of
  the logging. The default level is 1.
- `CUPS_DEBUG_LOG`: Specifies a log file to use.  Specify the name "-" to send
  the messages to stderr.  Prefix a filename with "+" to append to an existing
  file.  You can include a single "%d" in the filename to embed the current
  process ID.

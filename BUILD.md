Build Instructions for the IPP Sample Code
==========================================

This file describes how to compile and install the IPP sample code. For more
information on the IPP sample code see the file called `README.md`.  Changes are
summarized in the file `CHANGES.md`.

If you are building the sample code on Windows, skip to the bottom of this page
to the section titled ["Building on Windows."](#building-on-windows)


Prerequisites
-------------

You'll need a C99-compliant C compiler plus a POSIX-compliant make program and
a POSIX-compliant shell (/bin/sh).  The GNU compiler tools and Bash work well
and we have tested the current IPP sample code against several versions of Clang
and GCC with excellent results.

The makefiles used by the project should work with most versions of make.  BSD
users should use GNU make (gmake) since BSD make does not support the POSIX
"include" directive.

Besides these tools you'll need the following libraries:

- Avahi or mDNSResponder for mDNS/DNS-SD support
- GNU TLS, LibreSSL, or OpenSSL for encryption support
- ZLIB for compression support
- CoreGraphics (macOS), Poppler, or Xpdf PDF rasterization support (optional)
- CuraEngine for 3MF/STL slicing support (optional)
- PAM for authentication support (optional)

On a stock Debian/Ubuntu install, the following command will install all
prerequisites:

    sudo apt-get install build-essential autoconf avahi-daemon avahi-utils \
        cura-engine libavahi-client-dev libnss-mdns libpam-dev libssl-dev \
        zlib1g-dev


Configuration
-------------

The IPP sample code uses GNU autoconf, so you should find the usual `configure`
script in the main source directory.  To configure the code for your system,
type:

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

to build the software.


Installing the Software
-----------------------

Once you have built the software you need to install it.  The `install` target
provides a quick way to install the software on your local system:

    make install ENTER

Use the BUILDROOT variable to install to an alternate root directory:

    make BUILDROOT=/some/other/root/directory install ENTER


Getting Debug Logging
---------------------

Pass the `--enable-debug` option to the configure script to enable debug logging
support.  The following environment variables are used to enable and control
debug logging at run-time:

- `CUPS_DEBUG_FILTER`: Specifies a POSIX regular expression to control which
  messages are logged.
- `CUPS_DEBUG_LEVEL`: Specifies a number from 0 to 9 to control the verbosity of
  the logging. The default level is 1.
- `CUPS_DEBUG_LOG`: Specifies a log file to use.  Specify the name "-" to send
  the messages to stderr.  Prefix a filename with "+" to append to an existing
  file.  You can include a single "%d" in the filename to embed the current
  process ID.


Building on Windows
-------------------

The IPP Sample Code includes a Visual Studio 2019+ solution file in the "vcnet"
directory called "ippsample.sln".  After opening the solution in Visual Studio
(VS 2022 users will need to "upgrade" the projects), please do the following
steps to ensure that the package dependencies are installed:

1. From the "Tools" menu select "Command-Line" and "Developer PowerShell" to
   open a PowerShell window in the "vcnet" directory.

2. Type the following commands:

    cd ..\libcups\vcnet
    nuget restore
    cd ..\pdfio
    nuget restore

3. You should then be able to build the solution (F7).

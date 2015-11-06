# IPP Everywhere Printer Self-Certification Tools

The IPP Everywhere Printer self-certification tools are used to test the conformance of printers to PWG Candidate Standard 5100.14-2013: IPP Everywhere. The testing and submission procedures are defined in the draft IPP Everywhere Printer Self-Certification Manual.

The [IPP Everywhere home page](http://www.pwg.org/ipp/everywhere.html) provides access to all information relevant to IPP Eveywhere. Sample PWG Raster files (needed for the document tests) can be downloaded from [the PWG FTP server](http://ftp.pwg.org/pub/pwg/ipp/examples] - only the files dated June 16, 2015 (20150616) can be used.

The "ippeveselfcert@pwg.org" mailing list is used to discuss IPP Everywhere Printer Self-Certification. You can subscribe at [https://www.pwg.org/mailman/listinfo/ippeveselfcert](https://www.pwg.org/mailman/listinfo/ippeveselfcert).

Issues found in the tools should be reported using the [Github issues page](http://github.com/istopwg/ippeveselfcert).


# Compiling and Packaging

## Linux

You'll need the Avahi and GNU TLS developer packages to provide DNS-SD and TLS support, clang or GCC, and GNU make. Packages are targeted for Red Hat Enterprise Linux and Ubuntu. Run the following to compile the tools:

    ./configure
    make

## OS X

You'll need the current Xcode software and command-line tools to build things. Run the following to compile the tools:

    ./configure
    make

## Windows

You'll need the current Visual Studio C++ as well as the code signing tools and the PWG code signing certificate (available from the PWG officers for official use only) - without the certificate the build will fail unless you disable the post-build events that add the code signatures.

Open the "ippeveselfcert.sln" file in the "vcnet" subdirectory and build the installer project.


# Running/Testing

## Linux and OS X

The "runtests.sh" script can be used to run any of the test scripts using the locally-built tools. For example:

    ./runtests.sh bonjour-tests.sh "Example Test Printer"
    ./runtests.sh ipp-tests.sh "Example Test Printer"
    ./runtests.sh document-tests.sh "Example Test Printer"

The corresponding PWG Raster sample files (see link in the introduction) MUST be placed in a subdirectory of the "tests" directory. For example:

    cd tests
    curl http://ftp.pwg.org/pub/pwg/ipp/examples/pwg-raster-samples-300dpi-20150616.zip >temp.zip
    unzip temp.zip
    rm temp.zip
    cd ..

## Windows

You'll need to manually copy the DLL and EXE files from the "vcnet" directory to the "tests" directory. Then run the corresponding test from that directory, for example:

    cd tests
    bonjour-tests.bat "Example Test Printer"
    ipp-tests.bat "Example Test Printer"
    document-tests.bat "Example Test Printer"

The corresponding PWG Raster sample files (see link in the introduction) MUST be placed in a subdirectory of the "tests" directory. After downloading the files just extract them using Windows Explorer.


# Packaging

## Linux

Run:

    make dist

A tar.gz file will be placed in the current directory.


## OS X

You'll need the PWG code signing certificate (available from the PWG officers for official use only) or your own certificate loaded into your login keychain.  Then run:

    CODESIGN_IDENTITY="common name or SHA-1 hash of certificate" make dist

## Windows

If you built the installer target, you'll find the package in a MSI file in the "vcnet" directory.


# Legal Stuff

These tools are Copyright 2014-2015 by The Printer Working Group and Copyright 2007-2015 by Apple Inc. CUPS and the CUPS logo are trademarks of Apple Inc.  PWG and IPP Everywhere are trademarks of the IEEE-ISTO.

The tools are provided under the terms of version 2 of the GNU General Public License and GNU Library General Public License. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the file "LICENSE.txt" for more information.

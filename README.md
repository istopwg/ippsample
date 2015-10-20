# IPP Everywhere Printer Self-Certification Tools

The IPP Everywhere Printer self-certification tools are used to test the conformance of printers to PWG Candidate Standard 5100.14-2013: IPP Everywhere. The testing and submission procedures are defined in the draft IPP Everywhere Printer Self-Certification Manual.

The IPP Everywhere home page provides access to all information relevant to IPP Eveywhere:

    http://www.pwg.org/ipp/everywhere.html

The "ippeveselfcert@pwg.org" mailing list is used to discuss IPP Everywhere Printer Self-Certification. You can subscribe from the following page:

    https://www.pwg.org/mailman/listinfo/ippeveselfcert

# Compiling and Packaging

## Linux

You'll need the Avahi and GNU TLS developer packages to provide DNS-SD and TLS support, clang or GCC, and GNU make. Packages are targeted for Red Hat Enterprise Linux and Ubuntu.

Run:

    ./configure
    make dist

## OS X

You'll need the current Xcode software and command-line tools to build things. Packaging require a code signing certificate from a valid Certificate Authority - a certificate from Apple is not sufficient for general distribution.

Run:

    ./configure
    CODESIGN_IDENTITY="common name" make dist

## Windows

You'll need the current Visual Studio C++ as well as the code signing tools and a code signing certificate from a valid Certificate Authority.

Open the "ippeveselfcert.sln" file in the "vcnet" subdirectory and build the installer project.


# Legal Stuff

These tools are Copyright 2014-2015 by The Printer Working Group and Copyright 2007-2015 by Apple Inc. CUPS and the CUPS logo are trademarks of Apple Inc.  PWG and IPP Everywhere are trademarks of the IEEE-ISTO.

The tools are provided under the terms of version 2 of the GNU General Public License and GNU Library General Public License. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the file "LICENSE.txt" for more information.

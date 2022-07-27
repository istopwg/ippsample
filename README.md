IPP Sample Implementations
==========================

This code provides sample, non-production-ready implementations of IPP Clients,
Printers, Proxies, and Systems.  It makes use of the [CUPS Library v3][1] and
[PDFio library][2] projects to provide low-level HTTP, IPP, and PDF support.
The `ippserver` and `ipp3dprinter` code was also inspired by the original CUPS
`ippeveprinter` source code.

![Version](https://img.shields.io/github/v/release/istopwg/ippsample?include_prereleases)
![Apache 2.0](https://img.shields.io/github/license/istopwg/ippsample)
![Build and Test](https://github.com/istopwg/ippsample/workflows/Build%20and%20Test/badge.svg)
[![ipp](https://snapcraft.io/ipp/badge.svg)](https://snapcraft.io/ipp)
[![Coverity Scan Status](https://img.shields.io/coverity/scan/22384.svg)](https://scan.coverity.com/projects/istopwg-ippsample)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/istopwg/ippsample.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/istopwg/ippsample/alerts/)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/istopwg/ippsample.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/istopwg/ippsample/context:cpp)

> Note: This code is provided for educational purposes only.  While we will make
> every effort to ensure the code is bug-free and regularly run the code
> through dynamic and static analysis tools, it is written for correctness, not
> performance, and so is not intended for use as a production solution.  This
> code is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF
> ANY KIND, either express or implied.


Getting the Code
----------------

From the Github sources, clone the repository with the `--recurse-submodules`
option *or* use the `git submodule` commands:

    git clone --recurse-submodules git@github.com:istopwg/ippsample.git

    git clone git@github.com:istopwg/ippsample.git
    git submodule init
    git submodule update

When updating an already-cloned repository:

    git pull
    git submodule update


Building the Code
-----------------

The IPP sample code uses a configure script on POSIX platforms to take care of
any platform differences:

    ./configure OPTIONS
    make

The following options are supported:

- `--enable-debug`: Enable debugging and debug logging.
- `--disable-shared`: Disable shared libraries.
- `--enable-maintainer`: Enable warnings as errors.
- `--enable-sanitizer`: Enable address sanitizer.
- `--enable-static`: Enable static libraries.
- `--prefix=PATH`: Configure the installation directory, the default is
  `/usr/local`.

On macOS, the "xcode" directory contains an Xcode workspace called
"ippsample.xcworkspace" that can be used to build the code.  Similarly, on
Windows the "vcnet" directory contains a Visual Studio solution called
"ippsample.sln" that can be used to build the code.


Testing the Code
----------------

The "test" target runs all of the unit tests and a full-up "system" test of the
various programs:

    make test


Resources
---------

The IPP sample code includes per-specification ipptool test files under the
"examples" directory.


ipp3dprinter
-------------

The `ipp3dprinter` program implements a single IPP 3D printer and can be
configured to use a print command to do processing of document data.


ippdoclint
----------

The `ippdoclint` program verifies and reports on document data.  It is primarily
used for testing IPP Clients with the `ippeveprinter` and `ippserver` programs.


ippproxy
--------

The `ippproxy` program implements a generic IPP Proxy interface that allows you
to connect a local IPP or PCL printer to an IPP Infrastructure Printer such as
the `ippserver` program.


ippserver
---------

The `ippserver` program implements the IPP System Service and provides a generic
IPP Printer interface that allows you to host shared printers using the IPP
Shared Infrastructure Extensions as well as support local printing or document
processing.


ipptransform
------------

The `ipptransform` program is a generic file conversion utility that is used
primarily with `ippeveprinter` and `ippserver` to support transformation of
JPEG, PNG, PDF, and text documents to PDF or raster data for IPP Everywhere™
and HP PCL printers.


ipptransform3d
--------------

The `ipptransform3d` program is a generic 3D file conversion utility that is
used primarily with `ippserver` to support 3MF, G-code, and STL printing to 3D
printers using the [CuraEngine][3] software.


Legal Stuff
-----------

Copyright © 2014-2022 by the IEEE-ISTO Printer Working Group.

Copyright © 2007-2019 by Apple Inc.

Copyright © 1997-2007 by Easy Software Products.

This software is provided under the terms of the Apache License, Version 2.0.
A copy of this license can be found in the file `LICENSE`.  Additional legal
information is provided in the file `NOTICE`.

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.


[1]: https://github.com/michaelrsweet/libcups
[2]: https://github.com/michaelrsweet/pdfio
[3]: https://github.com/Ultimaker/CuraEngine

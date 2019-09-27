IPP Sample Implementations
==========================

This code provides sample implementations of IPP Clients, Printers, Proxies, and
Systems.  It is largely based upon the [CUPS](https://www.cups.org/) software,
with substantial changes to the original `ippproxy` and `ippserver` sample code
to make them more general-purpose and configurable.

[![Travis Build Status](https://travis-ci.org/istopwg/ippsample.svg?branch=master)](https://travis-ci.org/istopwg/ippsample)
[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/0ofsfvaqk984tew9?svg=true)](https://ci.appveyor.com/project/michaelrsweet/ippsample)
[![Snap Status](https://build.snapcraft.io/badge/istopwg/ippsample.svg)](https://build.snapcraft.io/user/istopwg/ippsample)


ipp3dprinter
-------------

The `ipp3dprinter` program implements a single IPP 3D printer and can be
configured to use a print command to do processing of document data.


ippdoclint
----------

The `ippdoclint` program verifies and reports on document data.  It is primarily
used for testing IPP Clients with the `ippeveprinter` and `ippserver` programs.


ippeveprinter
-------------

The `ippeveprinter` program implements a single IPP Everywhere printer and can
be configured to use a print command to do processing of document data.  It is
included with the IPP Everywhere™ Printer Self-Certification tools.


ippfind
-------

The `ippfind` program implements mDNS+DNS-SD discovery of IPP printers and can
be used to find and test specific printers.  Among other things, it is used as
part of the IPP Everywhere™ Printer Self-Certification tools.


ipptool
-------

The `ipptool` program implements a generic IPP Client interface that allows a
user to send different IPP requests and act based on the response from the
Printer.  Among other things, it is used as part of the IPP Everywhere™ Printer
Self-Certification tools.


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
primarily with `ippeveprinter` and `ippserver` to support rasterization of JPEG
and PDF documents for IPP Everywhere™ and HP PCL printers.


ipptransform3d
--------------

The `ipptransform3d` program is a generic 3D file conversion utility that is
used primarily with `ippserver` to support 3MF and G-code printing to 3D
printers.


Legal Stuff
-----------

Copyright © 2014-2019 by the IEEE-ISTO Printer Working Group.
Copyright © 2007-2019 by Apple Inc.
Copyright © 1997-2007 by Easy Software Products.

This software is provided under the terms of the Apache License, Version 2.0.
A copy of this license can be found in the file `LICENSE`.  Additional legal
information is provided in the file `NOTICE`.

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.

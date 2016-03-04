# IPP Sample Implementations

This code provides sample implementations of IPP Clients, Printers, and Proxies.
It is largely based upon the [CUPS](https://www.cups.org/) software, with
substantial changes to the ippproxy and ippserver implementations to make them
more general-purpose and configurable.

## ippfind

The ippfind program implements Bonjour/DNS-SD discovery of IPP printers and can
be used to find and test specific printers.  Among other things, it is used as
part of the IPP Everywhere Printer Self-Certification test tools.

## ipptool

The ipptool program implements a generic IPP Client interface that allows a
user to send different IPP requests and act based on the response from the
Printer.  Among other things, it is used as part of the IPP Everywhere Printer
Self-Certification test tools.

## ippproxy

The ippproxy program implements a generic IPP Proxy interface that allows you to
connect a local IPP or PCL printer to an IPP Infrastructure Printer such as the
ippserver program.

## ippserver

The ippserver program implements a generic IPP Printer interface that allows you
to host shared printers using the IPP Shared Infrastructure Extensions as well
as support local printing or document processing.

## ipptransform

The ipptransform program is a generic file conversion utility that is used primarily with ippserver to support rasterization of JPEG and PDF documents for IPP Everywhere and HP PCL printers.

# Legal Stuff

This code is Copyright 2014-2016 by The Printer Working Group and Copyright
2007-2016 by Apple Inc.  CUPS and the CUPS logo are trademarks of Apple Inc.
PWG and IPP Everywhere are trademarks of the IEEE-ISTO.

The tools are provided under the terms of version 2 of the GNU General Public
License and GNU Library General Public License.  This program is distributed in
the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the file "LICENSE.txt" for more information.

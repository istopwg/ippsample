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

Copyright © 2014-2018 by the IEEE-ISTO Printer Working Group.
Copyright © 2007-2018 by Apple Inc.
Copyright © 1997-2007 by Easy Software Products.

CUPS is provided under the terms of the Apache License, Version 2.0.  A copy of
this license can be found in the file `LICENSE`.  Additional legal information
is provided in the file `NOTICE`.

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.

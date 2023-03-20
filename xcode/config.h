//
// Xcode configuration file for the IPP sample code.
//
// Copyright © 2014-2023 by the Printer Working Group.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef IPPSAMPLE_CONFIG_H
#define IPPSAMPLE_CONFIG_H


// Version number
#define IPPSAMPLE_VERSION "2023.04"


// PAM support
#define HAVE_LIBPAM 1
#define HAVE_SECURITY_PAM_APPL_H 1
/* #undef HAVE_PAM_PAM_APPL_H */


// strlcpy support
#define HAVE_STRLCPY 1


// DNS-SD support
#define HAVE_DNSSD 1
#define HAVE_MDNSRESPONDER 1
/* #undef HAVE_AVAHI */


// CoreGraphics support
#define HAVE_COREGRAPHICS 1


// pdftoppm path
/* #undef PDFTOPPM */


// CuraEngine path
#define CURAENGINE "/Applications/Ultimaker Cura.app/Contents/MacOS/CuraEngine"


#endif // !IPPSAMPLE_CONFIG_H

//
// Xcode configuration file for the IPP sample code.
//
// Copyright © 2014-2025 by the Printer Working Group.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef IPPSAMPLE_CONFIG_H
#define IPPSAMPLE_CONFIG_H


// Version number
#define IPPSAMPLE_VERSION "2023.10"


// PAM support
#define HAVE_LIBPAM 1
#define HAVE_SECURITY_PAM_APPL_H 1
/* #undef HAVE_PAM_PAM_APPL_H */


// PDF support
#define HAVE_COREGRAPHICS_H 1


// CuraEngine path
#define CURAENGINE "/Applications/Ultimaker Cura.app/Contents/MacOS/CuraEngine"


#endif // !IPPSAMPLE_CONFIG_H

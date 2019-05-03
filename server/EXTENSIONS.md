ippserver Extensions to IPP
===========================

This file documents the `ippserver`-specific extensions to IPP.  Each attribute
uses the "smi2699-" prefix, where "2699" is the ISTO Printer Working Group's
Private Enterprise Number.


Model
-----

`ippserver` implements the IPP System Service, which defines a Create-Printer
operation but no specific attributes or semantics for associating a Printer with
an Output Device.  The following additional Printer Description attributes are
defined for this purpose:

Attribute                              | Description
---------------------------------------|----------------------------
smi2699-auth-print-group (name(MAX))   | Access group for print users
smi2699-auth-proxy-group (name(MAX))   | Access group for proxy users
smi2699-device-command (name(MAX))     | Print command for device
smi2699-device-format (mimeMediaType)  | Print format for device
smi2699-device-name (name(MAX))        | Output device name
smi2699-device-uri (uri)               | Output device URI

The supported values for these attributes are exposed by the following System
Description attributes:

Attribute                                                | Description
---------------------------------------------------------|----------------------------
smi2699-auth-group-supported (1setOf name(MAX))          | List of supported groups
smi2699-device-command-supported (1setOf name(MAX))      | List of supported commands
smi2699-device-format-supported (1setOf mimeMediaType)   | List of supported device formats
smi2699-device-uri-schemes-supported (1setOf uriScheme)  | List of supported device URI schemes


IANA Registration Template
--------------------------

```
https://github.com/istopwg/ippsample/blob/master/server/EXTENSIONS.md


Printer Description attributes:                         Reference
------------------------------                          ---------
smi2699-auth-print-group (name(MAX))                    [IPPSERVER]
smi2699-auth-proxy-group (name(MAX))                    [IPPSERVER]
smi2699-device-command (name(MAX))                      [IPPSERVER]
smi2699-device-format (mimeMediaType)                   [IPPSERVER]
smi2699-device-name (name(MAX))                         [IPPSERVER]
smi2699-device-uri (uri)                                [IPPSERVER]

System Description attributes:                          Reference
------------------------------                          ---------
smi2699-auth-group-supported (1setOf name(MAX))         [IPPSERVER]
smi2699-device-command-supported (1setOf name(MAX))     [IPPSERVER]
smi2699-device-format-supported (1setOf mimeMediaType)  [IPPSERVER]
smi2699-device-uri-schemes-supported (1setOf uriScheme) [IPPSERVER]
```

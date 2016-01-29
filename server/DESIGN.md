# IPP Server Design Overview

This version of ippserver is much more complete than the one provided in the original CUPS sources while being simpler and easier to modify than the CUPS scheduler (cupsd). It supports:

- "Baked in" or file-based configuration;
- Multiple persistent print services/queues, both direct print (to IPP/IPPS URI) and printing via proxies (IPP Shared Infrastructure Extensions);
- Retained Jobs and Documents;
- Authentication using Pluggable Authentication Modules (PAM);
- Basic web interface (status monitoring only);
- Basic Document transforms (JPEG and PDF to PWG Raster);
- Hold/release;
- Release printing/fan-out;
- Local overrides of printer capabilities (ready media, duplex, color, etc.); and
- Operation as an on-demand service via launchd, systemd, etc.

The following are planned to be added as the corresponding specifications reach prototype quality:

- IPP System Service
- IPP 3D Printing Extensions

## Source Organization

The server is composed of several source files roughly organized by object or function:

- "ippserver.h": Common header file
- "client.c": IPP Client request processing
- "conf.c": Configuration file support
- "device.c": Output device support
- "ipp.c": IPP Printer request processing
- "job.c": Job object and processing
- "log.c": Logging
- "main.c": Main entry
- "printer.c": Printer object
- "subscription.c": Subscription object and event processing
- "transform.c": Document (format) transforms

## Configuration Files

Configuration files are placed in a directory specified on the command-line. The "system.conf" configuration file specifies server-wide settings, for example:

    "system.conf":
    DefaultPrinter foo
    LogLevel debug
    LogFile stderr

Print services (queues) are placed in a subdirectory called "print", for example:

    "print/foo.conf":
    DeviceURI ipp://foo.example.com/ipp/print
    Attribute collection media-col-ready {media-size={x-dimension=21590 y-dimension=27940}},{media-size={x-dimension=21000 y-dimension=29700}}
    Attribute keyword media-ready na_letter_8.5x11in,iso_a4_210x297mm

    "print/bar.conf":
    ProxyUser bar
    Attribute keyword job-hold-until-default indefinite

The first example ("print/foo.conf") creates a direct print queue at "ipp://servername:port/ipp/print/foo" that overrides the "media-col-ready" and "media-ready" attributes reported by the printer with US Letter and ISO A4 media.

The second example ("print/bar.conf") creates a proxy print queue at "ipp://servername:port/ipp/print/bar" that overrides the "job-hold-until-default" attribute so that all jobs are held by default. Any Proxy that authenticates using user "bar" can register to pull print jobs from this queue.


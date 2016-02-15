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
    Attr collection media-col-ready {media-size={x-dimension=21590 y-dimension=27940}},{media-size={x-dimension=21000 y-dimension=29700}}
    Attr keyword media-ready na_letter_8.5x11in,iso_a4_210x297mm

    "print/bar.conf":
    ProxyUser bar
    Attr keyword job-hold-until-default indefinite

The first example ("print/foo.conf") creates a direct print queue at "ipp://servername:port/ipp/print/foo" that overrides the "media-col-ready" and "media-ready" attributes reported by the printer with US Letter and ISO A4 media.

The second example ("print/bar.conf") creates a proxy print queue at "ipp://servername:port/ipp/print/bar" that overrides the "job-hold-until-default" attribute so that all jobs are held by default. Any Proxy that authenticates using user "bar" can register to pull print jobs from this queue.

## Job Commands

Commands are used both to process (print) a job and to transform/filter a job into a printable format. Each command receives the source (print) file on the command-line, for example:

    mycommand /tmp/ippserver.12345/foo/1-mydocument.pdf

The standard input is redirected from /dev/null. The standard output is directed to either /dev/null for printer commands or the destination file for transforms performed by ippserver. The standard error is directed back to ippserver over a pipe which allows the command to send messages that affect the printer and job state as well as messages for the server log.

The environment is inherited from ippserver with the following additional variables:

- "CONTENT_TYPE": The source file's MIME media type, for example "application/pdf".
- "DEVICE_URI": The destination device URI such as "ipp://foo.example.com/ipp/print" or "socket://bar.example.com". For printer commands only.
- "DOCUMENT_CHARSET": The source file's character set, if specified.
- "DOCUMENT_LANGUAGE": The source file's natural language, if specified.
- "DOCUMENT_NAME": The source file's name, if specified.
- "DOCUMENT_PASSWORD": The source file's password, if any.
- "IPP_name": Job attributes converted from "foo-bar" to "IPP_FOO_BAR". The value is a string version of the IPP attribute.
- "JOB_PASSWORD": The password to use when submitting the job, if any. For printer commands only.
- "JOB_PASSWORD_ENCRYPTION": The named hash to use when submitting the job, if any. For printer commands only.
- "OUTPUT_ORDER": The order of output pages, either "first-to-last" or "last-to-first".
- "OUTPUT_TYPE": The destination MIME media type, for example "image/pwg-raster".
- "PWG_RASTER_DOCUMENT_RESOLUTION_SUPPORTED": The list of comma-delimited resolutions that are supported by the output device.
- "PWG_RASTER_DOCUMENT_SHEET_BACK": The transform to apply to the back size image when producing duplex output.
- "PWG_RASTER_DOCUMENT_TYPE_SUPPORTED": The color spaces and bit depths that are supported by the output device.

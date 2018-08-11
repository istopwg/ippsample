# IPPDOCLINT

***IMPORTANT:This repo is just a proof-of-work for my participation in Google
Summer of Code 2018. This repo has been out of sync with the upstream repo
since I started working on this and will continue to remain so (at least the
 master branch) in the future so that my commits are clearly visible***

## Introduction

I have been selected as a student for the
[Google Summer of Code](https://summerofcode.withgoogle.com/) program to work
for The Linux Foundation, more specifically the
[OpenPrinting project](https://wiki.linuxfoundation.org/openprinting/start).
My project was to build a tool which takes in different printing file formats
and checks them for structural errors before sending it to the printer. This
idea was taken from an open
[issue](https://github.com/istopwg/ippsample/issues/29) in the
[ippsample](http://istopwg.github.io/ippsample/) project.

## Brief Summary

The proposed linter program will take as input common print file formats and
checks them for any structural or content errors. The linter should support
basic raster formats such as PWG and CUPS rasters along with JPEG and PDF
formats. The program can be used as a standalone program or as a command for
the ippserver program to check the document submitted along with a job. The
program also reports various job attributes such as job-impressions-xxx,
job-media-sheets-xxx, job-pages-xxx. The skeleton file for the program has
already been created by Michael R. Sweet from Apple Inc. and my work will
start from it and build on top of it.

## Build

Refer to
[BUILD.md](https://github.com/rithvikp1998/ippsample/blob/master/BUILD.md)

## Testing

```
Usage: ippdoclint [options] filename
Options:
  --help              Show program usage.
  --version           Show program version.
  -i content-type     Set MIME media type for file.
  -o name=value       Set print options.
  -v                  Be verbose.
```

## TODO

* Try to test with files from as many different sources as possible and look
for corner cases or exceptions.
* Merge the code into the upstream repo.

## Contact

* Me - rithvikp98@gmail.com
* Mentors:
    * Aveek Basu, Lexmark - aveek.basu@lexmark.com
    * Danny Brennan, IBM - brennand@us.ibm.com
    * Smith Kennedy, HP - smith.kennedy@hp.com
* Till Kamppeter, Head, OpenPrinting - till.kamppeter@gmail.com
* Michael Sweet, Apple - msweet@apple.com

## Thank You

Working with OpenPrinting has been extraordinary as before (this is my second
GSoC with them :) ). If anything, it was even better. I thank all my mentors
for guiding me to write good code and making me a better developer. And I
thank OpenPrinting for this excellent opportunity one more time. I will try my
best to keep contributing to OpenPrinting regularly and I will be looking
forward to working with them again.
# Docker Support for IPP Sample Code

This repository includes a sample Dockerfile
for compiling and running ippserver in a Docker container.

To run IPP sample code on Docker:

1. From a shell prompt in the directory (on Windows 10|2016, OS/X, or Linux) containing this docker file run:

   docker build -t ippsample .

You now can run the container with a bash or later on with one of the compiled IPP sample binaries.

   docker run -it ippsample bash

To start the IPP server:

1. In the ippserver container run:

    docker run -it ippsample ippserver -M byMyself -l rightHere -m coolPrinter -n myHost -p 631 -s 72 -vvvv myPrintService

2. OR to run the server in debug mode using gdb:

    docker run -it ippsample gdb ippserver
    run  -M byMyself -l rightHere -m coolPrinter -n myHost -p 631 -s 72 -vvvv myPrintService

Run the IPP Client:

1. From the bash command prompt on the IPP client container and in the /root/ippsample/examples directory with the IPP Server running, run:

    docker run --rm ippsample ippfind

    (Note the URL returned, e.g., ipp://f8a365cfc7ec.local:631/ipp/print)

    docker run --rm -it -w /root/ippsample/examples ippsample ipptool [URL returned] identify-printer-display.test

    (Note the "IDENTIFY from 172.17.0.4: Hello, World!"  message in stdout on the ippserver container)

2. To run the IPP everywhere tests on the IPP Client using setup from step #1, run:

    docker run --rm -it -w /root/ippsample/examples ippsample ipptool -V 2.0 -tf document-letter.pdf [URL returned] ipp-everywhere.test

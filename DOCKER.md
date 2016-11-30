# Docker Support for IPP Sample Code

This repository includes a sample Docker configuration file ("ippsample.docker")
for running ippserver in a Docker container.

To run IPP sample code on Docker:

1. From a shell prompt in the directory (on Windows 10|2016, OS/X, or Linux) containing this docker file run:

   docker build -t --security-opt seccomp=unconfined ubuntu[-ippserver | -ippclient] .
   docker run -it -v d:\DockerShare:/data --security-opt seccomp=unconfined ubuntu-[ippclient | ippserver] bash

2. From the bash prompt on the newly created container as root, start the services needed for Bonjour

    service dbus start
    service avahi-daemon start

To start the IPP server:

1. In the ippserver container run:

    ippserver -M byMyself -l rightHere -m coolPrinter -n myHost -p 631 -s 72 -vvvv myPrintService

2. OR to run the server in debug mode using gdb:

    gdb ippserver
    run  -M byMyself -l rightHere -m coolPrinter -n myHost -p 631 -s 72 -vvvv myPrintService

Run the IPP Client:

1. From the bash command prompt on the IPP client container and in the /root/ippsample/examples directory with the IPP Server running, run:

    ippfind

    (Note the URL returned, e.g., ipp://f8a365cfc7ec.local:631/ipp/print)

    ipptool [URL returned] identify-printer-display.test

    (Note the "IDENTIFY from 172.17.0.4: Hello, World!"  message in stdout on the ippserver container)

2. To run the IPP everywhere tests on the IPP Client using setup from step #1, run:

    ipptool -V 2.0 -tf document-letter.pdf [URL returned] ipp-everywhere.test

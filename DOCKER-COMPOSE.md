# Docker Compose Support for IPP Sample Code

This repository includes a sample docker-compose.yml file
to run a IPP server with IPP clients in multiple containers.

To start the IPP server:

    docker-compose up ippserver

Run the IPP Everywhere test:

    docker-compose up ipptest

To list all IPP printers:

    docker-compose up ippfind

To run the IPP proxy:

    docker-compose up ippproxy

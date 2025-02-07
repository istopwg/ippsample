#
# Docker container for IPP sample implementations.
#
# Copyright © 2014-2025 by the Printer Working Group.
#
# Licensed under Apache License v2.0.  See the file "LICENSE" for more
# information.
#

# Build using current Ubuntu...
FROM ubuntu:latest AS builder

ARG DEBIAN_FRONTEND=noninteractive
#RUN apt-get -qq update
RUN apt-get update
RUN apt-get upgrade -y
RUN apt-get install -y avahi-daemon avahi-utils curl man iputils-ping net-tools tcpdump vim
RUN apt-get install -y build-essential autoconf libavahi-client-dev libjpeg-dev libnss-mdns libpam-dev libpng-dev libssl-dev libusb-1.0-0-dev zlib1g-dev

# Copy source files to image
COPY . /root/ippsample/
WORKDIR /root/ippsample
RUN ./configure; test -f server/ippserver && make clean; make && make install


# Use latest Ubuntu for run-time image...
FROM ubuntu:latest
ARG DEBIAN_FRONTEND=noninteractive
#RUN apt-get -qq update
RUN apt-get update
RUN apt-get upgrade -y
RUN apt-get install -y avahi-daemon avahi-utils curl man iputils-ping net-tools tcpdump vim
COPY --from=builder /usr/local /usr/local

# Make changes necessary to run Avahi for DNS-SD support
RUN sed -ie 's/rlimit-nproc=3/rlimit-nproc=8/' /etc/avahi/avahi-daemon.conf
RUN update-rc.d dbus defaults
# RUN update-rc.d avahi-daemon defaults

# Create entrypoint.sh script to start dbus and avahi-daemon
RUN echo '#!/bin/bash\n\
service dbus start\n\
service avahi-daemon start\n\
$*\n\
' > /usr/bin/entrypoint.sh && chmod +x /usr/bin/entrypoint.sh
ENTRYPOINT ["/usr/bin/entrypoint.sh"]

# Export port 8000 for ippserver
EXPOSE 8000
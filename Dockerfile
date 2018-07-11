FROM ubuntu:latest

RUN apt-get update && apt-get install -y autoconf build-essential avahi-daemon avahi-utils \
    libavahi-client-dev libgnutls28-dev libjpeg-dev libnss-mdns libpng-dev \
    zlib1g-dev \
    net-tools iputils-ping vim avahi-daemon tcpdump man curl
RUN /bin/echo 'colorscheme blue' > ~/.vimrc
RUN /bin/echo "LS_COLORS=\$LS_COLORS:'di=0;31:' ; export LS_COLORS" >> /root/.bashrc
# Make changes necessary to run bonjour
RUN sed -ie 's/rlimit-nproc=3/rlimit-nproc=8/' /etc/avahi/avahi-daemon.conf
RUN update-rc.d dbus defaults
RUN update-rc.d avahi-daemon defaults

# Create entrypoint.sh script to start dbus and avahi-daemon
RUN echo '#!/bin/bash\n\
service dbus start\n\
service avahi-daemon start\n\
$*\n\
' > /usr/bin/entrypoint.sh && chmod +x /usr/bin/entrypoint.sh
ENTRYPOINT ["/usr/bin/entrypoint.sh"]

# Copy source configs to
COPY config* /root/ippsample/
COPY Make* /root/ippsample/
RUN cd /root/ippsample; ./configure

# Copy rest of the sources to use build cache as far as possible
COPY . /root/ippsample/
RUN cd /root/ippsample; make; make install

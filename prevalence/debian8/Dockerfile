FROM debian:8

# Debian 8 (Jessie) is EOL; repos are on archive.debian.org
RUN echo "deb http://archive.debian.org/debian jessie main contrib non-free" \
        > /etc/apt/sources.list && \
    echo "deb http://archive.debian.org/debian-security jessie/updates main" \
        >> /etc/apt/sources.list && \
    echo 'Acquire::Check-Valid-Until "false";' \
        > /etc/apt/apt.conf.d/99no-check-valid

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        --allow-unauthenticated \
        build-essential \
        task-gnome-desktop \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

CMD ["/bin/bash"]

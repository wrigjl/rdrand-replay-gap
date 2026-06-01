FROM debian:9

# Debian 9 (Stretch) is EOL; security support ended June 2022,
# LTS ended June 2024.  Repos are on archive.debian.org.
RUN echo "deb http://archive.debian.org/debian stretch main contrib non-free" \
        > /etc/apt/sources.list && \
    echo "deb http://archive.debian.org/debian-security stretch/updates main" \
        >> /etc/apt/sources.list && \
    echo 'Acquire::Check-Valid-Until "false";' \
        > /etc/apt/apt.conf.d/99no-check-valid && \
    echo 'Acquire::AllowInsecureRepositories "true";' \
        >> /etc/apt/apt.conf.d/99no-check-valid && \
    echo 'APT::Get::AllowUnauthenticated "true";' \
        >> /etc/apt/apt.conf.d/99no-check-valid

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        --allow-unauthenticated \
        build-essential \
        task-gnome-desktop \
        binutils \
        file \
        libbotan-1.10-1 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Scan scripts are copied in by the build context.
COPY scan_rdrand.sh /scan/scan_rdrand.sh
RUN chmod +x /scan/scan_rdrand.sh

CMD ["/bin/bash"]

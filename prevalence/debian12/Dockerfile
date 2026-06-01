FROM debian:12

# Debian 12 (Bookworm).  Regular support ends 2026-06-09; LTS
# until June 2028.  After regular-support EOL, switch the apt
# sources to security.debian.org's LTS repos.  After LTS EOL,
# switch to archive.debian.org following the debian8/9/10 pattern.

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        task-gnome-desktop \
        binutils \
        file \
        libbotan-2-19 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Scan scripts are copied in by the build context.
COPY scan_rdrand.sh /scan/scan_rdrand.sh
RUN chmod +x /scan/scan_rdrand.sh

CMD ["/bin/bash"]

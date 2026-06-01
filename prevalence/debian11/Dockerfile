FROM debian:11

# Debian 11 (Bullseye) is on LTS until 2026-08-31.  After that
# date, switch the apt sources to archive.debian.org following
# the pattern used in debian8/Dockerfile and debian9/Dockerfile.

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        task-gnome-desktop \
        binutils \
        file \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Scan scripts are copied in by the build context.
COPY scan_rdrand.sh /scan/scan_rdrand.sh
RUN chmod +x /scan/scan_rdrand.sh

CMD ["/bin/bash"]

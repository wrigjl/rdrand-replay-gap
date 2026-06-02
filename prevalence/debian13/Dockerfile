FROM debian:13

# Debian 13 (Trixie) — current stable release.  Regular support
# through approximately mid-2028; LTS through mid-2030.  No
# archive shenanigans needed.

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        task-gnome-desktop \
        binutils \
        file \
        libbotan-2-19 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Scan scripts are copied from the repo's canonical location.
# Build from the repo root: docker build -f prevalence/debian13/Dockerfile .
COPY scripts/scan_rdrand.sh /scan/scan_rdrand.sh
RUN chmod +x /scan/scan_rdrand.sh

CMD ["/bin/bash"]

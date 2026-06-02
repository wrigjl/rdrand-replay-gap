FROM debian:8

# Debian 8 (Jessie) is EOL; repos are on archive.debian.org.
#
# Two compatibility hacks are required to make apt usable here:
#
#  1. Old apt (jessie's apt 1.0.x) walks RLIMIT_NOFILE on every
#     fork to close FDs.  Modern Docker sets NOFILE to ~1
#     billion, which makes apt loop billions of fcntl(EBADF)
#     syscalls.  We cap NOFILE to 1024 inside the build.
#
#  2. jessie's Release-file GPG signatures use weak hashes that
#     modern gpgv either rejects or busy-loops on.  We replace
#     /usr/bin/gpgv with /bin/true to short-circuit signature
#     verification.  apt's "Acquire::AllowInsecureRepositories"
#     setting tolerates the missing-fingerprint warning that
#     remains.
#
# These hacks are reasonable here because:
#  - jessie is EOL; archive.debian.org explicitly does not
#    accept liability for the integrity of these snapshots.
#  - The build artifact is used for a prevalence scan, not as
#    a production install.
#
# Recommended invocation:
#
#   docker build --ulimit nofile=1024:524288 -t rdrand-debian8 .
#
# The in-shell `ulimit -n 1024` in the install RUN below is
# sufficient on its own to make apt usable.  Passing
# --ulimit nofile to the build command is belt-and-suspenders:
# it lowers the NOFILE soft limit before the Dockerfile's RUN
# instructions even start, which is useful if you ever add
# another apt-using RUN above the install step (where the
# in-shell ulimit has not yet been applied) and forget to
# include `ulimit -n 1024 &&` at the front of it.

RUN cp /bin/true /usr/bin/gpgv

RUN echo "deb http://archive.debian.org/debian jessie main contrib non-free" \
        > /etc/apt/sources.list && \
    echo "deb http://archive.debian.org/debian-security jessie/updates main" \
        >> /etc/apt/sources.list && \
    echo 'Acquire::Check-Valid-Until "false";' \
        > /etc/apt/apt.conf.d/99no-check-valid && \
    echo 'Acquire::AllowInsecureRepositories "true";' \
        >> /etc/apt/apt.conf.d/99no-check-valid && \
    echo 'APT::Get::AllowUnauthenticated "true";' \
        >> /etc/apt/apt.conf.d/99no-check-valid

RUN ulimit -n 1024 && \
    apt-get -o Acquire::http::Timeout=30 update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        --allow-unauthenticated \
        build-essential \
        task-gnome-desktop \
        binutils \
        file \
        libbotan-1.10-0 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Scan scripts are copied from the repo's canonical location.
# Build from the repo root: docker build -f prevalence/debian8/Dockerfile .
COPY scripts/scan_rdrand.sh /scan/scan_rdrand.sh
RUN chmod +x /scan/scan_rdrand.sh

CMD ["/bin/bash"]

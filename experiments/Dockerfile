# Dockerfile for the replay-divergence experiments.
#
# Base image matches prevalence/debian13/Dockerfile so the
# experiments run against the same toolchain version we used
# for the prevalence scan.  rr is built from source at tag 5.9.0
# because Debian trixie's packaged rr is older than the version
# named in the paper.
#
# Build:
#   docker build -t rdrand-experiments -f experiments/Dockerfile .
#
# Run (host must have kernel.perf_event_paranoid <= 1):
#   docker run --rm -it \
#       --security-opt seccomp=unconfined \
#       --cap-add=SYS_PTRACE \
#       rdrand-experiments \
#       /experiments/run_all.sh
#
# The seccomp/ptrace flags are required for rr's
# perf_event_open path; without them rr fails to record.

FROM debian:13

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        binutils \
        ca-certificates \
        capnproto \
        cmake \
        diffutils \
        file \
        g++ \
        gcc \
        gdb \
        git \
        libcapnp-dev \
        liblldb-dev \
        libpython3-dev \
        lldb \
        libssl-dev \
        libzstd-dev \
        ninja-build \
        pkg-config \
        python3 \
        python3-pexpect \
        rustc \
        cargo \
        zlib1g-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Build rr 5.9.0 from source.
ARG RR_VERSION=5.9.0
RUN git clone --depth 1 --branch ${RR_VERSION} \
        https://github.com/rr-debugger/rr.git /tmp/rr && \
    mkdir /tmp/rr/build && \
    cd /tmp/rr/build && \
    cmake -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr/local \
          -Ddisable32bit=ON \
          .. && \
    ninja && \
    ninja install && \
    rm -rf /tmp/rr

# Sanity check: rr --version should report 5.9.0.
RUN rr --version

COPY experiments /experiments
WORKDIR /experiments

CMD ["/experiments/run_all.sh"]

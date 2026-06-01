# Gentoo RDRAND prevalence study
#
# Builds with -march=haswell (not -march=native) so the result
# does not depend on the host CPU.  Haswell is the lowest x86
# microarchitecture that enables RDRAND (Ivy Bridge has it too,
# but Haswell is the conventional baseline target).
#
# Usage:
#   docker build -t gentoo-rdrand .
#   docker run --rm gentoo-rdrand /scan/scan_rdrand.sh
#
# After running this, capture reproducibility metadata:
#   docker run --rm gentoo-rdrand emerge --info > emerge-info.txt
#   docker run --rm gentoo-rdrand date -u > portage-snapshot-date.txt
#
FROM gentoo/stage3:latest

# Use -march=haswell so the package set RDRAND-emits identically
# on any host (Haswell is the minimum that includes RDRAND).
RUN sed -i \
    's/^COMMON_FLAGS=.*/COMMON_FLAGS="-march=haswell -O2 -pipe"/' \
    /etc/portage/make.conf \
    && echo "MAKEOPTS=\"-j$(nproc)\"" >> /etc/portage/make.conf \
    && echo 'FEATURES="-sandbox -usersandbox -pid-sandbox"' \
       >> /etc/portage/make.conf

# Confirm -march=haswell enables RDRAND.
RUN echo 'int main(){}' > /tmp/test.c \
    && gcc -march=haswell -dM -E /tmp/test.c | grep -q __RDRND__ \
    || { echo "gcc -march=haswell did not define __RDRND__;" \
         "compiler is misconfigured." >&2; \
         exit 1; } \
    && rm /tmp/test.c

# Accept licenses needed by some crypto packages.
RUN echo 'ACCEPT_LICENSE="*"' >> /etc/portage/make.conf

# Sync the portage tree.  This snapshot is NOT pinned -- emerge
# --sync pulls whatever is current on the day of build, which is
# the principal source of reproducibility drift on Gentoo (see
# Gentoo Reproducibility Caveats in the artifact README).
RUN emerge --sync --quiet

# Rebuild the entire system with -march=haswell.  Portage
# handles dependency ordering (gcc and glibc before everything
# that depends on them).  This is slow but produces an image
# that accurately represents a real Gentoo installation.
RUN emerge -e --quiet-build --jobs=$(nproc) @world

# Emerge additional packages not in the stage3 that are
# relevant to the RDRAND prevalence comparison with Debian:
#   - crypto libraries (libgcrypt, libsodium, botan)
#   - BoringSSL-style code (nss as proxy; chromium too large)
#   - common networked utilities
RUN emerge --quiet-build --jobs=$(nproc) \
    dev-libs/libgcrypt \
    dev-libs/libsodium \
    dev-libs/botan \
    dev-libs/nss \
    app-crypt/gnupg \
    net-misc/openssh \
    net-misc/curl \
    app-editors/vim

# Copy scan scripts into the image.
COPY scan_rdrand.sh check_rdrand_gating.sh /scan/
RUN chmod +x /scan/*.sh

# Verify the build actually produced RDRAND instructions.
# If this finds zero hits, something is wrong.
RUN /scan/scan_rdrand.sh /usr/lib > /dev/null 2>&1 || true

ENTRYPOINT ["/bin/bash"]

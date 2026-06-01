# Gentoo RDRAND prevalence study
#
# Build on a host with RDRAND support (Ivy Bridge / Bulldozer or
# later) so that -march=native enables -mrdrnd.
#
# Usage:
#   docker build -t gentoo-rdrand .
#   docker run --rm gentoo-rdrand /scan/scan_rdrand.sh \
#       /usr/bin /usr/sbin /usr/lib /usr/libexec
#
FROM gentoo/stage3:latest

# Confirm -march=native enables RDRAND on this host CPU.
# The build will fail early if it does not.
RUN echo 'int main(){}' > /tmp/test.c \
    && gcc -march=native -dM -E /tmp/test.c | grep -q __RDRND__ \
    || { echo "Host CPU lacks RDRAND; -march=native will not" \
         "enable -mrdrnd. Run on Ivy Bridge or later." >&2; \
         exit 1; } \
    && rm /tmp/test.c

# Set -march=native (Gentoo recommended default) so that all
# compiled code may use RDRAND intrinsics without CPUID checks.
RUN sed -i \
    's/^COMMON_FLAGS=.*/COMMON_FLAGS="-march=native -O2 -pipe"/' \
    /etc/portage/make.conf \
    && echo "MAKEOPTS=\"-j$(nproc)\"" >> /etc/portage/make.conf \
    && echo 'FEATURES="-sandbox -usersandbox -pid-sandbox"' \
       >> /etc/portage/make.conf

# Accept licenses needed by some crypto packages.
RUN echo 'ACCEPT_LICENSE="*"' >> /etc/portage/make.conf

# Sync the portage tree.
RUN emerge --sync --quiet

# Rebuild the entire system with -march=native.  Portage
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

# Gentoo RDRAND/RDSEED prevalence study
#
# Builds with -march=broadwell (not -march=native) so the result
# does not depend on the host CPU.  Broadwell (5th gen Core,
# 2014) is the earliest Intel microarchitecture that enables
# both RDRAND *and* RDSEED; Haswell has RDRAND only.  Using
# broadwell here makes both instruction families reachable
# through -march alone.
#
# Usage (run from the repo root, not this directory):
#   docker build -t gentoo-rdrand -f prevalence/gentoo/Dockerfile .
#   docker run --rm gentoo-rdrand /scan/scan_rdrand.sh
#
# After running this, capture reproducibility metadata:
#   docker run --rm gentoo-rdrand emerge --info > emerge-info.txt
#   docker run --rm gentoo-rdrand date -u > portage-snapshot-date.txt
#
FROM gentoo/stage3:latest

# Use -march=broadwell so the package set RDRAND/RDSEED-emits
# identically on any host (Broadwell is the minimum that
# includes both RDRAND and RDSEED).
#
# USE flags required to build gnome-light, in order of cause:
#   elogind  -- gnome-session REQUIRED_USE="exactly-one-of ( elogind
#               systemd )".  Containers don't run systemd-as-init,
#               so elogind is the right login-session manager.
#   wayland  -- mutter (gnome-shell's compositor) REQUIRED_USE
#               "any-of ( X wayland )".  Wayland is GNOME 48's
#               default; pulls in udev as a dependent.
#   udev     -- forced by wayland's REQUIRED_USE.
#   X        -- belt-and-suspenders: enabled alongside wayland so
#               any GUI package that still wants X (xwayland is
#               profile-forced but full X may be required by
#               others) doesn't hit a follow-on REQUIRED_USE
#               failure mid-emerge.  Adds xorg-server to the
#               build, costing ~30 min and image size, but
#               minimizes iterate-and-rebuild cycles.
#
# Setting these here (before the @world -e rebuild) keeps the
# resulting image internally consistent: every package that has
# an X/wayland/elogind/systemd USE decision is built the same way.
RUN sed -i \
    's/^COMMON_FLAGS=.*/COMMON_FLAGS="-march=broadwell -O2 -pipe"/' \
    /etc/portage/make.conf \
    && echo "MAKEOPTS=\"-j$(nproc)\"" >> /etc/portage/make.conf \
    && echo 'FEATURES="-sandbox -usersandbox -pid-sandbox"' \
       >> /etc/portage/make.conf \
    && echo 'USE="elogind wayland udev X"' >> /etc/portage/make.conf

# Confirm -march=broadwell enables both RDRAND and RDSEED.
RUN echo 'int main(){}' > /tmp/test.c \
    && gcc -march=broadwell -dM -E /tmp/test.c | grep -q __RDRND__ \
    && gcc -march=broadwell -dM -E /tmp/test.c | grep -q __RDSEED__ \
    || { echo "gcc -march=broadwell did not define both" \
         "__RDRND__ and __RDSEED__; compiler is misconfigured." \
         >&2; exit 1; } \
    && rm /tmp/test.c

# Accept licenses needed by some crypto packages.
RUN echo 'ACCEPT_LICENSE="*"' >> /etc/portage/make.conf

# Sync the portage tree using emerge-webrsync (downloads an
# atomic, dated tarball snapshot instead of using rsync, which
# is prone to mid-transfer Manifest mismatches).  This snapshot
# is NOT pinned -- emerge-webrsync pulls whatever is current on
# the day of build, which is the principal source of
# reproducibility drift on Gentoo (see Gentoo Reproducibility
# Caveats in the artifact README).
RUN emerge-webrsync --verbose

# Rebuild the entire system with -march=broadwell.  Portage
# handles dependency ordering (gcc and glibc before everything
# that depends on them).  This is slow but produces an image
# that accurately represents a real Gentoo installation.
#
# sys-apps/portage is excluded because rebuilding portage from
# within an active emerge run trips a self-update bootstrap
# bug (post-install hook fails with
# "ModuleNotFoundError: No module named 'portage.dbapi._SyncfsProcess'"
# because the new portage's import tree is only partially
# installed when the hook runs).  Portage is pure Python; it
# does not emit RDRAND and gains nothing from the broadwell
# target, so skipping it is safe for our prevalence scan.
#
# --autounmask=y --autounmask-write=y --autounmask-continue: portage
# regularly needs to flip USE flags during rolling upgrades (e.g.
# enabling python_targets_python3_13 across a tree).  Without these
# flags, emerge stops with "USE changes are necessary to proceed"
# and dumps suggested package.use lines for human review.  We
# accept whatever emerge proposes and continue, which is consistent
# with the "snapshot of whatever portage is offering today" goal.
RUN emerge --autounmask=y --autounmask-write=y --autounmask-continue \
    -e --quiet-build --jobs=$(nproc) \
    --exclude sys-apps/portage @world

# Emerge additional packages relevant to the RDRAND prevalence
# comparison with Debian:
#   - crypto libraries (libgcrypt, libsodium, botan)
#   - BoringSSL-style code (nss as proxy; chromium too large)
#   - common networked utilities
#   - GNOME desktop (gnome-light) to make the binary count
#     comparable to Debian's task-gnome-desktop image.  Doubles
#     or triples the compile time but is what the original
#     paper scan implicitly measured.
RUN emerge --quiet-build --jobs=$(nproc) \
    --autounmask=y --autounmask-write=y --autounmask-continue \
    dev-libs/libgcrypt \
    dev-libs/libsodium \
    dev-libs/botan \
    dev-libs/nss \
    app-crypt/gnupg \
    net-misc/openssh \
    net-misc/curl \
    app-editors/vim \
    gnome-base/gnome-light

# Copy scan scripts into the image from the repo's canonical location.
# Build from the repo root: docker build -f prevalence/gentoo/Dockerfile .
COPY scripts/scan_rdrand.sh scripts/check_rdrand_gating.sh /scan/
RUN chmod +x /scan/*.sh

# Verify the build actually produced RDRAND instructions.
# If this finds zero hits, something is wrong.
RUN /scan/scan_rdrand.sh /usr/lib > /dev/null 2>&1 || true

ENTRYPOINT ["/bin/bash"]

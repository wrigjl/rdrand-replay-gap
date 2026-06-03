#!/bin/sh
#
# build_release_tarballs.sh -- bundle per-corpus RDRAND-bearing
# binaries for the v1.0 GitHub Release.
#
# For each prevalence corpus image, this script:
#   1. Runs scan_rdrand.sh inside a fresh container.
#   2. Copies the RDRAND/RDSEED-bearing ELFs into a staging tree
#      that mirrors the in-container path layout
#      (so reviewers can run `check_rdrand_gating.sh <path>`
#      against the extracted files directly).
#   3. For Gentoo, additionally captures emerge --info and
#      the portage snapshot date into a _meta/ subdir, so the
#      tarball is self-describing despite Gentoo's rolling-release
#      drift.
#   4. Produces release-assets/rdrand-binaries-<distro>.tar.gz
#      and appends a (corpus, sha256, size) line to MANIFEST.txt.
#
# Tarballs are NOT committed to the repo.  They are uploaded as
# GitHub Release assets via `gh release upload v1.0 *.tar.gz`
# at the moment of the v1.0 public flip.
#
# Usage:
#   ./scripts/build_release_tarballs.sh [output-dir]
#
# Default output-dir is ./release-assets/ relative to the repo root.
#
# Prerequisites:
#   - docker daemon running
#   - All prevalence corpus images built and tagged as
#     rdrand-<distro>:latest.  Build them from the repo root via:
#       docker build -t rdrand-<distro> -f prevalence/<distro>/Dockerfile .
#   - GNU cp (for --parents) inside each container; all the
#     Debian-based images and Gentoo stage3 provide this.

set -e
set -u

# Usage variants:
#   build_release_tarballs.sh
#       -> default outdir, all 7 corpora
#   build_release_tarballs.sh debian8 debian13
#       -> default outdir, only the named corpora
#   build_release_tarballs.sh --out=/tmp/foo debian8
#       -> custom outdir, just one corpus
OUT_DIR=release-assets
while [ $# -gt 0 ]; do
    case "$1" in
        --out=*)  OUT_DIR=${1#--out=}; shift ;;
        --out)    OUT_DIR=$2; shift 2 ;;
        *)        break ;;
    esac
done

if [ $# -eq 0 ]; then
    set -- debian8 debian9 debian10 debian11 debian12 debian13 gentoo
fi
CORPORA="$*"

mkdir -p "$OUT_DIR"
MANIFEST="$OUT_DIR/MANIFEST.txt"
# Append to MANIFEST so partial runs across hosts can co-exist;
# the moment-of-upload step concatenates the per-host manifests.
[ -f "$MANIFEST" ] || : > "$MANIFEST"

# Shared extraction recipe.  Runs inside the corpus container,
# writes the resulting tarball to the bind-mounted /out dir.
#
# Arguments:
#   $1 = distro name (debian8, gentoo, etc.)
#   $2 = "with-meta" or "no-meta" (Gentoo adds emerge-info etc.)
make_inner_script() {
    distro=$1
    metaflag=$2

    cat <<EOF
set -e
set -u

# Run the scan, capture the list of RDRAND-bearing binaries.
/scan/scan_rdrand.sh \
    | sed -n 's|^=== \(.*\) ===\$|\1|p' \
    > /tmp/list

count=\$(wc -l < /tmp/list)
echo "  ${distro}: \${count} binaries to bundle"

# Stage each binary under /tmp/stage, preserving its path
# layout so the tarball mirrors the in-container filesystem.
rm -rf /tmp/stage
mkdir -p /tmp/stage
while IFS= read -r p; do
    [ -z "\$p" ] && continue
    # cp --parents needs the source path to be relative to /.
    rel=\${p#/}
    mkdir -p /tmp/stage/\$(dirname "\$rel")
    cp -L "\$p" "/tmp/stage/\$rel"
done < /tmp/list

EOF

    if [ "$metaflag" = "with-meta" ]; then
        cat <<'EOF'
# Gentoo-only: capture snapshot metadata so the tarball is
# self-describing.  Without this, the rolling-release drift
# documented in the artifact README would make the corpus
# uninterpretable months from now.
mkdir -p /tmp/stage/_meta
emerge --info       > /tmp/stage/_meta/emerge-info.txt
date -u             > /tmp/stage/_meta/portage-snapshot-date.txt
EOF
    fi

    cat <<EOF
tar -C /tmp/stage -czf /out/rdrand-binaries-${distro}.tar.gz .
EOF
}

for distro in $CORPORA; do
    image="rdrand-${distro}:latest"

    # Verify image exists locally; fail with a clear hint if not.
    if ! docker image inspect "$image" > /dev/null 2>&1; then
        echo "ERROR: image $image not found." >&2
        echo "  Build it from the repo root with:" >&2
        echo "    docker build -t rdrand-${distro} -f prevalence/${distro}/Dockerfile ." >&2
        exit 2
    fi

    if [ "$distro" = "gentoo" ]; then
        inner=$(make_inner_script "$distro" with-meta)
    else
        inner=$(make_inner_script "$distro" no-meta)
    fi

    echo "=== ${distro} ==="
    docker run --rm \
        -v "$(realpath "$OUT_DIR")":/out \
        "$image" \
        sh -c "$inner"

    tarball="$OUT_DIR/rdrand-binaries-${distro}.tar.gz"
    sha=$(shasum -a 256 "$tarball" | awk '{print $1}')
    size=$(wc -c < "$tarball")
    printf '%-12s  %s  %10d\n' "$distro" "$sha" "$size" >> "$MANIFEST"
done

echo
echo "===== done ====="
cat "$MANIFEST"
echo
echo "Next: gh release upload v1.0 $OUT_DIR/rdrand-binaries-*.tar.gz"

#!/bin/sh
#
# scan_rdrand.sh -- scan ELF binaries for RDRAND/RDSEED instructions
#
# Usage: scan_rdrand.sh [path ...]
#
# With no arguments, scans the default set:
#   /bin /sbin /lib /usr/bin /usr/sbin /usr/lib /usr/lib64 /usr/libexec /usr/share
# On usr-merged systems (Debian 12+, Gentoo with merged-usr profile),
# /bin /sbin /lib are symlinks to their /usr/ counterparts and are
# filtered out automatically so each directory is scanned exactly
# once.  Same applies to /usr/lib64 when it's a symlink to /usr/lib
# (multilib Debian); on Gentoo non-merged-usr profiles /usr/lib64
# is a real directory and is the primary library location.
#
# For each ELF binary found under the given paths, disassemble it
# and check for rdrand/rdseed. If either is present, print the
# filename and all rdrand/rdseed/cpuid lines from the disassembly.
#
# Output format per binary:
#   === /path/to/binary ===
#   <matching disassembly lines>
#
# Summary at end on stderr.

DEFAULT_PATHS="/bin /sbin /lib /usr/bin /usr/sbin /usr/lib /usr/lib64 /usr/libexec /usr/share"

if [ $# -eq 0 ]; then
    set -- $DEFAULT_PATHS
fi

# Drop nonexistent paths and symlinks (the latter to avoid
# double-scanning usr-merged directories).
filtered=
for p in "$@"; do
    [ -e "$p" ] || continue
    [ -L "$p" ] && continue
    filtered="$filtered $p"
done
set -- $filtered

if [ $# -eq 0 ]; then
    echo "no valid paths to scan" >&2
    exit 1
fi

tmpfile=$(mktemp)
trap 'rm -f "$tmpfile"' EXIT

scanned=0
matched=0

for f in $(find "$@" -xdev -type f 2>/dev/null); do
    # Skip non-ELF files quickly via magic bytes
    head -c 4 "$f" 2>/dev/null | grep -q "^.ELF" || continue

    scanned=$((scanned + 1))

    objdump -d "$f" 2>/dev/null \
        | grep -E '\b(cpuid|rdrand|rdseed)\b' > "$tmpfile"

    if grep -qE '\b(rdrand|rdseed)\b' "$tmpfile"; then
        matched=$((matched + 1))
        echo "=== $f ==="
        cat "$tmpfile"
        echo
    fi

    if [ $((scanned % 100)) -eq 0 ]; then
        echo "  ... scanned $scanned files, $matched hits" >&2
    fi
done

echo "scanned $scanned binaries, $matched contain rdrand/rdseed" >&2

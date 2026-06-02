#!/bin/bash
# Drive every experiments/exp*/run.sh and capture per-experiment
# output under experiments/exp*/expected/.
#
# Each run.sh produces:
#   * its own build/log/diff artifacts in the experiment dir
#   * one or more `RESULT:` lines parsed below
#
# We tee the full transcript into expected/transcript.log and
# extract the RESULT: lines into expected/summary.txt for easy
# diffing across hosts.

set -u
cd "$(dirname "$0")"

# Best-effort sanity check.  rr will fail noisily later if these
# are wrong, but a one-line check here saves a long Docker rebuild.
if ! command -v rr > /dev/null; then
    echo "ERROR: rr not found on PATH" >&2
    exit 2
fi

paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "?")
echo "host: $(uname -srm)"
echo "vendor: $(awk -F: '/vendor_id/ {print $2; exit}' /proc/cpuinfo | tr -d ' ')"
echo "rr: $(rr --version 2>&1 | head -1)"
echo "perf_event_paranoid: $paranoid"
echo

# In a fresh container we may not have a real rr-recordable PMU; check.
if [ "$paranoid" != "?" ] && [ "$paranoid" -gt 1 ] 2>/dev/null; then
    echo "WARNING: kernel.perf_event_paranoid=$paranoid (>1)." >&2
    echo "         rr requires <=1 on the host.  Continuing anyway." >&2
fi

experiments=$(find . -mindepth 2 -maxdepth 2 -name run.sh -type f -perm -u+x \
              | sed 's|^\./||; s|/run\.sh$||' | sort)
[ -z "$experiments" ] && { echo "no experiments found" >&2; exit 2; }

# Per-vendor subdir so runs on Intel and AMD hosts coexist in the
# committed artifact without overwriting each other.
case "$(awk -F: '/vendor_id/ {print $2; exit}' /proc/cpuinfo | tr -d ' ')" in
    GenuineIntel) host_tag=intel ;;
    AuthenticAMD) host_tag=amd ;;
    *)            host_tag=unknown ;;
esac
echo "host_tag: $host_tag"
echo

overall=0
for exp in $experiments; do
    out="$exp/expected/by-host/$host_tag"
    mkdir -p "$out"
    transcript="$out/transcript.log"
    summary="$out/summary.txt"

    echo "########## $exp ##########"
    # shellcheck disable=SC2024
    if ( cd "$exp" && ./run.sh ) > "$transcript" 2>&1; then
        :
    else
        echo "WARNING: $exp/run.sh exited non-zero" >&2
        overall=1
    fi

    grep '^RESULT:' "$transcript" > "$summary" || true
    cat "$summary"
    echo
done

echo "===== overall summary (host_tag=$host_tag) ====="
for exp in $experiments; do
    s="$exp/expected/by-host/$host_tag/summary.txt"
    [ -f "$s" ] && cat "$s"
done

exit $overall

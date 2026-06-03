#!/bin/sh
# Probe CPUID-faulting support on this host.
#
# Compiles probe.c and runs it twice if root is available:
# unprivileged first (covers everything except Intel MSR), then
# under sudo with the msr module loaded (covers the MSR layer).

set -u
cd "$(dirname "$0")"

gcc -O2 -Wall -o probe probe.c

echo "==================== unprivileged ===================="
./probe
echo

if [ "$(id -u)" -ne 0 ] && command -v sudo > /dev/null 2>&1; then
    echo "==================== root (Intel MSR layer) ===================="
    sudo -n modprobe msr 2>/dev/null || true
    if sudo -n ./probe 2>/dev/null; then
        :
    else
        echo "(sudo requires a password or msr module load failed; skipping)"
    fi
fi

#!/bin/bash
# Smoke test for exp1: std::random_device under rr.
#
# Library-path divergence demonstration (paper §4.3, related to
# exp4-openssl).  On Intel with CPUID-faulting available, rr
# suppresses the RDRAND CPUID bit and libstdc++ falls back to
# arc4random / getentropy: no divergence.  On AMD with Linux
# < 6.17 (no CPUID faulting), libstdc++ takes the RDRAND path
# and replay diverges silently.

set -u
cd "$(dirname "$0")"
# shellcheck source=../lib.sh
. "$(dirname "$0")/../lib.sh"

echo "=== exp1-random-device ==="

g++ -O2 -o exp1_random_device exp1_random_device.cpp

echo "--- objdump: rdrand / cpuid sites in libstdc++ caller ---"
objdump -d exp1_random_device | grep -E 'rdrand|cpuid' || echo "(none in main binary; library path is dynamic)"

rr record ./exp1_random_device 2> record.log
rec=$?
rr replay -a 2> replay.log
rep=$?

diverged=$(diverged_pair record.log replay.log)
errs=$(rr_error_count replay.log)

# Expected outcome depends on host vendor + kernel CPUID-faulting.
# We don't fail the smoke test on either outcome; the harness logs
# both.  See expected/README.md for the matrix.
case "$(host_cpu_vendor)" in
    intel) expected=faithful ;;
    amd)   expected=diverged ;;
    *)     expected=unknown ;;
esac

report_result exp1-random-device "$rec" "$rep" "$diverged" "$errs" "$expected"

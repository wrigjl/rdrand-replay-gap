#!/bin/bash
# Smoke test for exp4: library-internal RDRAND under rr.
#
# Three sub-experiments:
#   exp4-openssl  : libcrypto RAND_bytes()
#   exp4-stdrand  : libstdc++ std::random_device
#   exp7-stdrand  : std::random_device with explicit rdrand /
#                   rdseed tokens (probes whether libstdc++
#                   exposes the hardware path directly)
#
# Expected (paper §4.3, Table 3):
#   - Intel + CPUID-faulting: rr suppresses RDRAND bit, libraries
#     fall back to a non-RDRAND source -> faithful replay.
#   - AMD with Linux < 6.17: no CPUID faulting, library-internal
#     CPUID succeeds, RDRAND is reached directly -> diverges.

set -u
cd "$(dirname "$0")"
. "$(dirname "$0")/../lib.sh"

vendor=$(host_cpu_vendor)
case "$vendor" in
    intel) lib_expected=faithful ;;
    amd)   lib_expected=diverged ;;
    *)     lib_expected=unknown ;;
esac

#-----------------------------------------------------------------
echo "=== exp4-openssl ==="
gcc -O2 -o exp4_openssl exp4_openssl.c -lcrypto
rr record ./exp4_openssl > record_openssl.bin 2> record_openssl.log
rec=$?
rr replay -a > replay_openssl.bin 2> replay_openssl.log
rep=$?
div=$(diverged_pair record_openssl.bin replay_openssl.bin)
errs=$(rr_error_count replay_openssl.log)
report_result exp4-openssl "$rec" "$rep" "$div" "$errs" "$lib_expected"

#-----------------------------------------------------------------
echo "=== exp4-stdrand ==="
g++ -O2 -o exp4_stdrand exp4_stdrand.cpp
rr record ./exp4_stdrand > record_stdrand.bin 2> record_stdrand.log
rec=$?
rr replay -a > replay_stdrand.bin 2> replay_stdrand.log
rep=$?
div=$(diverged_pair record_stdrand.bin replay_stdrand.bin)
errs=$(rr_error_count replay_stdrand.log)
report_result exp4-stdrand "$rec" "$rep" "$div" "$errs" "$lib_expected"

#-----------------------------------------------------------------
echo "=== exp7-stdrand (explicit tokens) ==="
g++ -std=c++17 -O2 -o exp7_stdrand exp7_stdrand.cpp
rr record ./exp7_stdrand 2> record_exp7.log
rec=$?
rr replay -a 2> replay_exp7.log
rep=$?
div=$(diverged_pair record_exp7.log replay_exp7.log)
errs=$(rr_error_count replay_exp7.log)
# libstdc++ validates the explicit "rdrand"/"rdseed" tokens
# against CPUID at construction.  On Intel under rr, CPUID is
# suppressed and the token constructors throw deterministic
# exceptions -> faithful replay.  On AMD without CPUID-faulting,
# CPUID succeeds, RDRAND/RDSEED are emitted directly -> divergence.
# (The default-token portion of the program also depends on
# vendor: Intel falls back to a non-RDRAND source, AMD reaches
# RDRAND.)
report_result exp7-stdrand "$rec" "$rep" "$div" "$errs" "$lib_expected"

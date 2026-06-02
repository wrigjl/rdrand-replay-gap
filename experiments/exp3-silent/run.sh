#!/bin/bash
# Smoke test for exp3: silent ciphertext divergence (paper §5.1).
#
# Uses -mrdrnd intrinsic for the key, then XOR-encrypts a fixed
# plaintext.  The program is deliberately tick-stable: no branch
# on the RDRAND carry flag, fixed-length hex output via raw
# write().  Replay should exit 0 with no rr errors, yet stdout
# (ciphertext) differs from the recorded run.

set -u
cd "$(dirname "$0")"
. "$(dirname "$0")/../lib.sh"

echo "=== exp3-silent ==="
gcc -O2 -mrdrnd -o exp3_silent exp3_silent.c

rr record ./exp3_silent > record.bin 2> record.log
rec=$?
rr replay -a > replay.bin 2> replay.log
rep=$?

diverged_out=$(diverged_pair record.bin replay.bin)
diverged_err=$(diverged_pair record.log replay.log)
errs=$(rr_error_count replay.log)

# We treat the experiment as "diverged" if EITHER stdout (ciphertext)
# or stderr (key) differs.  Both should differ when RDRAND is silent.
if [ "$diverged_out" = yes ] || [ "$diverged_err" = yes ]; then
    diverged=yes
else
    diverged=no
fi

report_result exp3-silent "$rec" "$rep" "$diverged" "$errs" diverged
echo "--- detail: stdout_diverged=$diverged_out stderr_diverged=$diverged_err ---"

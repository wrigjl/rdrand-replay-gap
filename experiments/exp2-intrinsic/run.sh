#!/bin/bash
# Smoke test for exp2: -mrdrnd intrinsic vs control build.
#
# Runs both sub-experiments (intrinsic, control) and reports
# each separately.  Paper §4.2:
#   - intrinsic build (-mrdrnd) always diverges under rr.
#   - control build (runtime CPUID) is faithful on Intel where
#     rr's CPUID suppression works, and diverges on AMD < 6.17.

set -u
cd "$(dirname "$0")"
. "$(dirname "$0")/../lib.sh"

echo "=== exp2-intrinsic (intrinsic build) ==="
gcc -O2 -mrdrnd -o exp2_intrinsic exp2_intrinsic.c
echo "--- objdump: rdrand / cpuid sites ---"
objdump -d exp2_intrinsic | grep -E 'rdrand|cpuid' || echo "(none)"

rr record ./exp2_intrinsic 2> record_intrinsic.log
rec_i=$?
rr replay -a 2> replay_intrinsic.log
rep_i=$?
div_i=$(diverged_pair record_intrinsic.log replay_intrinsic.log)
err_i=$(rr_error_count replay_intrinsic.log)
report_result exp2-intrinsic "$rec_i" "$rep_i" "$div_i" "$err_i" diverged

echo "=== exp2-control (CPUID-gated build) ==="
gcc -O2 -o exp2_control exp2_intrinsic.c
echo "--- objdump: rdrand / cpuid sites ---"
objdump -d exp2_control | grep -E 'rdrand|cpuid' || echo "(none)"

rr record ./exp2_control 2> record_control.log
rec_c=$?
rr replay -a 2> replay_control.log
rep_c=$?
div_c=$(diverged_pair record_control.log replay_control.log)
err_c=$(rr_error_count replay_control.log)

case "$(host_cpu_vendor)" in
    intel) exp_c=faithful ;;
    amd)   exp_c=diverged ;;
    *)     exp_c=unknown ;;
esac
report_result exp2-control "$rec_c" "$rep_c" "$div_c" "$err_c" "$exp_c"

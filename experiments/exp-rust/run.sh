#!/bin/bash
# Smoke test for exp-rust: Rust target-feature=+rdrand.
#
# Two builds:
#   default              -> runtime CPUID check before RDRAND.
#                            Intel: rr suppresses CPUID -> faithful.
#                            AMD < 6.17: CPUID succeeds -> diverged.
#   target-feature=+rdrand -> compile-time elision of the CPUID
#                            check; RDRAND emitted unconditionally.
#                            Always diverges under rr.

set -u
cd "$(dirname "$0")"
. "$(dirname "$0")/../lib.sh"

vendor=$(host_cpu_vendor)
case "$vendor" in
    intel) default_expected=faithful ;;
    amd)   default_expected=diverged ;;
    *)     default_expected=unknown ;;
esac

bin=target/release/exp_rust

#-----------------------------------------------------------------
echo "=== exp-rust (default build) ==="
cargo build --release --quiet
echo "--- objdump: rdrand / cpuid sites (default) ---"
objdump -d "$bin" | grep -E 'rdrand|cpuid' || echo "(none)"

rr record "./$bin" 2> record_default.log
rec=$?
rr replay -a 2> replay_default.log
rep=$?
div=$(diverged_pair record_default.log replay_default.log)
errs=$(rr_error_count replay_default.log)
report_result exp-rust-default "$rec" "$rep" "$div" "$errs" "$default_expected"

#-----------------------------------------------------------------
echo "=== exp-rust (target-feature=+rdrand) ==="
# Force a fresh build with the target feature flag.
cargo clean --quiet
RUSTFLAGS="-C target-feature=+rdrand" cargo build --release --quiet
echo "--- objdump: rdrand / cpuid sites (+rdrand) ---"
objdump -d "$bin" | grep -E 'rdrand|cpuid' || echo "(none)"

rr record "./$bin" 2> record_tf.log
rec=$?
rr replay -a 2> replay_tf.log
rep=$?
div=$(diverged_pair record_tf.log replay_tf.log)
errs=$(rr_error_count replay_tf.log)
report_result exp-rust-tf-rdrand "$rec" "$rep" "$div" "$errs" diverged

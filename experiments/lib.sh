# Shared helpers for experiment smoke-test scripts.
#
# Source from each experiments/exp*/run.sh.  Provides a single
# function, report_result, that emits a one-line RESULT: record
# consumed by run_all.sh.
#
# Convention:
#   - record.log, replay.log capture rr's stderr.
#   - record.bin, replay.bin capture the tracee's stdout when the
#     experiment cares about stdout divergence; otherwise omit.
#   - Each experiment declares its expected outcome in the
#     EXPECTED variable before calling report_result.

# host_cpu_vendor: prints "intel", "amd", or "unknown".
host_cpu_vendor() {
    case "$(awk -F: '/vendor_id/ {print $2; exit}' /proc/cpuinfo 2>/dev/null | tr -d ' ')" in
        GenuineIntel) echo intel ;;
        AuthenticAMD) echo amd ;;
        *)            echo unknown ;;
    esac
}

# diverged_pair file_a file_b -> echoes "yes" or "no"
diverged_pair() {
    if diff -q "$1" "$2" > /dev/null 2>&1; then
        echo no
    else
        echo yes
    fi
}

# rr_error_count file -> count of error/FATAL lines in rr log
# (awk over grep -c so we always emit exactly one integer: grep -c
# both prints "0" and exits non-zero on zero matches, which when
# combined with `|| echo 0` would emit "0\n0" and break printf.)
rr_error_count() {
    awk '/error|FATAL/ {c++} END {print c+0}' "$1" 2>/dev/null
}

# report_result name exit_record exit_replay diverged rr_errors expected
# Emits a single machine-parseable summary line.
report_result() {
    printf 'RESULT: name=%s exit_record=%s exit_replay=%s diverged=%s rr_errors=%s expected=%s vendor=%s\n' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$(host_cpu_vendor)"
}

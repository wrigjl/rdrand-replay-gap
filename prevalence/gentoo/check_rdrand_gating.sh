#!/bin/sh
#
# check_rdrand_gating.sh -- analyze RDRAND/RDSEED gating in ELF binaries
#
# Usage: check_rdrand_gating.sh binary [binary ...]
#
# For each binary containing RDRAND/RDSEED, determines whether the
# instruction appears within a function that also contains CPUID
# (suggesting a runtime feature gate) or stands alone (ungated).
#
# Heuristic: disassemble the binary, split into functions (by
# objdump's "<name>:" labels), and for each function containing
# rdrand or rdseed, check whether cpuid also appears in that
# same function.
#
# Output per binary (stdout):
#   GATED   /path  (all RDRAND/RDSEED in CPUID-containing functions)
#   UNGATED /path  (at least one RDRAND/RDSEED without CPUID)
#   MIXED   /path  (some gated, some ungated)
#   NONE    /path  (no RDRAND/RDSEED found)
#
# Per-function detail on stderr.

if [ $# -eq 0 ]; then
    echo "usage: $0 binary [binary ...]" >&2
    exit 1
fi

total=0
gated=0
ungated=0
mixed=0
none=0

for f in "$@"; do
    [ -f "$f" ] || continue

    # Pipe disassembly directly into awk; avoid storing in variable.
    # Use tab-delimited mnemonic field to match instructions.
    result=$(objdump -d "$f" 2>/dev/null | awk '
    # Function header: "addr <name>:"
    /<.*>:$/ {
        if (func != "" && has_rdrand) {
            if (has_cpuid)
                printf "  GATED   %s\n", func
            else
                printf "  UNGATED %s\n", func
        }
        func = $0
        has_rdrand = 0
        has_cpuid = 0
        next
    }

    # Match instruction mnemonics in disassembly lines.
    # objdump format: "  addr:\t<bytes>\t<mnemonic> <operands>"
    # The mnemonic appears after the second tab.
    /\trdrand / || /\trdseed / { has_rdrand = 1 }
    /\tcpuid/                   { has_cpuid = 1 }

    END {
        if (func != "" && has_rdrand) {
            if (has_cpuid)
                printf "  GATED   %s\n", func
            else
                printf "  UNGATED %s\n", func
        }
    }
    ')

    total=$((total + 1))

    if [ -z "$result" ]; then
        echo "NONE    $f"
        none=$((none + 1))
        continue
    fi

    n_gated=$(echo "$result" | grep -c '^ *GATED')
    n_ungated=$(echo "$result" | grep -c '^ *UNGATED')

    if [ "$n_ungated" -eq 0 ]; then
        echo "GATED   $f  ($n_gated functions)"
        gated=$((gated + 1))
    elif [ "$n_gated" -eq 0 ]; then
        echo "UNGATED $f  ($n_ungated functions)"
        ungated=$((ungated + 1))
    else
        echo "MIXED   $f  ($n_gated gated, $n_ungated ungated)"
        mixed=$((mixed + 1))
    fi

    # Detail to stderr
    echo "$result" >&2
done

echo "" >&2
echo "--- summary ---" >&2
echo "Analyzed:  $total binaries" >&2
echo "GATED:     $gated" >&2
echo "UNGATED:   $ungated" >&2
echo "MIXED:     $mixed" >&2
echo "NONE:      $none" >&2

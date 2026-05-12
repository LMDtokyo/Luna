#!/usr/bin/env bash
# lint_tiers.sh — verify Luna tier discipline.
#
# Walks std/, compiler/, tools/, examples/ and enforces:
#   1. File's declared tier matches its path.
#   2. Modules at tier N may import only modules at tiers <= N.
#   3. Modules at T2-T5 may not shell out (no shell_run/popen/system).
#   4. extern "C" outside T6+ is rejected (undeclared FFI).
#
# Files without a `# tier: TN` header are treated as legacy and
# skipped, unless LUNA_LINT_REQUIRE_TIER=1 is set (used after phase 7
# of the migration; see docs/ARCHITECTURE.md).
#
# Exit codes: 0 clean, 1 violations, 2 internal error.

set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

REQUIRE_TIER="${LUNA_LINT_REQUIRE_TIER:-0}"

# ---------- helpers ----------

# Map a path (relative to repo root) to the tier its folder mandates.
# Empty output = path is outside the layered tree (linter ignores it).
expected_tier_for_path() {
    case "$1" in
        std/runtime/*)         echo 2 ;;
        std/core/*)            echo 3 ;;
        std/std/*)             echo 4 ;;
        std/net/*)             echo 5 ;;
        std/ext/*)             echo 6 ;;
        tools/*)               echo 7 ;;
        examples/*)            echo 8 ;;
        compiler/bootstrap/*)  echo 0 ;;
        compiler/seed/*)       echo 1 ;;
        compiler/core/*)       echo 1 ;;
        *)                     echo  ;;
    esac
}

# Read first ~10 lines of file, extract `# tier: TN` value (just the N).
declared_tier_for_file() {
    head -n 10 "$1" 2>/dev/null \
        | grep -m1 -E '^[[:space:]]*#[[:space:]]*tier:[[:space:]]*T[0-8][[:space:]]*$' \
        | sed -E 's/.*T([0-8]).*/\1/'
}

# Same, for `# ffi:` value (everything after the colon, trimmed).
declared_ffi_for_file() {
    head -n 10 "$1" 2>/dev/null \
        | grep -m1 -E '^[[:space:]]*#[[:space:]]*ffi:' \
        | sed -E 's/^[[:space:]]*#[[:space:]]*ffi:[[:space:]]*//' \
        | sed -E 's/[[:space:]]+$//'
}

# ---------- pass 1: index modules by name -> tier ----------
# Walk std/ only (compiler/tools/examples don't expose importable names).

declare -A MOD_TIER     # module_name -> tier number
declare -A MOD_PATH     # module_name -> path (for diagnostics)
DUP_VIOLATIONS=0

while IFS= read -r f; do
    tier=$(expected_tier_for_path "$f")
    [ -z "$tier" ] && continue
    name="$(basename "$f" .luna)"
    if [ -n "${MOD_TIER[$name]:-}" ]; then
        prev_tier="${MOD_TIER[$name]}"
        prev_path="${MOD_PATH[$name]}"
        echo "::error::duplicate module name '$name': T${prev_tier} ${prev_path} vs T${tier} ${f}"
        DUP_VIOLATIONS=$((DUP_VIOLATIONS + 1))
        # keep the lower-tier entry (first wins) for upcall checks below
    else
        MOD_TIER[$name]=$tier
        MOD_PATH[$name]=$f
    fi
done < <(find std -type f -name '*.luna' 2>/dev/null | sort)

# ---------- pass 2: per-file checks ----------

VIOLATIONS=$DUP_VIOLATIONS
FILES_CHECKED=0
FILES_SKIPPED=0

check_file() {
    local f="$1"
    local expected declared
    expected=$(expected_tier_for_path "$f")
    [ -z "$expected" ] && return    # outside layered tree
    declared=$(declared_tier_for_file "$f")

    # 0) header presence
    if [ -z "$declared" ]; then
        if [ "$REQUIRE_TIER" = "1" ]; then
            echo "::error file=$f::missing '# tier: TN' header"
            VIOLATIONS=$((VIOLATIONS + 1))
        else
            FILES_SKIPPED=$((FILES_SKIPPED + 1))
        fi
        return
    fi

    FILES_CHECKED=$((FILES_CHECKED + 1))

    # 1) declared tier must match path
    if [ "$declared" != "$expected" ]; then
        echo "::error file=$f::declared T${declared} but path requires T${expected}"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi

    # 2) shell-out forbidden at T2-T5
    if [ "$declared" -ge 2 ] && [ "$declared" -le 5 ]; then
        local hits
        hits=$(grep -nE '(^|[^A-Za-z_])(shell_run|shell_capture|popen|system)[[:space:]]*\(' "$f" \
               | grep -vE '^[[:digit:]]+:[[:space:]]*#' || true)
        if [ -n "$hits" ]; then
            echo "::error file=$f::T${declared} module may not shell out (only syscalls allowed):"
            echo "$hits" | sed 's/^/    /'
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    fi

    # 3) extern "C" requires FFI declaration; only allowed at T6+
    if grep -qE '^[[:space:]]*extern[[:space:]]+"C"' "$f"; then
        local ffi
        ffi=$(declared_ffi_for_file "$f")
        if [ "$declared" -lt 6 ]; then
            echo "::error file=$f::extern \"C\" found but T${declared} forbids FFI (move to std/ext/, T6)"
            VIOLATIONS=$((VIOLATIONS + 1))
        elif [ -z "$ffi" ] || [ "$ffi" = "none" ]; then
            echo "::error file=$f::extern \"C\" used but '# ffi:' is missing or 'none'"
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    fi

    # 4) imports — flag upcalls
    while IFS= read -r imp; do
        [ -z "$imp" ] && continue
        local tgt="${MOD_TIER[$imp]:-}"
        [ -z "$tgt" ] && continue       # unknown name — external/legacy, can't judge
        if [ "$tgt" -gt "$declared" ]; then
            echo "::error file=$f::T${declared} imports '$imp' at T${tgt} — upcalls forbidden"
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    done < <(grep -E '^[[:space:]]*import[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' "$f" 2>/dev/null \
             | awk '{print $2}')
}

# Walk all in-scope files. Honour an optional positional list for
# spot-checks (e.g. `tools/lint_tiers.sh std/std/json.luna`).
if [ "$#" -gt 0 ]; then
    for f in "$@"; do
        check_file "$f"
    done
else
    while IFS= read -r f; do
        check_file "$f"
    done < <(find std compiler tools examples -type f -name '*.luna' 2>/dev/null | sort)
fi

# ---------- summary ----------

modules_indexed=${#MOD_TIER[@]}
echo
echo "lint_tiers: ${modules_indexed} module(s) indexed under std/, ${FILES_CHECKED} file(s) checked, ${FILES_SKIPPED} skipped (no tier header), ${VIOLATIONS} violation(s)"

if [ "$VIOLATIONS" -gt 0 ]; then
    exit 1
fi
exit 0

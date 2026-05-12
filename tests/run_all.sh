#!/usr/bin/env bash
# Walk tests/ and run every *_test.luna via the installed `luna` CLI.
#
# Usage (from repo root):
#     bash tests/run_all.sh                 # everything
#     bash tests/run_all.sh std/core        # one tier
#     bash tests/run_all.sh std/core/env    # one module (omit _test suffix)
#
# LUNA_PATH is set so `import foo` finds modules in the repo's std/
# tree without requiring `install.sh` to have run first. This makes
# the new layout testable in-checkout.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS_DIR="$ROOT/tests"
LUNA="${LUNA:-luna}"

if ! command -v "$LUNA" >/dev/null 2>&1; then
    echo "luna not on PATH — run install.sh first or export PATH=\"\$HOME/.luna/bin:\$PATH\"" >&2
    exit 2
fi

# Search path: every layered tier in std/, plus std/ext subdirectories.
# Order is high → low so a duplicated module name resolves to the
# higher-tier copy (which would also be flagged by lint_tiers).
LUNA_PATH_NEW="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext"
for sub in "$ROOT"/std/ext/*/; do
    [ -d "$sub" ] || continue
    LUNA_PATH_NEW="$LUNA_PATH_NEW:${sub%/}"
done

# Preserve any caller-supplied LUNA_PATH (prepended for override).
if [ -n "${LUNA_PATH:-}" ]; then
    export LUNA_PATH="$LUNA_PATH:$LUNA_PATH_NEW"
else
    export LUNA_PATH="$LUNA_PATH_NEW"
fi

# Resolve test scope: optional positional arg.
SCOPE="${1:-}"
if [ -n "$SCOPE" ]; then
    if [ -d "$TESTS_DIR/$SCOPE" ]; then
        find_root="$TESTS_DIR/$SCOPE"
        find_pat='*_test.luna'
    else
        # Treat as module name: tests/std/core/env -> tests/std/core/env_test.luna
        explicit="$TESTS_DIR/${SCOPE}_test.luna"
        [ -f "$explicit" ] || { echo "no test for scope: $SCOPE" >&2; exit 2; }
        find_root="$(dirname "$explicit")"
        find_pat="$(basename "$explicit")"
    fi
else
    find_root="$TESTS_DIR"
    find_pat='*_test.luna'
fi

total_pass=0
total_fail=0
total_modules=0

while IFS= read -r tf; do
    [ -f "$tf" ] || continue
    rel="${tf#$TESTS_DIR/}"
    name="${rel%_test.luna}"
    total_modules=$((total_modules + 1))

    if ! out=$("$LUNA" run "$tf" 2>&1); then
        echo "[$name] RUN FAILED"
        echo "$out" | sed 's/^/    /'
        total_fail=$((total_fail + 1))
        continue
    fi

    summary=$(echo "$out" | grep -E '^=== .* PASS, .* FAIL ===$' | tail -1)
    if [[ "$summary" =~ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL ]]; then
        p="${BASH_REMATCH[1]}"
        f="${BASH_REMATCH[2]}"
        total_pass=$((total_pass + p))
        total_fail=$((total_fail + f))
        if [ "$f" -gt 0 ]; then
            echo "[$name] $p PASS / $f FAIL"
            echo "$out" | grep FAIL | sed 's/^/    /'
        else
            echo "[$name] $p PASS"
        fi
    else
        echo "[$name] no summary line — output was:"
        echo "$out" | sed 's/^/    /'
        total_fail=$((total_fail + 1))
    fi
done < <(find "$find_root" -type f -name "$find_pat" | sort)

echo
echo "=== tests: $total_modules modules, $total_pass PASS, $total_fail FAIL ==="
exit "$total_fail"

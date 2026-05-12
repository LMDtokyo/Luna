#!/usr/bin/env bash
# tests/pkg/e2e.sh — end-to-end exercise of `luna pkg`.
#
# Builds a tiny fake "library" git repo, then in a fresh project
# directory runs init/add/list/build/run/remove/sync and verifies
# the output at each step.
#
# Usage (from repo root):
#     bash tests/pkg/e2e.sh

set -u

command -v luna >/dev/null 2>&1 || { echo "luna not on PATH, skipping" >&2; exit 77; }
command -v git  >/dev/null 2>&1 || { echo "git not installed, skipping"  >&2; exit 77; }

WORK="$(mktemp -d -t luna-pkg-e2e-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

LIB_REPO="$WORK/greet-repo"
APP_DIR="$WORK/app"

mkdir -p "$LIB_REPO/src/lib"
cat > "$LIB_REPO/src/lib/greet.luna" <<'EOF'
# Tiny library — exports a single greet() function.
fn greet(@who: int) -> int
    @prefix = "Hello, "
    @suffix = "!"
    return str_concat(str_concat(@prefix, @who), @suffix)
EOF

# Local git repo, branch "main".
(
    cd "$LIB_REPO"
    git -c init.defaultBranch=main init --quiet
    git -c user.name=test -c user.email=test@test config user.email test@test
    git -c user.name=test -c user.email=test@test config user.name test
    git add .
    git -c user.name=test -c user.email=test@test commit --quiet -m "v0.1.0"
    git branch -M main
)

step() { printf "\n--- %s ---\n" "$1"; }
fail() { echo "FAIL: $1" >&2; exit 1; }

# --- App project setup --------------------------------------------------
mkdir -p "$APP_DIR"
cd "$APP_DIR"

step "luna pkg init"
luna pkg init || fail "init failed"
[ -f luna.toml ] || fail "luna.toml not created"
[ -d .luna/pkgs ] || fail ".luna/pkgs/ not created"
grep -q '^\.luna/' .gitignore || fail ".gitignore not updated"

step "luna pkg add file://$LIB_REPO"
luna pkg add "file://$LIB_REPO@main" || fail "add failed"
[ -d .luna/pkgs/greet-repo ] || fail "pkg dir not created"
[ -f .luna/pkgs/greet-repo/src/lib/greet.luna ] || fail "lib file missing"
[ -f luna.lock ] || fail "luna.lock not created"
grep -q "file://$LIB_REPO" luna.lock || fail "lock entry missing"

step "luna pkg list"
out=$(luna pkg list)
echo "$out" | grep -q 'greet-repo' || fail "list missing greet-repo"

step "luna build/run with the dep"
cat > main.luna <<'EOF'
import greet

fn main() -> int
    shine(greet("Luna"))
    return 0
EOF

run_out=$(luna run main.luna 2>&1)
rc=$?
echo "$run_out"
[ "$rc" -eq 0 ] || fail "luna run failed (exit $rc)"
echo "$run_out" | grep -qF 'Hello, Luna!' || fail "expected greeting not found"

step "luna pkg remove"
luna pkg remove greet-repo || fail "remove failed"
[ ! -d .luna/pkgs/greet-repo ] || fail "pkg dir still present"
grep -q 'greet-repo' luna.toml && fail "luna.toml still has entry"

step "luna pkg sync (rehydrate from luna.toml)"
# Put the entry back manually to test sync (remove cleaned it out).
cat >> luna.toml <<EOF
"file://$LIB_REPO" = "main"
EOF
luna pkg sync || fail "sync failed"
[ -d .luna/pkgs/greet-repo ] || fail "sync did not rehydrate"

step "rebuild after sync"
luna run main.luna 2>&1 | grep -qF 'Hello, Luna!' || fail "post-sync run failed"

echo
echo "pkg e2e: OK"

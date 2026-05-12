#!/usr/bin/env bash
# postgres_e2e.sh — spawn a sandbox PostgreSQL cluster on
# 127.0.0.1:15432, run the Luna postgres client test suite against it,
# stop the cluster.
#
# Skips with exit 77 if `initdb` / `pg_ctl` / `pg_isready` aren't on
# PATH. On Debian/Ubuntu they live under /usr/lib/postgresql/<ver>/bin/
# and are not symlinked by default — add that to PATH (or install
# postgresql-common) if you want this test to run.
#
# Usage (from repo root):
#     bash tests/std/ext/db/postgres_e2e.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
TEST="$ROOT/tests/std/ext/db/postgres_test.luna"
PORT=15432

# Allow Debian/Ubuntu's versioned binary dir if the user hasn't already
# put it on PATH. Pick the highest available version.
if ! command -v initdb >/dev/null 2>&1; then
    for d in /usr/lib/postgresql/*/bin; do
        if [ -x "$d/initdb" ]; then
            export PATH="$d:$PATH"
        fi
    done
fi

for bin in initdb pg_ctl pg_isready; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        echo "postgres_e2e: $bin not installed, skipping" >&2
        exit 77
    fi
done

if ! command -v luna >/dev/null 2>&1; then
    echo "postgres_e2e: luna not on PATH, skipping" >&2
    exit 77
fi

WORK="$(mktemp -d -t luna-pg-test-XXXXXX)"
DATA="$WORK/data"
LOG="$WORK/pg.log"

cleanup() {
    if [ -f "$DATA/postmaster.pid" ]; then
        pg_ctl -D "$DATA" -m immediate stop >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

# 1. initdb — quiet, trust auth, no locale to keep it portable.
if ! initdb -D "$DATA" -A trust -U lunatest --no-locale -E UTF8 \
        >"$WORK/initdb.log" 2>&1; then
    echo "postgres_e2e: initdb failed:" >&2
    cat "$WORK/initdb.log" >&2
    exit 1
fi

# 2. Override pg_hba.conf to be 100% trust on localhost (initdb's
# default is already trust because of -A trust above, but be explicit
# so this works even on distros that ship a different template).
cat > "$DATA/pg_hba.conf" <<'EOF'
# Sandbox: trust auth from localhost only — never use in production.
local   all             all                                     trust
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
EOF

# 3. Start. listen on localhost only, the chosen port, no unix socket
# clutter outside the data dir.
if ! pg_ctl -D "$DATA" -l "$LOG" -w -t 30 \
        -o "-p $PORT -h 127.0.0.1 -k $WORK" \
        start >/dev/null 2>&1; then
    echo "postgres_e2e: pg_ctl start failed:" >&2
    tail -n 50 "$LOG" >&2 || true
    exit 1
fi

# 4. Sanity check via pg_isready.
if ! pg_isready -h 127.0.0.1 -p "$PORT" -U lunatest >/dev/null 2>&1; then
    echo "postgres_e2e: pg_isready says not ready" >&2
    exit 1
fi

# 5. Create the test database (initdb only made postgres + lunatest's
# personal db is missing — `createdb` would do it but we want the
# minimal dependency surface, so use psql via pg_ctl's running cluster
# only if it's available; otherwise CREATE DATABASE through the postgres
# system db using PGPASSWORD-less trust auth).
if command -v createdb >/dev/null 2>&1; then
    createdb -h 127.0.0.1 -p "$PORT" -U lunatest lunatest >/dev/null 2>&1 || true
else
    # Fallback: psql is in the same bin dir as pg_ctl on every distro.
    psql -h 127.0.0.1 -p "$PORT" -U lunatest -d postgres \
        -c "CREATE DATABASE lunatest;" >/dev/null 2>&1 || true
fi

export LUNA_PATH="$ROOT/std/runtime:$ROOT/std/core:$ROOT/std/std:$ROOT/std/net:$ROOT/std/ext:$ROOT/std/ext/db:$ROOT/std/ext/crypto"

out=$(luna run "$TEST" 2>&1)
rc=$?

echo "$out"
if [ "$rc" -ne 0 ]; then
    echo "postgres_e2e: test exit=$rc"
    exit 1
fi

summary=$(echo "$out" | grep -E '^=== postgres: ' | tail -1)
if [[ ! "$summary" =~ ^===\ postgres:\ ([0-9]+)\ PASS,\ ([0-9]+)\ FAIL\ ===$ ]]; then
    echo "postgres_e2e: no summary line"
    exit 1
fi
p="${BASH_REMATCH[1]}"
f="${BASH_REMATCH[2]}"
if [ "$f" != "0" ]; then
    echo "postgres_e2e: $f failures"
    exit 1
fi
if [ "$p" -lt 10 ]; then
    echo "postgres_e2e: only $p assertions — sandbox probably wasn't reached"
    exit 1
fi
echo "postgres_e2e: OK ($p assertions)"

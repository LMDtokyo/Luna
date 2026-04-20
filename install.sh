#!/usr/bin/env bash
# Luna installer — Linux / WSL.
#
# Usage:
#   bash install.sh           # install from this checkout
#   curl -fsSL <url>/install.sh | bash   # install latest (future)
#
# Installs into ~/.luna/ by default:
#   ~/.luna/bin/luna                the CLI wrapper
#   ~/.luna/bin/luna-mini           the ELF64 self-hosted compiler
#   ~/.luna/lib/prelude.luna        stdlib auto-included in every build
#
# Adds ~/.luna/bin to PATH via .bashrc / .zshrc / .profile (whichever
# the user's shell uses) and re-sources the rc so the `luna` command
# is available in the current shell after install finishes.

set -euo pipefail

LUNA_HOME="${LUNA_HOME:-$HOME/.luna}"
BIN_DIR="$LUNA_HOME/bin"
LIB_DIR="$LUNA_HOME/lib"
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"

SRC_COMPILER="$REPO_ROOT/src/bootminor/luna-mini.elf"
SRC_PRELUDE="$REPO_ROOT/src/bootminor/bootminor_prelude.luna"
SRC_CLI="$REPO_ROOT/src/bootminor/luna"

# Sanity checks before we touch the user's home directory.
if [ ! -f "$SRC_COMPILER" ]; then
    echo "error: $SRC_COMPILER not found." >&2
    echo "       Run from a Luna checkout, or fetch the release archive first." >&2
    exit 1
fi
if [ ! -f "$SRC_PRELUDE" ]; then
    echo "error: $SRC_PRELUDE not found." >&2
    exit 1
fi
if [ ! -f "$SRC_CLI" ]; then
    echo "error: $SRC_CLI not found." >&2
    exit 1
fi

echo "Installing Luna into $LUNA_HOME ..."

mkdir -p "$BIN_DIR" "$LIB_DIR"

install -m 0755 "$SRC_COMPILER" "$BIN_DIR/luna-mini"
install -m 0755 "$SRC_CLI"      "$BIN_DIR/luna"
install -m 0644 "$SRC_PRELUDE"  "$LIB_DIR/prelude.luna"

# Determine which shell rc file to update.
shell_name="$(basename "${SHELL:-/bin/sh}")"
case "$shell_name" in
    bash)  rc_file="$HOME/.bashrc" ;;
    zsh)   rc_file="$HOME/.zshrc" ;;
    fish)  rc_file="$HOME/.config/fish/config.fish" ;;
    *)     rc_file="$HOME/.profile" ;;
esac

# Idempotent PATH injection — only append if not already present.
path_line='export PATH="$HOME/.luna/bin:$PATH"'
fish_line='set -Ux PATH $HOME/.luna/bin $PATH'

if [ "$shell_name" = "fish" ]; then
    if ! grep -Fq "$HOME/.luna/bin" "$rc_file" 2>/dev/null; then
        mkdir -p "$(dirname "$rc_file")"
        echo "" >> "$rc_file"
        echo "# Luna (added by install.sh)" >> "$rc_file"
        echo "$fish_line" >> "$rc_file"
        added=1
    fi
else
    if ! grep -Fq ".luna/bin" "$rc_file" 2>/dev/null; then
        echo "" >> "$rc_file"
        echo "# Luna (added by install.sh)" >> "$rc_file"
        echo "$path_line" >> "$rc_file"
        added=1
    fi
fi

# Stamp install metadata so later `luna upgrade` can verify.
cat > "$LUNA_HOME/install.json" <<EOF
{
  "version": "0.1.0-bootminor",
  "compiler_path": "$BIN_DIR/luna-mini",
  "prelude_path":  "$LIB_DIR/prelude.luna",
  "installed_at":  "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

echo
echo "Luna installed."
echo "    compiler: $BIN_DIR/luna-mini"
echo "    CLI:      $BIN_DIR/luna"
echo "    prelude:  $LIB_DIR/prelude.luna"
echo

if [ "${added:-0}" = "1" ]; then
    echo "Added $BIN_DIR to PATH in $rc_file."
    echo "Open a new terminal, or run:"
    echo "    source $rc_file"
else
    echo "$BIN_DIR already on PATH; nothing to change."
fi
echo
echo "Quick test:"
echo "    luna new hello"
echo "    cd hello"
echo "    luna run"

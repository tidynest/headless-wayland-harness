#!/usr/bin/env bash
# Build the vp driver and put `hgui` on PATH (~/.local/bin).
# Re-run after pulling changes to rebuild the binary.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BIN="$HOME/.local/bin"

echo "==> Checking dependencies"
missing=()
for c in gcc pkg-config wayland-scanner sway grim wtype; do
  command -v "$c" >/dev/null || missing+=("$c")
done
if [ ${#missing[@]} -gt 0 ]; then
  echo "Missing: ${missing[*]}" >&2
  echo "On Arch: sudo pacman -S --needed gcc pkgconf wayland sway grim wtype" >&2
  exit 1
fi
pkg-config --exists wayland-client || { echo "wayland-client dev files missing" >&2; exit 1; }

echo "==> Generating protocol bindings + building vp"
mkdir -p "$HERE/build"
XML="$HERE/protocol/wlr-virtual-pointer-unstable-v1.xml"
[ -f "$XML" ] || { echo "missing $XML" >&2; exit 1; }
wayland-scanner client-header "$XML" "$HERE/build/vp-client.h"
wayland-scanner private-code  "$XML" "$HERE/build/vp-code.c"
gcc "$HERE/src/vp.c" "$HERE/build/vp-code.c" -I"$HERE/build" -o "$HERE/build/vp" \
    $(pkg-config --cflags --libs wayland-client)
echo "    built $HERE/build/vp"

echo "==> Linking hgui into $BIN"
mkdir -p "$BIN"
chmod +x "$HERE/hgui"
ln -sf "$HERE/hgui" "$BIN/hgui"
echo "    $BIN/hgui -> $HERE/hgui"

case ":$PATH:" in
  *":$BIN:"*) ;;
  *) echo "NOTE: $BIN is not on PATH — add it in your shell rc." ;;
esac
echo "==> Done. Try: hgui --help"

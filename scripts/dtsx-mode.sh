#!/usr/bin/env bash
# Select the active speaker profile (requires a PipeWire restart to take effect).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CONFDIR=${XDG_CONFIG_HOME:-$HOME/.config}
DTSX_CONF="$CONFDIR/dtsx"
mode=${1:-}
case "$mode" in
  music|movie|voice|game1|game2)
    src="$ROOT/configs/speakers-$mode.json" ;;
  headphones)
    src="$ROOT/configs/headphones-music.json" ;;
  *)
    echo "usage: $0 music|movie|voice|game1|game2|headphones" >&2
    exit 2 ;;
esac
[ -f "$src" ] || { echo "missing $src (run make / build_configs.py)" >&2; exit 2; }
mkdir -p "$DTSX_CONF"
if [ "$mode" = headphones ]; then
  cp "$src" "$DTSX_CONF/headphones.json"
  echo "active headphone profile: $mode"
else
  cp "$src" "$DTSX_CONF/speakers.json"
  echo "active speaker profile: $mode"
fi
echo "Restart PipeWire to reload the plugin:"
echo "  systemctl --user restart pipewire pipewire-pulse wireplumber"

#!/usr/bin/env bash
# Install dtsx for the current user. Never overwrites unrelated audio config.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PREFIX=${PREFIX:-$HOME/.local}
CONFDIR=${XDG_CONFIG_HOME:-$HOME/.config}
BIN_DIR="$PREFIX/bin"
PLUGIN_DIR="$PREFIX/lib/ladspa"
DTSX_CONF="$CONFDIR/dtsx"
PROFILES="$DTSX_CONF/profiles"
DROPIN="$CONFDIR/pipewire/pipewire.conf.d"
SYSTEMD_USER="$CONFDIR/systemd/user/pipewire.service.d"

ENABLE=0
FORCE=0
RESTART=0
for arg in "$@"; do
  case "$arg" in
    --enable) ENABLE=1 ;;
    --force) FORCE=1 ;;
    --restart) RESTART=1 ;;
    -h|--help)
      sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

echo "dtsx install"
echo "  project : $ROOT"
echo "  prefix  : $PREFIX"
echo "  config  : $DTSX_CONF"

make -C "$ROOT" all

mkdir -p "$BIN_DIR" "$PLUGIN_DIR" "$PROFILES" "$DTSX_CONF"
install -m 755 "$ROOT/build/dtsdsp" "$BIN_DIR/dtsdsp"
install -m 755 "$ROOT/build/dtsx_ladspa.so" "$PLUGIN_DIR/dtsx_ladspa.so"

for f in "$ROOT"/configs/speakers-*.json "$ROOT"/configs/headphones-*.json; do
  install -m 644 "$f" "$PROFILES/$(basename "$f")"
done

# Active profiles are only created when absent unless --force is given, so a
# user's edited profile is never silently replaced.
copy_active() {
  local src=$1 dst=$2
  if [ -e "$dst" ] && [ "$FORCE" -eq 0 ]; then
    echo "  keep existing $dst (use --force to replace)"
  else
    install -m 644 "$src" "$dst"
    echo "  active profile $dst"
  fi
}
copy_active "$ROOT/configs/speakers-music.json" "$DTSX_CONF/speakers.json"
copy_active "$ROOT/configs/headphones-music.json" "$DTSX_CONF/headphones.json"

echo "  binary  : $BIN_DIR/dtsdsp"
echo "  plugin  : $PLUGIN_DIR/dtsx_ladspa.so"

if [ "$ENABLE" -eq 1 ]; then
  mkdir -p "$DROPIN" "$SYSTEMD_USER"
  # PipeWire's LADSPA loader only searches LADSPA_PATH (dir + name + .so).
  cat > "$SYSTEMD_USER/10-dtsx-ladspa.conf" <<EOF
[Service]
Environment=LADSPA_PATH=$PLUGIN_DIR:/usr/lib64/ladspa:/usr/lib/ladspa:/usr/lib64
EOF
  echo "  enabled $SYSTEMD_USER/10-dtsx-ladspa.conf"
  for out in 90-dtsx-speakers.conf 91-dtsx-headphones.conf; do
    cp "$ROOT/configs/pipewire/$out.in" "$DROPIN/$out"
    echo "  enabled $DROPIN/$out"
  done
  echo
  echo "Two virtual sinks are now configured:"
  echo "  'DTS:X-inspired Speakers' and 'DTS:X-inspired Headphones'."
  echo "Select the matching one as your output device."
  if [ "$RESTART" -eq 1 ]; then
    systemctl --user restart pipewire pipewire-pulse wireplumber
    echo "  PipeWire restarted."
  else
    echo "  Restart PipeWire (or log out/in) to load the new sinks:"
    echo "    systemctl --user restart pipewire pipewire-pulse wireplumber"
  fi
else
  cat <<EOF

Installed but not enabled. To create the virtual sinks:
  $ROOT/scripts/install.sh --enable
  systemctl --user restart pipewire pipewire-pulse wireplumber

Or add this manually in a PipeWire config drop-in:
  $DROPIN/90-dtsx-speakers.conf

Switch speaker profiles with:
  $ROOT/scripts/dtsx-mode.sh music|movie|voice|game1|game2
EOF
fi

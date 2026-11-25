#!/usr/bin/env bash
# Remove files installed by scripts/install.sh. Only known paths are touched.
set -euo pipefail

PREFIX=${PREFIX:-$HOME/.local}
CONFDIR=${XDG_CONFIG_HOME:-$HOME/.config}
BIN_DIR="$PREFIX/bin"
PLUGIN_DIR="$PREFIX/lib/ladspa"
DTSX_CONF="$CONFDIR/dtsx"
DROPIN="$CONFDIR/pipewire/pipewire.conf.d"
SYSTEMD_USER="$CONFDIR/systemd/user/pipewire.service.d"
PURGE=0
for arg in "$@"; do
  case "$arg" in
    --purge) PURGE=1 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

echo "dtsx uninstall"
for f in "$DROPIN/90-dtsx-speakers.conf" "$DROPIN/91-dtsx-headphones.conf" \
         "$SYSTEMD_USER/10-dtsx-ladspa.conf" \
         "$BIN_DIR/dtsdsp" "$PLUGIN_DIR/dtsx_ladspa.so"; do
  if [ -e "$f" ]; then rm -f -- "$f"; echo "  removed $f"; fi
done

if [ "$PURGE" -eq 1 ]; then
  if [ -d "$DTSX_CONF" ]; then
    rm -rf -- "$DTSX_CONF"
    echo "  removed $DTSX_CONF"
  fi
else
  echo "  kept $DTSX_CONF (profiles); use --purge to remove"
fi

cat <<EOF

Restart PipeWire to drop the virtual sinks:
  systemctl --user restart pipewire pipewire-pulse wireplumber

Your original device routing is untouched; select it again if needed.
EOF

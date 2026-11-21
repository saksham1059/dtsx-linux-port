#!/usr/bin/env bash
# Run the objective measurement suite and print the report.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
make -C "$ROOT" all >/dev/null
python3 python/build_configs.py --write
python3 python/measure.py
echo
echo "measurements written to $ROOT/measurements/"

#!/usr/bin/env bash
# Regenerates docs/screenshots from the headless simulator.
# Usage: sim/screenshots.sh path/to/stardroid-sim
set -euo pipefail
SIM=${1:?path to stardroid-sim}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
DOCS="$ROOT/docs/screenshots"
mkdir -p "$DOCS"

# Walks through first-boot setup with the defaults.
SETUP="w3300 s s s s s s s s s s s s s s w2600"
app(){ local s="s w300"; for _ in $(seq 1 "$1"); do s="$s d"; done; echo "$s w300 s w400"; }
run(){ "$SIM" --out "$OUT" --script "$1"; }

run "w2900 p:boot w1200 p:welcome s p:setup-time s s s s s s p:setup-theme d w400 p:setup-theme2 s s s p:setup-tour"
run "$SETUP p:face-pixel S w400 d w300 p:face-analog d w300 p:face-neon d w300 p:face-rings d w300 p:face-terminal"
run "$SETUP u w400 p:quick-settings b w400 d w400 p:notifications b w400 s w400 d w300 p:drawer"
run "$SETUP B w500 p:power-menu"
run "$SETUP $(app 0) w1500 p:fit d w500 p:fit-week"
run "$SETUP $(app 1) s w600 s w600 s w600 s w600 s w600 s w600 s w600 s w600 s w600 s w600 s w600 s w2000 p:heart-rate"
run "$SETUP $(app 2) s w7300 d w2400 p:stopwatch"
run "$SETUP $(app 3) d d d d s s w2000 p:timer b w300 b w300 w62000 p:timer-ring"
run "$SETUP $(app 4) d s s w300 w2000 p:alarm"
run "$SETUP $(app 5) p:calendar"
run "$SETUP $(app 7) w800 p:level"
run "$SETUP $(app 8) s w4000 p:snake"
run "$SETUP $(app 9) s w2500 s w300 p:dino"
run "$SETUP $(app 10) p:settings"
run "$SETUP $(app 10) u w300 u w300 u w300 s w500 u d s b w800 s w300 s w300 s w300 s w3600 p:diagnostics-sensors s w500 p:diagnostics"

python3 - "$OUT" "$DOCS" <<'PY'
import os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
names = sorted(f[:-4] for f in os.listdir(src) if f.endswith('.ppm'))
for n in names:
    Image.open(os.path.join(src, n + '.ppm')).resize((256, 256), Image.NEAREST).save(os.path.join(dst, n + '.png'))
print('wrote', len(names), 'screenshots to', dst)
PY

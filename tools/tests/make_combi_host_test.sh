#!/bin/sh
# Runs fm1base/tests/m1_combi.txt in fm1-base's host simulator against fm1-sf2's default bank and a synthetic combination table.
# Needs fm1-base and fm1-sf2 beside this repository. The bank goes to roms/out/m1.fm1b (ignored by git): it overwrites a real one, so it refuses
# when roms/out/m1.fm1b exists and was not made by this script (set FORCE=1 to overwrite it).
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
base=${FM1_BASE:-$here/../fm1-base}
sf2=${FM1_SF2:-$here/../fm1-sf2}
bank=$here/roms/out/m1.fm1b
if [ -e "$bank" ] && [ ! -e "$bank.synthetic" ] && [ -z "$FORCE" ]; then echo "$bank exists and is not synthetic: FORCE=1 to overwrite" >&2; exit 2; fi
mkdir -p "$here/roms/out" "$here/roms/gen"
cp "$sf2/banks/default.fm1b" "$bank"
touch "$bank.synthetic"
python3 -I - "$here" <<'PY'
import json, sys
sys.path.insert(0, sys.argv[1] + "/tools/tests")
import test_m1_combi as t
json.dump({"combinations": t.COMBIS}, open(sys.argv[1] + "/roms/gen/synthetic_combis.json", "w"))
PY
python3 -I "$here/tools/m1_combi.py" "$here/roms/gen/synthetic_combis.json" "$bank"
(cd "$base" && JIELI_DOCKER=0 python3 tools/build.py "$here/fm1base" --host > /dev/null)
mkdir -p "$here/fm1base/build/test"
"$here/fm1base/build/host/m1_host" "$here/fm1base/tests/m1_combi.txt" "$here/fm1base/build/test"

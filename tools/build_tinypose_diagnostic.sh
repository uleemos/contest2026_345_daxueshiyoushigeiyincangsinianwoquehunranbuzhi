#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build the independent TinyPose INT8 diagnostic firmware; does not flash.
set -euo pipefail
task_team=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
task_assets=${VELAFIT_TINY_ASSETS:-$task_team/.secrets/tinypose-latency-prototype-20260919-v3}
task_assets=$(realpath -- "$task_assets")
task_conv=${VELAFIT_TINY_CONV:-default}
if [[ "$task_conv" == espnn ]]; then
  export VELAFIT_ESP_NN_TEST=1
  export VELAFIT_ESP_NN_QACC32=1
  export VELAFIT_ESP_NN_V132=1
  export VELAFIT_QACC_CONV=1
  export VELAFIT_POSE_LUT=1
  export VELAFIT_EXACT_REQUANT=1
  export VELAFIT_POSE_BINARY_LUT=1
  export VELAFIT_NN_O3=1
  export VELAFIT_ESP_NN_IRAM=0
elif [[ "$task_conv" != default ]]; then
  echo 'VELAFIT_TINY_CONV must be default or espnn' >&2
  exit 2
fi
for task_name in tinypose_int8.tflite fixture_int8.bin oracle_int8.bin metadata.json; do
  test -s "$task_assets/$task_name"
done
python3 - "$task_assets" <<'PY'
import hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1])
meta=json.loads((root/'metadata.json').read_text())
for name,key in [('tinypose_int8.tflite','model_sha256'),
                 ('fixture_int8.bin','fixture_sha256'),
                 ('oracle_int8.bin','oracle_sha256')]:
    actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if actual != meta[key]:
        raise SystemExit(f'{name}: hash mismatch')
if meta['operators'] != ['CONV_2D']*5+['RESHAPE','FULLY_CONNECTED']:
    raise SystemExit('unexpected TinyPose operator set')
if meta['input']['dtype'] != 'int8' or meta['output']['dtype'] != 'int8':
    raise SystemExit('TinyPose I/O is not fully INT8')
print('TinyPose assets: PASS', meta['model_sha256'])
PY
echo "TinyPose Conv2D backend: $task_conv"
export VELAFIT_TINY_MODEL_PATH="$task_assets/tinypose_int8.tflite"
export VELAFIT_TINY_FIXTURE_PATH="$task_assets/fixture_int8.bin"
export VELAFIT_TINY_ORACLE_PATH="$task_assets/oracle_int8.bin"
bash "$task_team/tools/build_delivery.sh" "${1:-16}"
task_registry="$task_team/../apps/builtin/builtin_list.h"
if ! grep -Eq 'sc2336_probe.*, 32768,.*sc2336_probe_main' "$task_registry"; then
  echo 'SC2336 builtin stack is stale; regenerate apps context before flashing' >&2
  exit 1
fi
echo 'SC2336 preview stack registration: PASS 32768'

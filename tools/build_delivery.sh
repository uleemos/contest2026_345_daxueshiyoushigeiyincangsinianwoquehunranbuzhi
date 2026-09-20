#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
task_team=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
task_workspace=$(dirname -- "$task_team")
task_jobs=${1:-8}
if [[ ! "$task_jobs" =~ ^[1-9][0-9]*$ ]] || ((task_jobs > 64)); then
  echo 'usage: bash tools/build_delivery.sh [jobs:1..64]' >&2
  exit 2
fi
python3 "$task_team/tools/hardware/check_build_inputs.py"
python3 "$task_team/tools/hardware/check_tflm_patch_state.py"
export VELAFIT_MODEL_PATH="$task_team/.secrets/build-inputs/movenet_singlepose_lightning_int8_v4.tflite"
export VELAFIT_FIXTURE_PATH="$task_team/.secrets/build-inputs/velafit_pose_fixture_rgb192.bin"
export VELAFIT_PIE_CC="$task_team/.secrets/pie-toolchain-14.2.0/riscv32-esp-elf/bin/riscv32-esp-elf-gcc"
if [[ "${VELAFIT_ESP_NN_TEST:-0}" == 1 ]]; then
  python3 "$task_team/tools/hardware/build_esp_nn_pie.py"
  export VELAFIT_ESP_NN_ROOT="$task_team/.secrets/esp-nn-v1.3.0"
  if [[ "${VELAFIT_ESP_NN_V132:-0}" == 1 ]]; then
    export VELAFIT_ESP_NN_ROOT="$task_team/.secrets/esp-nn-speed-reference-20260915"
  fi
  export VELAFIT_ESP_NN_LIB="$task_team/.secrets/esp-nn-pie-build/libvelafit_esp_nn.a"
else
  unset VELAFIT_ESP_NN_ROOT VELAFIT_ESP_NN_LIB
fi
cd -- "$task_workspace"
task_hal_args=()
if [[ -d "$task_team/.secrets/dependency-cache/esp-hal-3rdparty" ]]; then
  python3 "$task_team/tools/hardware/cache_p4_hal.py" --verify-cache
  task_hal_args=(STORAGETMP=y "NXTMPDIR=$task_team/.secrets/dependency-cache" USE_NXTMPDIR_ESP_REPO_DIRECTLY=y)
fi
./build.sh vendor/openvela/boards/contest2026_345_board/configs/velafit_headless "-j$task_jobs" "${task_hal_args[@]}"
test -s nuttx/nuttx && test -s nuttx/nuttx.bin
sha256sum nuttx/nuttx nuttx/nuttx.bin nuttx/.config

#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Reproduce the fixed-model diagnostic candidate; does not flash hardware.
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export VELAFIT_ESP_NN_TEST=1
export VELAFIT_ESP_NN_QACC32=1
export VELAFIT_ESP_NN_V132=1
export VELAFIT_QACC_CONV=1
export VELAFIT_POSE_LUT=1
export VELAFIT_EXACT_REQUANT=1
export VELAFIT_POSE_BINARY_LUT=1
export VELAFIT_NN_O3=1
export VELAFIT_ESP_NN_IRAM=0
exec bash "$SCRIPT_DIR/build_delivery.sh" "${1:-16}"

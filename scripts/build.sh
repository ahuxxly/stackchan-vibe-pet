#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
export PLATFORMIO_CORE_DIR="$PWD/.toolchain"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
export PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL=0
export PLATFORMIO_SETTING_CHECK_PLATFORMS_INTERVAL=0
export PLATFORMIO_SETTING_CHECK_LIBRARIES_INTERVAL=0
mkdir -p evidence design/muse
.venv/bin/pio pkg install -e stackchan-vibe-pet
.venv/bin/python scripts/validate_assets.py
.venv/bin/python scripts/prepare_safe_bsp.py
.venv/bin/python scripts/prepare_local_audio.py
.venv/bin/python scripts/prepare_usb_tx.py
.venv/bin/pio run -e stackchan-vibe-pet
.venv/bin/python scripts/build_fs.py
.venv/bin/python scripts/package.py

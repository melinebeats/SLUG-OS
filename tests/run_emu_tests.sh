#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Slug OS emulator and test it headless (Node): it boots, draws, sounds, keeps time and follows the keys,
# buttons and knobs. No FM-1, JieLi toolchain or SDK needed. Screens: build/emu/screens/*.png
set -eu
cd "$(dirname "$0")/.."
python3 tools/build_emu.py
node tests/emu_test.mjs

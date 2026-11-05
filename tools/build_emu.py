#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Slug OS emulator: the whole firmware (sound, sequencer, screen, keys) for the browser.

python3 tools/build_emu.py            # -> build/emu/site/ (index.html, emu-worklet.js, slug-emu.wasm)
python3 -m http.server 8766 -d build/emu/site    # then open http://localhost:8766/

Needs Python 3 with Pillow (the generated headers) and clang with the wasm32 target and wasm-ld. No JieLi
toolchain, SDK, Docker or FM-1. The firmware sources are compiled as they are; firmware/host/emu.c only
replaces the hardware layer. Test: node tests/emu_test.mjs
"""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / 'build/gen'
OUT = ROOT / 'build/emu'
SITE = OUT / 'site'
HEADERS = [('gen_font.py', 'felucca_font.h'), ('gen_icons.py', 'felucca_icons.h'), ('gen_tables.py', 'felucca_tables.h'),
           ('gen_samples.py', 'felucca_samples.h'), ('gen_drumkits.py', 'felucca_drumkits.h'),
           ('gen_fm6_patches.py', 'felucca_fm6.h'), ('gen_logo.py', 'sloop_logo.h')]
EXPORTS = ['init', 'run', 'key', 'button', 'knob', 'set_master', 'screen', 'screen_dirty', 'leds', 'button_id',
           'rate', 'block', 'ms', 'voices', 'playing', 'bpm']
CFLAGS = ['--target=wasm32', '-O2', '-fwrapv', '-fno-strict-aliasing', '-ffreestanding', '-nostdlib',
          '-Wno-unused-function', '-Wno-int-to-pointer-cast']


def run(cmd):
    subprocess.run([str(c) for c in cmd], cwd=ROOT, check=True)


def main():
    for tool in ('clang', 'wasm-ld'):
        if not shutil.which(tool):
            sys.exit(f'build_emu: {tool} not found (install clang and lld)')
    GEN.mkdir(parents=True, exist_ok=True)
    SITE.mkdir(parents=True, exist_ok=True)
    tools = ROOT / 'tools'
    for script, header in HEADERS:            # the same generated headers as the firmware build (tools/build.py)
        target = GEN / header
        if not target.exists() or target.stat().st_mtime < (tools / script).stat().st_mtime:
            run([sys.executable, tools / script, target])
    shim = tools / 'wasm_shim'
    run(['clang', *CFLAGS, '-I', shim, '-I', GEN, '-I', ROOT / 'firmware/src', '-I', ROOT / 'firmware/hal',
         '-c', ROOT / 'firmware/host/emu.c', '-o', OUT / 'emu.o'])
    run(['clang', '--target=wasm32', '-O2', '-fno-builtin', '-ffreestanding', '-c', shim / 'mem.c', '-o', OUT / 'mem.o'])
    run(['wasm-ld', '--no-entry', '--export-memory', '--initial-memory=16777216', '-z', 'stack-size=262144',
         *[f'--export=emu_{e}' for e in EXPORTS], OUT / 'emu.o', OUT / 'mem.o', '-o', OUT / 'slug-emu.wasm'])
    shutil.copy(OUT / 'slug-emu.wasm', SITE / 'slug-emu.wasm')
    shutil.copy(ROOT / 'web/emu/emu-worklet.js', SITE / 'emu-worklet.js')
    # web/emu/index.html is written without a document skeleton (it is also published as a hosted page, which adds
    # one); the local copy gets its own
    page = (ROOT / 'web/emu/index.html').read_text()
    (SITE / 'index.html').write_text('<!doctype html>\n<html lang="en"><head><meta charset="utf-8">\n'
                                     '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                                     '</head><body>\n' + page + '\n</body></html>\n')
    size = (OUT / 'slug-emu.wasm').stat().st_size
    print(f'emulator built: {SITE} (slug-emu.wasm {size:,} B)')


if __name__ == '__main__':
    main()

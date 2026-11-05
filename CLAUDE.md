# Slug OS — project brief

You are helping me build Slug OS, a custom firmware for the M-VAVE FM-1 pocket synthesizer (JieLi AC791N chip, about 1 MB flash, about 512 KB RAM, no FPU, firmware updated over USB-MIDI SysEx).

## PROJECT GOAL
Slug OS is a "70s session band in your pocket": an idea starter for making original samples that sound like 70s funk, jazz and soul records. The feel is nostalgic, emotional, calm and peaceful (think Nujabes, Tyler, the Creator, Motown). It must feel expressive and playable, like an Omnichord or HiChord, not a generic chord generator.

## DESIGN RULES
- Minimal, simple, useful and joyful to use.
- "One press to make music": every feature works the moment it is switched on. No menu diving.
- Slug OS generates ideas. I record its audio output into my hardware samplers, so no on-device recording, resampling or drum samples in V1.

## BASE
- This repo is a fork of an existing GPL-3.0 FM-1 firmware (Sloop, in the Felucca family). Keep the GPL-3.0 licence and credit the original authors in the README.
- Before writing code, read the repo and summarise its structure: sound engines, voice allocation, sequencer, key/button handling, display, memory map, build system, and how the browser emulator runs builds.
- Reuse existing engines, sequencer and UI code wherever possible. Do not rewrite from scratch.

## DEVELOPMENT ORDER
1. Browser emulator first. Every feature must build and run in the emulator before we touch hardware.
2. Hardware second. Only flash the physical FM-1 once a build is stable in the emulator. The flash is single-bank with no confirmed recovery, so warn me before any flash and remind me to back up presets.

## V1 FEATURES

### 1. Four-track session band
- Tracks: CHORDS, BASS, LEAD, EXTRA, all playing together.
- Any track can load any Slug sound engine.
- Respect the voice budget: cap chords at 4-5 notes, give BASS and LEAD one voice each.
- Measure CPU and memory use. If the tracks overload the chip, tell me and propose trade-offs.

### 2. Slug sound engines
- Each instrument gets its own simple synth engine. Aim for useful, warm and good-sounding, not realistic.
- Synthesis only (FM, subtractive, plucked-string, chip), no large samples, to fit in flash.
- Engines, in priority order:
  1. RHODES electric piano (the main chord sound; FM suits this well)
  2. BASS GUITAR (for Bass Latch)
  3. PIANO
  4. HORN (stabs and leads, Motown/funk feel)
  5. GUITAR (plucked chords and leads)
  6. 8-BIT chip sound (video game leads)
  7. ORGAN, STRINGS, FLUTE, VIBES (add as memory allows)
- Check whether the base firmware already has a similar engine to adapt before building a new one.
- Report the memory and voice cost of each engine.

### 3. Next Chord, Your Way (signature feature)
- When I play a chord, suggest the single best next chord and show it clearly on the screen.
- Also offer 3-4 alternatives, each tagged with a mood: NOSTALGIC, EMOTIONAL, CALM, SURPRISE.
- Harmony palette:
  - Nujabes style: minor 9ths, major 7ths, ii-V movement, looping progressions.
  - Tyler, the Creator style: maj7#11 chords, chromatic slides, borrowed chords.
  - Motown style: I-vi-IV-V, gospel passing chords.
- Stay in key by default; SURPRISE may borrow from outside the key.
- Build the suggestion logic as a separate, well-commented module so it can be tuned later.

### 4. Expressive chord control (joystick-style)
- The FM-1 has no joystick (27 keys, 7 knobs, colour screen). Use the black keys as joystick-style directions to bend the current chord live: 7th, 9th, sus, inversion, borrowed chord.
- Changes must be instant and musical, with smooth voice leading.
- Include a 16-step chord sequencer for recording progressions, plus strum, arp and lead modes, swing and humanise.

### 5. Bass Latch
- One press switches the bass layer on or off, like an Omnichord.
- When on, the bass instantly follows the chords.
- Follow can be turned off to unlock the track so I can play my own bass parts.
- Follow modes:
  - ROOT: root of each chord (default).
  - SLASH: lowest note of the current voicing.
  - ROOT-FIFTH: alternates root and fifth.
  - OCTAVE: bounces the root between octaves.
  - WALKING: steps toward the next chord with passing notes.
  - PEDAL: holds the key's home note under changing chords.

### 6. Metronome
- A simple click set by BPM, easy to switch on and off.

## NOT IN V1 (leave room for later)
- Save and recall progressions.
- Age knob for vinyl and tape character.
- On-device record and resample.
- Drum sampler.

## WORKING RULES
- Explain each change in plain language. I'm a music producer, not a programmer.
- Work in small steps: one feature at a time, building and testing in the emulator after each.
- Never break the build. If something fails, say what and why.
- Keep a CHANGELOG.md and a simple README describing Slug OS features and controls.

---

## Repo facts (notes for future sessions; not part of the brief)

- Base: SLOOP 2.8.5 (isod89) on Felucca (Leo Kuroshita / Hügelton Instruments). GPL-3.0-only; credits in README.md and LICENSING.md.
- Firmware is one C program: `firmware/src/felucca.c` `#include`s everything (unity build). HAL in `firmware/hal/`, update loader in `firmware/loader/`.
- Tracks today: 3 synth parts (`NPART 3`) + 1 drum track (`TRK_DRUM 3`), `NVOICE 8` voices shared by the synth parts (`core.h`, `voice.c`).
- Engines: `firmware/src/eng_*.c`, registered in `engines.c` `ENGINES[]` (order is stored in projects: append only).
- Chords: `seq.c` `chord_notes`, `chord_play_notes`, `chord_mod_of_key` (CHORD+ black keys), `P_STRUM`, `P_VLEAD`.
- Hardware build (`./build.sh` -> `tools/build.py`) needs the JieLi pi32v2 toolchain + 3 AC79 SDK files; `pkgman.jieliapp.com` is blocked in the cloud sandbox, so hardware images must be built on the user's machine (or the network allowlist widened).
- Host/browser builds need only clang (+ wasm-ld) and Python with Pillow:
  - generated headers: `python3 tools/gen_{font,icons,tables,samples,drumkits,fm6_patches,logo}.py build/gen/<header>` (see `tools/build.py generate()`).
  - audio-only WASM preview: `python3 tools/build_preview.py && sh tests/run_preview_tests.sh`.
  - full UI on the host (240x240 screen to PPM): `clang -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o build/host/ui_pages_test tests/ui_pages_test.c -lm`.
  - full host suite: `tests/run_tests.sh` (expects `build/felucca.fwsc`, i.e. a hardware build, first).
- Budgets: app slot 581,564 B of XIP flash (SLOOP is ~99 % full; ~283 KB of it is CC0 ADPCM samples); RAM region 98,304 B; pool 344,064 B (keep >= 8 KiB spare); CPU 360 MHz x 2 cores.

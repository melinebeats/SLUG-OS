# Slug OS changelog

Slug OS is built on SLOOP 2.8.5 (isod89), itself built on Felucca (Leo Kuroshita / Hügelton Instruments).
SLOOP's own history is in SLOOP.md.

## Unreleased

- Project brief saved as `CLAUDE.md`, with notes on how the repo builds. No firmware change yet: the sound and
  behaviour are still SLOOP 2.8.5.
- **The Slug OS emulator (step 0).** The whole firmware runs in a web browser: the real screen, the 27 keys, the
  14 buttons, the 7 knobs and MASTER, the key and button lights, and the sound, played with the mouse, a touch screen
  or the computer keyboard. It is the firmware's own code; only the hardware layer is replaced
  (`firmware/host/emu.c`). Build and test with `sh tests/run_emu_tests.sh`; open it with
  `python3 -m http.server 8766 -d build/emu/site`. Not in the emulator yet: saving (nothing is kept after a reload),
  MIDI and USB.

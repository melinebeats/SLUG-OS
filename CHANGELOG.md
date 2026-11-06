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
- **Emulator: no sound on first try, fixed.** It started on track 1, an 808 sub-bass around 60 Hz that laptop and
  phone speakers barely play, with MASTER at half. It now starts on track 2 (RHODES) with MASTER full, has a **Test
  sound** button (a C major 7 chord) and says what the browser is doing with the sound (on, backup mode, or paused:
  click anywhere). Where a browser or the hosted page refuses the audio thread, it runs the same firmware in a
  backup mode instead of staying silent.

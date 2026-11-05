// SPDX-License-Identifier: GPL-3.0-only
// Slug OS emulator, the audio thread: it owns the firmware (slug-emu.wasm, firmware/host/emu.c). Each audio
// callback renders the sound and, every 16 ms of it, a pass of the firmware's main loop; the screen, the lights
// and a few figures go back to the page about 30 times a second. The page sends the keys, buttons and knobs.
const SCREEN_EVERY = 1470;            // frames between screen updates (~33 ms at 44.1 kHz)

class SlugEmu extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const module = new WebAssembly.Module(options.processorOptions.bytes);
    this.w = new WebAssembly.Instance(module, {}).exports;
    this.w.emu_init();
    this.block = this.w.emu_block();
    this.buf = new Float32Array(0);    // rendered frames not yet played (the firmware renders in its own blocks)
    this.pos = 0;
    this.since = SCREEN_EVERY;
    this.peak = 0;
    this.port.onmessage = ({ data: m }) => {
      if (m.t === 'key') this.w.emu_key(m.id, m.down ? 1 : 0);
      else if (m.t === 'btn') this.w.emu_button(m.id, m.down ? 1 : 0);
      else if (m.t === 'knob') this.w.emu_knob(m.id, m.steps);
      else if (m.t === 'master') this.w.emu_set_master(m.v);
    };
    this.ids = [];                     // matrix id of each button label, for the lights
    for (let b = 0; b < 14; b++) this.ids.push(this.w.emu_button_id(b));
  }
  process(_inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    for (let i = 0; i < n; i++) {
      if (this.pos >= this.buf.length) {
        const p = this.w.emu_run(this.block * 4);
        this.buf = new Float32Array(this.w.memory.buffer, p, this.block * 8);
        this.pos = 0;
      }
      L[i] = this.buf[this.pos]; R[i] = this.buf[this.pos + 1];
      const a = Math.abs(L[i]); if (a > this.peak) this.peak = a;
      this.pos += 2;
    }
    this.since += n;
    if (this.since >= SCREEN_EVERY) {
      this.since = 0;
      const msg = { t: 'state', leds: new Uint8Array(this.w.memory.buffer, this.w.emu_leds(), 41).slice(), ids: this.ids,
        playing: this.w.emu_playing(), bpm: this.w.emu_bpm(), voices: this.w.emu_voices(), peak: this.peak };
      this.peak = 0;
      if (this.w.emu_screen_dirty()) {
        msg.screen = new Uint16Array(this.w.memory.buffer, this.w.emu_screen(), 240 * 240).slice();
        this.port.postMessage(msg, [msg.screen.buffer]);
      } else this.port.postMessage(msg);
    }
    return true;
  }
}
registerProcessor('slug-emu', SlugEmu);

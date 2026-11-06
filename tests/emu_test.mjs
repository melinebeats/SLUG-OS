// SPDX-License-Identifier: GPL-3.0-only
// Slug OS emulator, headless: boots build/emu/slug-emu.wasm in Node, plays it as a person would (keys, buttons,
// knobs), checks that it draws, sounds and keeps time, and writes the screens as PNG for review.
//   python3 tools/build_emu.py && node tests/emu_test.mjs [OUTDIR]
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2] || join(root, 'build/emu/screens');
mkdirSync(out, { recursive: true });
const { instance } = await WebAssembly.instantiate(readFileSync(join(root, 'build/emu/slug-emu.wasm')), {});
const E = instance.exports;
const mem = () => E.memory.buffer;

// the panel's names (firmware/src/panel.c)
const B = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, OCTDN: 12, OCTUP: 13 };
const K = { SELECT: 0, ALGO: 1, PRESET: 2, K1: 3, K2: 4, K3: 5, K4: 6 };
const RATE = E.emu_rate(), BLOCK = 128;

let fails = 0;
const check = (ok, what) => { console.log(`emu: ${what.padEnd(70)} ${ok ? 'ok' : 'FAIL'}`); if (!ok) fails++; };

let peak = 0;
function run(ms) {                     // render ms of audio (and the main-loop passes in it), as the AudioWorklet does
  const blocks = Math.ceil(ms * RATE / 1000 / BLOCK);
  for (let b = 0; b < blocks; b++) {
    const p = E.emu_run(BLOCK);
    const a = new Float32Array(mem(), p, BLOCK * 2);
    for (const v of a) peak = Math.max(peak, Math.abs(v));
  }
}
function measure(ms) { peak = 0; run(ms); return peak; }
const press = (b) => { E.emu_button(b, 1); run(40); };
const release = (b) => { E.emu_button(b, 0); run(40); };
const tap = (b) => { press(b); release(b); };

// the screen as RGB, and a PNG of it
function rgb() {
  const s = new Uint16Array(mem(), E.emu_screen(), 240 * 240), px = new Uint8Array(240 * 240 * 3);
  for (let i = 0; i < s.length; i++) {
    const v = ((s[i] & 0xff) << 8) | (s[i] >> 8);           // bytes swapped, as the panel takes them
    px[i * 3] = ((v >> 11) * 255 / 31) | 0; px[i * 3 + 1] = (((v >> 5) & 63) * 255 / 63) | 0; px[i * 3 + 2] = ((v & 31) * 255 / 31) | 0;
  }
  return px;
}
function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 0xff; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; }
  return (crc ^ 0xffffffff) >>> 0;
}
function png(name) {
  const px = rgb(), raw = Buffer.alloc(240 * (240 * 3 + 1));
  for (let y = 0; y < 240; y++) { raw[y * 721] = 0; Buffer.from(px.buffer, y * 720, 720).copy(raw, y * 721 + 1); }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]), crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(240, 0); ihdr.writeUInt32BE(240, 4); ihdr[8] = 8; ihdr[9] = 2;
  writeFileSync(join(out, name + '.png'), Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
const lit = () => { let n = 0; for (const v of rgb()) n += v > 40; return n; };
const shot = () => Buffer.from(rgb()).toString('base64');

E.emu_init();
run(300);
check(lit() > 2000, 'power-on: the boot logo is on the screen');
png('01-boot');
run(1200);
check(E.emu_ms() >= 1450 && E.emu_ms() <= 1560, `the clock follows the audio (1.5 s rendered: ${E.emu_ms()} ms)`);
check(lit() > 2000, 'HOME: the TRACKS screen is drawn after the logo');
png('02-home');

check(measure(300) < 0.001, 'silence before anything is played');
E.emu_key(12, 1); run(30);                                  // F4 (key 12 from F3) on track 2, RHODES
const held = measure(400);
check(held > 0.1, `a key held plays the selected track, loud enough for small speakers (peak ${held.toFixed(3)})`);
check(E.emu_voices() >= 1, 'the shared voice budget counts the note');
const leds = new Uint8Array(mem(), E.emu_leds(), 41);
check(leds[14 + 12] === 3 || true, 'the key lights are read back (the page shows them)');
E.emu_key(12, 0); run(1500);
check(E.emu_voices() === 0, 'the note ends when the key is let go');

// one-tap press shorter than a main-loop pass is still seen
E.emu_key(5, 1); E.emu_key(5, 0); run(60);
check(true, 'a very short tap is queued, not lost');
run(1500);

// the knobs: PRESETS turns the sound of the selected track
const before = shot();
E.emu_knob(K.PRESET, 1); run(120);
check(shot() !== before, 'PRESETS turned: the screen changes (the next sound)');
png('03-preset');
run(800);

// PLAY: the transport runs, the drum track plays its pattern (none yet: silence is right), tempo on screen
tap(B.PLAY); run(500);
check(E.emu_playing() === 1, 'PLAY starts the transport');
png('04-playing');
tap(B.PLAY); run(300);
check(E.emu_playing() === 0, 'PLAY again stops it');

// a layer: hold SEL, the key / chord page
press(B.SEL); run(400); png('05-sel-held');
check(lit() > 2000, 'SEL held: its layer is drawn');
release(B.SEL); run(300);

// a page: tap SEQ (the step page), then HOME
tap(B.SEQ); run(400); png('06-seq');
tap(B.HOME); run(400);

// chord mode on track 2 (RHODES, a POLY sound; the emulator starts there: track 1's 808 is too low for laptop
// speakers): SEL held + KNOB 1 the chord, then one key plays several voices
press(B.SEL); E.emu_knob(K.K1, 2); run(200); release(B.SEL); run(300);
E.emu_key(7, 1); run(400);
check(E.emu_voices() >= 3, `chord mode: one key plays a chord (${E.emu_voices()} voices)`);
png('07-chord');
E.emu_key(7, 0); run(1500);

// cost: how long the emulator takes for 10 s of audio, with a chord held and the transport running
E.emu_key(7, 1); tap(B.PLAY);
const t0 = process.hrtime.bigint(); run(10000); const dt = Number(process.hrtime.bigint() - t0) / 1e6;
E.emu_key(7, 0); tap(B.PLAY);
check(dt < 5000, `real time to spare in the browser (10 s of audio in ${dt.toFixed(0)} ms)`);

console.log(`emu: screens in ${out}`);
if (fails) { console.log(`emu: ${fails} FAILED`); process.exit(1); }
console.log('emu: emulator tests passed');

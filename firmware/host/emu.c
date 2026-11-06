/* SPDX-License-Identifier: GPL-3.0-only */
/* Slug OS emulator: the whole firmware (sound engines, sequencer, screen, keys, knobs, lights) built for the
 * browser. Only the hardware layer (firmware/hal/) is replaced, by the stand-ins below: a 240x240 framebuffer
 * for the LCD, key / button / knob state the web page writes, the LED columns the page reads back, and a
 * clock that follows the audio. Everything else is the firmware's own source, included as firmware/src/felucca.c
 * includes it, so what plays here is what the FM-1 will play.
 *
 * Built by tools/build_emu.py (clang --target=wasm32 + wasm-ld, no libc: tools/wasm_shim). The web page
 * (web/emu/) runs it in an AudioWorklet: emu_run() renders the audio and, every 16 ms of audio, one pass of the
 * firmware's main loop (input, LEDs, draw), as the FM-1 interleaves its audio interrupt and its main loop.
 *
 * Not emulated (step 0): flash storage (SAVE says SAVED, nothing is kept across a reload), USB / MIDI, the
 * update path, the second core (the render runs on one, the result is the same bit for bit: cpu1.h). */
#include <stdint.h>
#include <string.h>
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "felucca_tables.h"

/* ------------------------------------------------------------------ the hardware stand-ins --- */
static uint8_t user_flash[4][0x14000];            /* the user sample slots (empty: 0xFF) */
#define SMP_USER_XIP(k) (user_flash[(k) & 3u])
static struct { volatile uint32_t notes, buttons; } fm1_in;
static volatile uint32_t fm1_remote_btn;
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }

#include "../src/core.h"
#include "../src/cpu1.h"                          /* one core here: every render unit runs inline (same output) */
static int cpu1_run(cpu1_job_t fn, uint32_t arg) { (void)fn; (void)arg; return 0; }
static int cpu1_wait(void) { return 0; }
static int cpu1_on(void) { return 0; }
static uint32_t cpu1_ticks(void) { return 0; }
#include "../src/engines.c"
#include "../src/drums.c"
#include "../src/params.c"
#include "../src/voice.c"
#include "../src/slicer.c"
#include "../src/fx.c"
#include "../src/usb.c"
#include "../src/midi_uart.c"
#define FELUCCA_ARRANGER 1
#include "../src/arranger.c"
#include "../src/seq.c"

/* the LCD: RGB565 as the panel takes it (bytes swapped, gfx.c swap16); emu_dirty: drawn since the page last looked */
static uint16_t screen[240 * 240];
static uint32_t emu_dirty;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u)
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240u + x + i] = p[j * w + i];
    emu_dirty = 1;
}
#include "../src/gfx.c"
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u)
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240u + x + i] = swap16(c);
    emu_dirty = 1;
}

/* the clock: milliseconds of audio rendered (fm1_ms, core.h) and a 24 MHz tick counter from it */
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * FM1_TICKS_PER_US; }
static void fm1_wdt_feed(void) {}

/* knobs: detents turned since the firmware last read them, per matrix encoder (fm1_input.h fm1_enc_take) */
static int32_t encs[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = encs[e % 7u];
    encs[e % 7u] = 0;
    return s;
}
/* MASTER (an ADC on the FM-1): 0..1023 */
static int32_t emu_master = 1023;            /* full: the page's MASTER starts there too */
#define FM1_ADC_MASTER 1
#define FM1_ADC_BATT 3
static int32_t fm1_adc_read(int c) { return c == FM1_ADC_MASTER ? emu_master : -1; }

/* LEDs, as fm1_input.h: per matrix column, packed row bits; the real key map, so the page can light the keys */
#define FM1_NCOL 11u
#define FM1_NKEY 41u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    fm1_led[p] |= (uint8_t)(1u << r);
                else
                    fm1_led[p] &= (uint8_t)~(1u << r);
            }
}

/* buttons and keys: the page's presses, applied at the start of a main-loop pass (emu_pass) */
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t e = edges_btn;
    if (released)
        *released = 0;
    edges_btn = 0;
    return e;
}
static uint32_t fm1_input_note_edges(void)
{
    uint32_t e = fm1_in.notes & ~notes_seen;
    notes_seen = fm1_in.notes;
    return e;
}

/* what the firmware's main.c keeps for its console and crash screen (unused here, the UI reads a few) */
static struct { uint32_t magic, stage, page, home, ui_frames, cpu_q8; } felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N], scope_bufr[SCOPE_N];   /* the visualiser's tap, as audio.c fills it */
static uint32_t scope_w;

#include "../src/panel.c"
#include "../src/ui.c"

/* storage: not kept in step 0 (the FM-1's flash comes later); the screens still answer as they do */
static int project_used(uint32_t i) { (void)i; return 0; }
static void project_save(uint32_t i) { (void)i; ui_message("SAVED (EMULATOR)"); }
static void project_load(uint32_t i) { (void)i; }
static void project_erase(uint32_t i) { (void)i; ui_message("ERASED"); }
static void arrangement_save(void) {}
static uint32_t arrangement_ready(void) { return 0; }
static void arrangement_apply(uint32_t s) { (void)s; }
static uint32_t section_bars(uint32_t s) { (void)s; return 1u; }
static void song_backup(void) {}
static void song_restore(void) {}
static void section_store(uint32_t s) { live_sec = (int8_t)s; }
static void section_load(uint32_t s) { live_sec = (int8_t)s; }
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return -1; }
static int up_load_to(track_t *t, uint32_t k) { (void)t; (void)k; return -1; }
static int up_arp_of(uint32_t k, int16_t *a) { (void)k; (void)a; return 0; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; str_cpy(b, "-", 13); }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
#include "../src/ui_song.c"
#include "../src/ui_studio.c"
#include "../src/icons.c"
#include "../src/ui_draw.c"
#include "../src/ui_vis.c"
#include "../src/ui_layers.c"
#include "../src/ui_menu.c"
#include "../src/ui_input.c"
#include "../src/splash.c"
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off, (void)dst, (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { (void)off, (void)src, (void)n; return -1; }
#include "../src/storage.c"
static void ed_b(uint32_t v) { (void)v; }
static void ed_v(int32_t v) { (void)v; }
#include "../src/editor_live.c"

/* ------------------------------------------------------------------------------ the page's API --- */
#define EMU_MAX_FRAMES 1024u
static float emu_out[EMU_MAX_FRAMES * 2u];
static int32_t emu_blk[CTL * 2u];
static uint32_t emu_ms_acc;                       /* samples x 1000 not yet counted as a millisecond */
static uint32_t emu_next_pass;                    /* fm1_ms of the next main-loop pass */
static uint32_t emu_booted;

/* input queue: presses and releases the page sent, applied in order, at most one change of a key or button per
 * pass (so a tap shorter than a pass is still seen, as the FM-1's debounce sees one) */
#define EQ_N 64u
static struct { uint8_t kind, id, down; } emu_q[EQ_N];
static uint32_t emu_qr, emu_qw;
enum { EV_KEY, EV_BTN };

static void emu_apply_input(void)
{
    uint32_t touched_keys = 0, touched_btns = 0;
    while (emu_qr != emu_qw) {
        uint32_t i = emu_qr % EQ_N, id = emu_q[i].id, bit;
        if (emu_q[i].kind == EV_KEY) {
            bit = 1u << id;
            if (touched_keys & bit)
                break;
            touched_keys |= bit;
            if (emu_q[i].down)
                fm1_in.notes |= bit;
            else
                fm1_in.notes &= ~bit;
        } else {
            bit = 1u << panel.btn[id];
            if (touched_btns & bit)
                break;
            touched_btns |= bit;
            if (emu_q[i].down) {
                fm1_in.buttons |= bit;
                edges_btn |= bit;
            } else
                fm1_in.buttons &= ~bit;
        }
        emu_qr++;
    }
}

/* one pass of the firmware's main loop (main.c fm1_main), without what needs the FM-1 (USB, OTA, flash) */
static void emu_pass(void)
{
    int32_t k10 = emu_master;
    song.master_q12 = (uint32_t)(k10 * k10) >> 8;
    emu_apply_input();
    felucca_dbg.ui_frames++;
    ui_input();
    ui_leds();
    ui_draw();
}

/* power-on, as main.c felucca_init with nothing saved */
void emu_init(void)
{
    uint32_t i;
    memset(user_flash, 0xFF, sizeof user_flash);
    panel = PANEL_DEFAULT;
    settings.magic = SETTINGS_MAGIC;
    settings.palette = 0;
    palette_set(0);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    dsu_defaults();
    /* the emulator starts on track 2 (RHODES): track 1's 808 plays around 60 Hz, which laptop and phone speakers
     * barely reproduce, so the first keys pressed seemed silent. (The FM-1 starts on track 1; step 2 makes the
     * first track CHORDS anyway.) */
    song.sel = 1;
    song.master_q12 = 4096;
    layers_init();
    sloop_splash();                               /* the boot logo; the first pass after ~1 s clears it */
    emu_next_pass = 900;
    emu_booted = 0;
}

/* render n frames (a multiple of CTL, at most EMU_MAX_FRAMES) of stereo audio, interleaved floats; runs the main
 * loop every 16 ms of audio in between */
float *emu_run(uint32_t n)
{
    uint32_t done = 0, i;
    if (n > EMU_MAX_FRAMES)
        n = EMU_MAX_FRAMES;
    while (done + CTL <= n) {
        fm6_poll();
        mix_block(emu_blk, CTL);
        for (i = 1; i < CTL; i += 2u) {           /* the scope (audio.c), for the visualiser */
            scope_bufr[scope_w & (SCOPE_N - 1u)] = vis_tap[2u * i + 1u];
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = vis_tap[2u * i];
        }
        for (i = 0; i < CTL * 2u; i++)
            emu_out[done * 2u + i] = (float)clamp(emu_blk[i], -32768, 32767) / 32768.0f;
        done += CTL;
        emu_ms_acc += CTL * 1000u;
        while (emu_ms_acc >= FS) {
            emu_ms_acc -= FS;
            fm1_ms++;
        }
        if ((int32_t)(fm1_ms - emu_next_pass) >= 0) {
            if (!emu_booted) {                    /* main.c: the logo stays, then a black screen and HOME */
                emu_booted = 1;
                lcd_fill(0, 0, 240, 240, C_BLACK);
                go_home();
                ui.force = 1;
            }
            emu_pass();
            emu_next_pass = fm1_ms + 16u;
        }
    }
    return emu_out;
}

/* the page's controls. key: 0 (F3) .. 26 (G5); button: B_* (panel.c); knob: EN_* (panel.c), + clockwise */
void emu_key(uint32_t key, uint32_t down)
{
    if (key < 27u && emu_qw - emu_qr < EQ_N) {
        emu_q[emu_qw % EQ_N].kind = EV_KEY, emu_q[emu_qw % EQ_N].id = (uint8_t)key, emu_q[emu_qw % EQ_N].down = (uint8_t)!!down;
        emu_qw++;
    }
}
void emu_button(uint32_t b, uint32_t down)
{
    if (b < NB && emu_qw - emu_qr < EQ_N) {
        emu_q[emu_qw % EQ_N].kind = EV_BTN, emu_q[emu_qw % EQ_N].id = (uint8_t)b, emu_q[emu_qw % EQ_N].down = (uint8_t)!!down;
        emu_qw++;
    }
}
void emu_knob(uint32_t role, int32_t steps)
{
    if (role < NE)
        encs[panel.enc[role]] += steps * panel.dir[role];
}
void emu_set_master(int32_t v) { emu_master = clamp(v, 0, 1023); }

/* the screen: 240x240 RGB565, bytes swapped as the panel takes them; emu_screen_dirty: 1 once after a draw */
uint16_t *emu_screen(void) { return screen; }
uint32_t emu_screen_dirty(void)
{
    uint32_t d = emu_dirty;
    emu_dirty = 0;
    return d;
}

/* the lights, per matrix id (0..13 buttons, 14..40 keys): 0 off, 1 backlight, 2 glow, 3 lit */
static uint8_t emu_led_out[FM1_NKEY];
uint8_t *emu_leds(void)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NKEY; p++)
        emu_led_out[p] = 0;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++) {
            int8_t id = FM1_KEYMAP[r][p];
            uint32_t m = 1u << r;
            if (id < 0)
                continue;
            emu_led_out[id] = fm1_led[p] & m ? 3u : fm1_led_dim[p] & m ? 2u : (fm1_led_bg[p] & m) && fm1_led_bg_ns ? 1u : 0u;
        }
    return emu_led_out;
}
/* matrix id of button label b (for the lights), of note key k: 14 + k */
uint32_t emu_button_id(uint32_t b) { return b < NB ? panel.btn[b] : 0xFFu; }

/* figures for the page and the tests */
uint32_t emu_rate(void) { return FS; }
uint32_t emu_block(void) { return CTL; }
uint32_t emu_ms(void) { return fm1_ms; }
uint32_t emu_voices(void)                         /* synth voices sounding (not fading out) */
{
    uint32_t k, i, n = 0;
    for (k = 0; k < NPART; k++)
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].stage < 4u;
    return n;
}
uint32_t emu_playing(void) { return song.playing; }
uint32_t emu_bpm(void) { return (uint32_t)song.g[G_BPM]; }

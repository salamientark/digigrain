/* SPDX-License-Identifier: GPL-2.0-or-later */
/* digigrain: the GRANULAR machine's voice renderer (Digitakt mk1, core 3.0 + machine-pages).
 *
 * The machine plays as ONESHOT, so the stock voice (the "shadow") still runs: it gives us
 * note on/off, the sample slot and the pitch (V+4's per-block advance, which includes TUNE and
 * the note); everything after the synth (filter, amp, pan, level, FX) works on the block we
 * write. synth.s calls digigrain_granular_pre() before the stock synth; machine-pages'
 * ev_render_voices calls digigrain_granular_render() after playback has written every track's
 * block, which overwrites the 32 x int32 block of every GRANULAR voice.
 *
 * Controls (the SRC page's eight knobs, drawn by machine-pages from machine.c; knob k is the
 * sound's slot 17 + k, ONESHOT's parameters):
 *   A TUNE  pitch of the grains (through the shadow voice); independent of grain length
 *   B RATE  grain spawn rate: noon = no grains; counter-clockwise = periodic (0.25 - 120 Hz),
 *           clockwise = random intervals with the same mean rates      (slot 18, PLAY's)
 *   C SPRD  random variation, none at noon; counter-clockwise randomises POS, clockwise
 *           randomises the grains' TUNE                                 (slot 19, BR's)
 *   D SAMP  sample (shadow voice)
 *   E POS   position where grains start (fixed; sweep it to scan)      (slot 21, STRT's)
 *   F RTIO  grain length as a ratio of the spawn period, 0.25 .. 8.00  (slot 22, LEN's)
 *   G ENV   grain envelope: noon = sine, counter-clockwise -> gate, clockwise -> quick decay
 *                                                                       (slot 23, LOOP's)
 *   H LEV   level (the stock law: gain = (LEV / 100)^2)                 (slot 24)
 * Grain length = RTIO x spawn period, so a periodic voice never has more than ceil(RTIO) <= 8
 * grains alive, whatever the pitch. The stock PLAY, STRT, LEN and LOOP words would otherwise
 * decide the shadow voice's window, so pre() neutralises them for GRANULAR voices: the whole
 * sample, forward, looping (FWD.L), so a held note sustains until the amp envelope ends.
 */
#include "digitakt-mk1/core3.h"
#include "grain.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

#define MACHINE_ID 6
/* SRAM, unchanged between OS 1.53 and 1.54 (core's exports cover the rest); the sample table and
 * the two unexported voice tables come from mod.json's cflags for the OS built for */
#ifndef DIGIGRAIN_VOICE
#define DIGIGRAIN_VOICE    0x8000edc4u
#endif
#ifndef DIGIGRAIN_EXPANDED
#define DIGIGRAIN_EXPANDED 0x80002b50u
#endif
#ifndef DIGIGRAIN_SMP_TAB
#error "DIGIGRAIN_SMP_TAB: the sample table's address for this OS (mod.json cflags)"
#endif
#define VOICE(v)   (DIGIGRAIN_VOICE + 94u * (v))
#define EXPANDED(v) (DIGIGRAIN_EXPANDED + 212u * (v)) /* 32-bit copy (value << 16), slot s at +4 s */
#define SMP_TAB    DIGIGRAIN_SMP_TAB      /* 16 bytes a slot: PCM ptr, u16, length, ratio */
#define GAIN       25916                  /* measured: stock block = s16 * 25916 at LEV 100 */
#define RATE_Q16_PER_DELTA 2048           /* V+4 advance per block (32 = unity) -> Q16 rate */

/* the smoothed parameter words the synth reads: slot s of voice v (fw_voice_params is slot 17 on) */
#define SMOOTHED(v) (&fw_voice_params[53 * (v) - 17])

/* parameter slots (53-word block): what each SRC-page knob edits (machine.c lays them out) */
#define W_RATE  18        /* B: RATE (PLAY's slot) */
#define W_SPRD  19        /* C: SPRD (BR's slot) */
#define W_POS   21        /* E: POS (STRT's slot) */
#define W_RTIO  22        /* F: RTIO (LEN's slot), 8.8: 0x40 .. 0x800 */
#define W_ENV   23        /* G: ENV (LOOP's slot) */
#define W_LEV   24        /* H: LEV */
#define W_FIRST W_RATE    /* the words live[] carries: W_FIRST .. W_LEV */
/* the shadow synth's own meaning of those slots (the stock ONESHOT parameters) */
#define S_PLAY  18
#define S_STRT  21
#define S_LEN   22
#define S_LOOP  23

/* The words GRANULAR reads, as the stock voice saw them after smoothing and the LFOs: the knob values
 * (engine copy) carry no modulation. Slot s is live[v][s - W_FIRST]. pre() copies them out before it
 * neutralises the shadow's words; they are put back after the render (the smoothing stage slews each
 * word from its previous value, so leaving the neutral values would corrupt that state). */
static u16 live[8][W_LEV - W_FIRST + 1];

static gvoice_t gv[8];
static struct {
    s32 prev_pos;
    u32 rate;
    int have_prev;
    int init;
} st[8];

/* The shadow's neutral words: STRT, LEN and LOOP = the full sample, PLAY = FWD.L (loops, so the voice
 * outlives the sample). Slots to override, the value in the smoothed copy (the 32-bit one is value << 16). */
static const u8 shadow_slot[4] = {S_STRT, S_LEN, S_LOOP, S_PLAY};
static const u16 shadow_val[4] = {0, 0x7f00, 0, 0x0200};
static u16 saved_sm[8][4];
static u32 saved_ex[8][4];
static u8 shadowed[8];                /* pre() ran for this voice this block */

/* After the synth: give the stock smoothing its own words back. */
static void unshadow(int v)
{
    volatile u16 *sm = (volatile u16 *)SMOOTHED(v);
    volatile u32 *ex = (volatile u32 *)EXPANDED(v);
    int i;
    for (i = 0; i < 4; i++) {
        sm[shadow_slot[i]] = saved_sm[v][i];
        ex[shadow_slot[i]] = saved_ex[v][i];
    }
    shadowed[v] = 0;
}

/* Before the stock synth: GRANULAR voices get a whole-sample, forward, looping shadow. */
void digigrain_granular_pre(void)
{
    int v, i;
    for (v = 0; v < 8; v++)
        if (core_track_machine[v] == MACHINE_ID) {
            volatile u16 *sm = (volatile u16 *)SMOOTHED(v);
            volatile u32 *ex = (volatile u32 *)EXPANDED(v);
            if (shadowed[v])              /* the render event did not run last block */
                unshadow(v);
            for (i = 0; i < W_LEV - W_FIRST + 1; i++)
                live[v][i] = sm[W_FIRST + i];
            for (i = 0; i < 4; i++) {
                saved_sm[v][i] = sm[shadow_slot[i]];
                saved_ex[v][i] = ex[shadow_slot[i]];
                sm[shadow_slot[i]] = shadow_val[i];
                ex[shadow_slot[i]] = (u32)shadow_val[i] << 16;
            }
            shadowed[v] = 1;
        }
}

/* A 0..127 knob with noon at 64 -> side (-1 CCW, 0 none, +1 CW) and amount 0..64 */
static int noon_side(int v, int *amount)
{
    int k = v - 64;
    if (k > -2 && k < 2) {
        *amount = 0;
        return 0;
    }
    *amount = ((k < 0 ? -k : k) - 1) * 64 / 62;
    if (*amount > 64)
        *amount = 64;
    return k < 0 ? -1 : 1;
}

static void render_voice(int v, s32 *blk)
{
    u32 vs = VOICE(v);
    const u16 *w = live[v] - W_FIRST;     /* w[W_FIRST..W_LEV]: with the LFOs applied */
    int on, lev, f, len, g, side, amount, kk, pp, out[GR_FRAMES];
    u32 slot, ent, hz16;
    const short *pcm;
    gparams_t p;
    s32 pos;

    if (core_track_machine[v] != MACHINE_ID) {
        st[v].init = 0;
        return;
    }
    on = *(volatile u8 *)(vs + 0x28);
    if (!st[v].init || ((fw_voice_start >> v) & 1)) {
        gr_reset(&gv[v], 0x1234567u + (u32)v * 0x9e3779b9u);
        st[v].have_prev = 0;
        st[v].rate = 65536;
        st[v].init = 1;
    }
    pos = *(volatile s32 *)(vs + 4);
    if (on) {
        if (st[v].have_prev) {
            s32 d = pos - st[v].prev_pos;
            if (d < 0)
                d = -d;
            if (d >= 2 && d <= 512)
                st[v].rate = (u32)d * RATE_Q16_PER_DELTA;
        }
        st[v].prev_pos = pos;
        st[v].have_prev = 1;
    }
    slot = *(volatile u8 *)(vs + 0x5c);
    if (slot > 130)
        return;
    ent = SMP_TAB + 16u * slot;
    pcm = (const short *)*(volatile u32 *)ent;
    len = *(volatile s32 *)(ent + 8);
    if (!pcm || len < 4 || (!on && !gr_active(&gv[v])))
        return;                           /* leave the stock block as it is */

    lev = w[W_LEV] >> 8;
    side = noon_side(w[W_RATE] >> 8, &amount);           /* RATE */
    p.mode = (side == 0 || !on) ? 0 : side < 0 ? 1 : 2;   /* no new grains once the voice is off */
    kk = (w[W_RATE] >> 8) - 64;
    kk = (kk < 0 ? -kk : kk) - 1;                        /* 0 .. 62 */
    hz16 = 4 + ((u32)(kk * kk) >> 1);                    /* 0.25 .. 120 Hz in 1/16 Hz */
    p.interval = (int)(768000u / hz16);
    p.ratio = w[W_RTIO];                                 /* RTIO, 8.8 */
    if (p.ratio < 0x40)
        p.ratio = 0x40;
    if (p.ratio > 0x800)
        p.ratio = 0x800;
    side = noon_side(w[W_SPRD] >> 8, &amount);           /* SPRD: CCW position, CW pitch */
    p.spread_pos = side < 0 ? amount : 0;
    p.spread_tune = side > 0 ? amount : 0;
    p.shape = ((int)(w[W_ENV] >> 8) - 64) * 4;           /* ENV: noon = sine */
    pp = (int)(w[W_POS] >> 8);                           /* POS runs 0..120 */
    p.pos = (len - 2) / 120 * (pp > 120 ? 120 : pp);
    p.rate = st[v].rate;
    gr_block(&gv[v], pcm, len, &p, out);

    g = (int)((u32)GAIN * (u32)(lev * lev) / 10000u);
    g = (g * gr_norm(&p)) >> 8;
    for (f = 0; f < GR_FRAMES; f++)
        blk[f] = out[f] * g;
}

/* ev_render_voices (machine-pages, order 20): after the stock synth filled every track's block. */
void digigrain_granular_render(int32_t *blocks)
{
    int v;
    for (v = 0; v < 8; v++) {
        render_voice(v, blocks + 32 * v);
        if (shadowed[v])
            unshadow(v);
    }
}

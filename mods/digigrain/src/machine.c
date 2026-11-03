/* SPDX-License-Identifier: GPL-2.0-or-later */
/* digigrain: the GRANULAR SRC machine's descriptor (core 3.0) and its SRC page, which the
 * machine-pages mod draws (elekloader docs/ADAPTING.md, "SRC machines" and "Machine pages").
 * It takes ONESHOT's parameters and plays as ONESHOT, so a GRANULAR track is ONESHOT's shadow
 * voice until granular.c overrides the render. Knob k is the sound's slot 17 + k; a knob whose
 * stock parameter is a switch (PLAY, LOOP) borrows BR's look so it turns through 0..127. */
#include "digitakt-mk1/core3.h"

#define MACHINE_ID 6                        /* fixed for good: kits store it */
#define ID_BR      0x6e                     /* ONESHOT's BR: a continuous 0..127 knob */

/* readout.c: the value texts, by the ids it keeps for the host tests */
int rd_format(int id, int word, char *buf);
#define RD_SPRD 2
#define RD_ENV  3
#define RD_RATE 0x6e

static int32_t fmt_rate(char *buf, int32_t v, int32_t ctx, int32_t m)
{ (void)ctx; (void)m; return rd_format(RD_RATE, v, buf); }
static int32_t fmt_sprd(char *buf, int32_t v, int32_t ctx, int32_t m)
{ (void)ctx; (void)m; return rd_format(RD_SPRD, v, buf); }
static int32_t fmt_env(char *buf, int32_t v, int32_t ctx, int32_t m)
{ (void)ctx; (void)m; return rd_format(RD_ENV, v, buf); }
/* RTIO: the 8.8 ratio as "d.dd", clamped to 0.25 .. 8.00 as the render clamps it */
#define RTIO_MIN 0x40
#define RTIO_MAX 0x800
static int32_t fmt_rtio(char *buf, int32_t v, int32_t ctx, int32_t m)
{
    int32_t c;
    v = v < RTIO_MIN ? RTIO_MIN : v > RTIO_MAX ? RTIO_MAX : v;
    c = (v * 100 + 128) >> 8;                            /* hundredths */
    (void)ctx; (void)m;
    buf[0] = (char)('0' + c / 100);
    buf[1] = '.';
    buf[2] = (char)('0' + c / 10 % 10);
    buf[3] = (char)('0' + c % 10);
    buf[4] = 0;
    return 1;
}
/* RTIO's graphic is LEN's arc (0 .. 120 << 8): spread the ratio's range over it */
static int32_t gfx_rtio(int32_t v)
{
    v = v < RTIO_MIN ? RTIO_MIN : v > RTIO_MAX ? RTIO_MAX : v;
    return (v - RTIO_MIN) * (120 << 8) / (RTIO_MAX - RTIO_MIN);
}

/* A track turns to GRANULAR (machine-pages, the UI task): the stock switch wrote ONESHOT's defaults
 * (PLAY FWD, BR 0, STRT 0, LEN 120, LOOP 0) into the sound, and a cm_ui_v3 page's def is not applied
 * there. If the words still hold those, give the track sensible granular defaults; a sound saved
 * with exactly those values is re-defaulted once (0.0.x's tick handler did the same). */
#define DEF_RATE (36 << 8)                  /* periodic, ~24 Hz */
#define DEF_SPRD (64 << 8)                  /* none */
#define DEF_POS  0
#define DEF_RTIO 0x100                      /* 1:1 */
#define DEF_ENV  (64 << 8)                  /* sine */
static const int16_t gran_defaults[8] = { -1, DEF_RATE, DEF_SPRD, -1, DEF_POS, DEF_RTIO, DEF_ENV, -1 };
static void gran_on_switch(int32_t track, int32_t from, uint8_t *sound)
{
    uint16_t *w = (uint16_t *)(sound + FW_SOUND_PARAM(0));   /* knob k = w[k], 8.8 */
    int k;
    (void)from;
    if (w[1] != 0x0300 || w[2] != 0 || w[4] != 0 || w[5] != 0x7800 || w[6] != 0)
        return;
    for (k = 0; k < 8; k++)
        if (gran_defaults[k] >= 0) {
            w[k] = (uint16_t)gran_defaults[k];               /* the sound, for saving and the page */
            fw_set_param(gran_defaults[k], track, 17 + k);   /* and the engine's copy, which the switch synced before */
        }
}

#define KNOB(sn, ln, lk, lo, hi, d, f) { .name = sn, .lname = ln, .look = lk, \
    .flags = CM_RANGE | CM_DEFAULT, .min = lo, .max = hi, .def = d, .fmt = f }
static const struct cm_ui gran_page = {
    .abi = cm_ui_v3, .page_from = 0, .group = "GRAN",   /* ONESHOT's page */
    .on_switch = gran_on_switch,
    .knob = {
        [0] = { 0 },                                            /* A TUNE: stock */
        [1] = KNOB("RATE", "Grain Rate", ID_BR, 0, 0x7f00, DEF_RATE, fmt_rate),    /* B, PLAY's slot */
        [2] = { .name = "SPRD", .lname = "Spread", .flags = CM_DEFAULT, .def = DEF_SPRD,
                .fmt = fmt_sprd },                              /* C, BR's own slot and look */
        [3] = { 0 },                                            /* D SAMP: stock */
        [4] = { .name = "POS", .lname = "Position" },           /* E, STRT's slot, 0..120 */
        [5] = { .name = "RTIO", .lname = "Grain Ratio", .flags = CM_RANGE | CM_DEFAULT,
                .min = RTIO_MIN, .max = RTIO_MAX, .def = DEF_RTIO, .fmt = fmt_rtio,
                .gfx = gfx_rtio },                              /* F, LEN's own slot and look */
        [6] = KNOB("ENV", "Grain Envelope", ID_BR, 0, 0x7f00, DEF_ENV, fmt_env),  /* G, LOOP's slot */
        [7] = { 0 },                                            /* H LEV: stock */
    },
};
#undef KNOB

/* 11 x 7 menu icon, a word per column, rows in bits 31-25: scattered grains. */
static const uint32_t icon_px[11] = {
    0x38000000, 0x00000000, 0x10000000, 0x54000000, 0x10000000,
    0x00000000, 0x7c000000, 0x00000000, 0x10000000, 0x28000000,
    0x10000000,
};
static const uint32_t icon_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000,
};
static const struct {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} gran_icon = { fw_bitmap_vt, 11, 7, 1, icon_px, icon_mask, 0 };

/* params 0 and render 0: ONESHOT's eight parameters and its voice, the shadow granular.c plays over */
const struct cm_machine digigrain_granular_machine = {
    MACHINE_ID, "GRANULAR", "GRAN", &gran_icon, 0, 0, CM_UI_TAG, &gran_page,
};

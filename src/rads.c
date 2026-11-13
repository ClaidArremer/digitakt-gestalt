/* SPDX-License-Identifier: GPL-2.0-or-later
 * RADS: Geiger Counter-style digital destruction on the Digitakt mk1's master output (OS 1.53 and
 * 1.54), after RADS for Max for Live (itself inspired by the WMD Geiger Counter). By Robbie. It began
 * as the Digitone GESTALT's RADS; here it runs in GESTALT's chain (pump.c), set up by pumpdt.c's page.
 * The wave tables are GESTALT's own (tools/make_tables.py).
 *
 * PUMP 4.0: RADS runs first in PUMP's chain (pump.c's pm_insert), the compressor after it; the page
 * (pumpctl.c) sets digirads_active from TABLE (OFF: RADS fades out and leaves the chain). PUMP 3.0's
 * LOCUT and envelope follower are gone. 4.4: stereo, each channel through its own converter, table,
 * DC block, even and blend, at about what the mono loop cost.
 *
 * The chain: DRIVE preamp into the converter's brickwall clip -> SRR sample and hold -> the 8-bit
 * index -> the wave table (crossfading on every change, so it never clicks) -> DC block -> Even
 * (the effected signal comes out at the dry mix's level) -> BLEND against the dry mix -> a soft
 * ceiling at about -1 dBFS. Stereo (4.4; 4.0 to 4.3 ran the effect once on L + R, as the pedal is
 * mono).
 *
 *   TABLE (A) the wave table, 0..511: 00..FF musical, 00...FF. broken.
 *   DRIVE (B) the preamp, -12 .. +51.5 dB in half dBs (0 dB to start).
 *   SRR   (C) sample rate reduction, a sample and hold, 200 Hz .. 48 kHz (127: off).
 *   BLEND (D) dry / wet.
 * Fixed: tone flat, 8 bits, no mask, no gate, 8-bit (stepped) lookup.
 *
 * CPU. Per channel and frame: two or three multiplies (the gain, only when the hold takes a new
 * sample; even; the blend unless it is fully wet) and one table read (two while a change
 * crossfades); the levels for even are read a frame in eight and worked out once a 32-frame block. No libc, no FPU, no 64-bit products. With
 * TABLE OFF or BLEND at 0 it leaves the chain once it has faded out, and costs nothing.
 *
 * Levels: the effect path runs at 16 bits (x16, full scale 32767); the table is 8-bit anyway.
 * The dry path and the mix run at 24 bits, as core hands the block over.
 */
#include <stdint.h>
#ifdef PUMP_DT
#define RD_PACKED                       /* the Digitakt: the tables packed (its mods share 256 KB) */
#endif
#ifdef RD_PACKED
#define RD_NO_TABLES
#include "rads_data.h"
#include "rads_pack.h"
#else
#include "rads_data.h"
#endif
#ifdef PUMP_DT
#include "digitakt-mk1/core3.h"
#else
#include "digitone-mk1/core3.h"
#endif

#define RD_FRAMES 32

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The parameters, as pumpctl.c edits and stores them (TBL 0..511, the rest 0..127) */
int digirads_tbl = 0;
int digirads_gan = 24;          /* DRIVE: 0 dB */
int digirads_srt = 127;         /* every sample */
int digirads_mix = 127;         /* BLEND */
int digirads_active;            /* TABLE is not OFF */

/* For the page: the tables being heard (crossfading from ta to tb, xf 0..256), the envelope
 * (0..1024) and the mix as the envelope has moved it (0..1024) */
int digirads_ta, digirads_tb, digirads_xf = 256;
int digirads_wm;

/* the two tables' 256 values, as heard (for the page's curve) */
const uint8_t *volatile digirads_pa, *volatile digirads_pb;
#ifndef RD_PACKED
const uint8_t (*const digirads_tables)[256] = rd_tables;   /* for the page */
#endif

/* ---- state ---- */
static int rd_ta, rd_tb, rd_xf = 256;   /* a table change: from ta to tb, xf 0..256 (Q8) */
#ifdef RD_PACKED
/* Packed (the Digitakt): the UI task unpacks the table TABLE asks for into a free one of three
 * buffers and offers it (rd_ready); the render takes it once a crossfade is over and crossfades
 * from the buffer heard (rd_ba) to it (rd_bb). rd_ready is written by the UI task only while it is
 * -1, and by the render only while it is not, so neither ever writes over the other. */
static uint8_t rd_buf[3][256];
static volatile int rd_slot[3] = { -1, -1, -1 };   /* the table each buffer holds */
static volatile int rd_ba, rd_bb, rd_ready = -1;

/* table t (0..511) out of rads_pack.h (tools/pack_tables.py): its first value, then each step in
 * the canonical Huffman code, an escape followed by the value itself */
static void rd_unpack(int t, uint8_t *o)
{
    const unsigned char *d = rd_pk_bits + rd_pk_off[t];
    unsigned p = 0;
    int n, v = 0, b;
#define RD_BIT() ((d[p >> 3] >> (7 - (p & 7))) & 1u); p++
    for (b = 0; b < 8; b++) { v = (v << 1) | (int)RD_BIT(); }
    o[0] = (uint8_t)v;
    for (n = 1; n < 256; n++) {
        int c = 0, first = 0, idx = 0, l, sy = RD_PK_ESC;
        for (l = 1; l <= 16; l++) {
            int cnt = rd_pk_count[l];
            c |= (int)RD_BIT();
            if (c - first < cnt) { sy = rd_pk_sym[idx + c - first]; break; }
            idx += cnt;
            first = (first + cnt) << 1;
            c <<= 1;
        }
        if (sy == RD_PK_ESC) {
            v = 0;
            for (b = 0; b < 8; b++) { v = (v << 1) | (int)RD_BIT(); }
        } else {
            v += sy - RD_PK_LIM;
        }
        o[n] = (uint8_t)v;
    }
#undef RD_BIT
}

/* From the UI task: TABLE's table unpacked and offered, unless it is heard or on its way already,
 * or one is waiting to be taken */
static void rd_prepare(void)
{
    int want = clampi(digirads_tbl, 0, 511), b;
    if (rd_ready >= 0 || rd_slot[rd_bb] == want)
        return;
    for (b = 0; b < 3; b++)
        if (b != rd_ba && b != rd_bb)
            break;
    if (rd_slot[b] != want) {
        rd_slot[b] = -1;
        rd_unpack(want, rd_buf[b]);
        rd_slot[b] = want;
    }
    rd_ready = b;
}
#endif
#define RD_XF_STEP 12                   /* 21 blocks: a 14 ms crossfade a change */
/* a channel's state: the held table index, the DC block's last input and output (x16 << 4) */
struct rd_chs { int idx, dx, dy; };
static struct rd_chs rd_ch[2];
static unsigned rd_ph;                  /* the hold's phase, Q16 */
static int rd_ev = 4096 << 8;           /* even-level gain, Q20 */
static int rd_pin, rd_pout;             /* smoothed block powers (170 ms) */
static int rd_wm;                       /* the mix applied at the end of the last block, Q10 */

/* lu = log2 in Q8 (256 lu = 6.02 dB), as PUMP's units */
/* log2(p) in lu, for p > 0 (a block's power); 0 lu = 1. Once a block. */
static int rd_log2(unsigned p)
{
    int n = 31, m;
    unsigned x;
    if (!p)
        return -8000;
    while (!(p & (1u << n)))
        n--;
    /* fraction from the next 8 bits, linear in between: close enough for a level */
    x = n >= 8 ? p >> (n - 8) : p << (8 - n);       /* 256..511 */
    m = (int)x - 256;
    /* log2(1 + m/256) ~= m/256 * (1 + (256 - m) * 0.34 / 256), within 0.01 octave */
    return (n << 8) + m + ((m * (256 - m)) * 87 >> 16);
}

/* 2^(lu/256) as a Q12 gain (4096 = 1.0), lu within +-8 octaves. Once a block. */
static int rd_exp2(int lu)
{
    int n = lu >> 8, f = lu & 255;
    /* 2^(f/256) ~= 1 + f/256 * (1 - (256 - f) * 0.34 / 256), Q12 */
    int b = 4096 + ((f << 4) - (((f * (256 - f)) * 87 >> 16) << 4));
    if (n >= 0)
        return n > 6 ? 4096 << 6 : b << n;
    return -n > 12 ? 0 : b >> -n;
}

/* the ceiling: as PUMP's, transparent below ~-2.5 dBFS, 2:1 above, held under ~-0.9 dBFS */
#define RD_LIM_KNEE 0x600000
#define RD_LIM_MAX  0x740000
static inline int rd_ceil(int x)
{
    if (x > RD_LIM_KNEE) {
        x = RD_LIM_KNEE + ((x - RD_LIM_KNEE) >> 1);
        return x > RD_LIM_MAX ? RD_LIM_MAX : x;
    }
    if (x < -RD_LIM_KNEE) {
        x = -RD_LIM_KNEE - ((-RD_LIM_KNEE - x) >> 1);
        return x < -RD_LIM_MAX ? -RD_LIM_MAX : x;
    }
    return x;
}


/* a block's constants for rd_frames (radsloop.s reads them by these offsets: keep them in step) */
struct rd_k {
    int g, lim, ev, f, inc, wm0, dwm, full;     /*  0  4  8 12 16 20 24 28 */
    unsigned ph;                                /* 32 */
    const uint8_t *ta, *tb;                     /* 36 40 */
    int sh;                                     /* 44: the hold on */
};

/* the DC block's leak, dy >> 11, but at least 1 while dy is above 0, so it settles at 0 (a
 * positive leftover under 2048 never leaked away, and the compressor's make-up lifted it to a
 * steady offset in silence; below 0 the shift already rounds away from 0) */
static inline int rd_leak(int dy)
{
    int l = dy >> 11;
    return (!l && dy > 0) ? 1 : l;
}

/* the converter: the preamp, its clip and the 8-bit index in one step, on x16 */
static inline int rd_index(int y, int g, int lim)
{
    int i;
    if (y >= lim) return 255;
    if (y <= -lim) return 0;
    i = (y * g + 0x808000) >> 16;
    return i > 255 ? 255 : i;                   /* just under the clip it can round to 256 */
}

/* The block's frames (in and out interleaved L, R), in stereo (GESTALT 4.4): each channel through
 * the converter (the sample and hold keeps its index, which is the same thing), the table, the DC
 * block (at 16 bits << 4), even (Q11 on the DC block's x16: its overshoot to twice full scale times 4
 * stays in 32 bits), the blend against its own dry signal, the ceiling. The hold's clock is shared,
 * and the levels for even come from the two channels' sums, a frame in eight (plenty for its 170 ms).
 * Returns the hold's phase. */
#if defined(__mcoldfire__) && !defined(RD_C_LOOP)
/* on the Digitone: the same loop in assembly (radsloop.s), which keeps it in registers */
unsigned rd_frames_asm(const int32_t *in, int32_t *out, struct rd_chs *s, const struct rd_k *k,
                       unsigned *pinp, unsigned *poutp);
#define rd_frames rd_frames_asm
#else
static unsigned rd_frames(const int32_t *in, int32_t *out, struct rd_chs *s, const struct rd_k *k,
                          unsigned *pinp, unsigned *poutp)
{
    int il = s[0].idx, dxl = s[0].dx, dyl = s[0].dy, ir = s[1].idx, dxr = s[1].dx, dyr = s[1].dy;
    const int g = k->g, lim = k->lim, ev = k->ev, f = k->f, sh = k->sh;
    const uint8_t *ta = k->ta, *tb = k->tb;
    unsigned ph = k->ph, inc = k->inc, pin = 0, pout = 0;
    int i, wmq = 0, wm = 0;

    for (i = 0; i < RD_FRAMES; i++) {
        int xl = in[0] >> 8, xr = in[1] >> 8, o, t, w;
        if (sh)
            ph += inc;
        if (!sh || ph >= 65536u) {              /* new indexes (held: as they were) */
            if (sh)
                ph &= 0xffffu;                  /* no hold: the phase is left alone */
            il = rd_index(xl >> 8, g, lim);
            ir = rd_index(xr >> 8, g, lim);
        }
        o = ta[il] << 8;
        if (f) o += (tb[il] - ta[il]) * f;      /* a table change crossfading */
        t = (o - 32640) * 16;                   /* x16 << 4, the table's centre at 0 */
        dyl += t - dxl - rd_leak(dyl);          /* DC block */
        dxl = t;
        o = ta[ir] << 8;
        if (f) o += (tb[ir] - ta[ir]) * f;
        t = (o - 32640) * 16;
        dyr += t - dxr - rd_leak(dyr);
        dxr = t;
        if (!(i & 7)) {                         /* the levels, a frame in eight */
            int xm = ((xl + xr) >> 1) >> 8, yh = ((dyl + dyr) >> 1) >> 6;
            pin += (unsigned)((xm * xm) >> 8);
            pout += (unsigned)((yh * yh) >> 4);
        }
        if (!k->full) {
            wmq += k->dwm;
            wm = k->wm0 + (wmq >> 11);
        }
        w = ((dyl >> 4) * ev) >> 3;             /* even: x16 times Q11 (up to 4) >> 3: 24 bits */
        if (!k->full) w = xl + ((((w - xl) >> 7) * wm) >> 3);
        out[0] = (int32_t)((uint32_t)rd_ceil(w) << 8);
        w = ((dyr >> 4) * ev) >> 3;
        if (!k->full) w = xr + ((((w - xr) >> 7) * wm) >> 3);
        out[1] = (int32_t)((uint32_t)rd_ceil(w) << 8);
        in += 2;
        out += 2;
    }
    s[0].idx = il; s[0].dx = dxl; s[0].dy = dyl;
    s[1].idx = ir; s[1].dx = dxr; s[1].dy = dyr;
    *pinp += pin;
    *poutp += pout;
    return ph;
}
#endif

/* One 32-frame block: core's input (Q1.31) in, the output (Q1.31) out */
void digirads_block(const int32_t *in, int32_t *out)
{
    int f, g, lim, wm0, wm1, dwm;
    unsigned pin = 0, pout = 0, inc;
    const uint8_t *ta, *tb;
#ifndef RD_PACKED
    int tgt = clampi(digirads_tbl, 0, 511);
#endif

    /* TBL: each change crossfades from the table heard to the new one (14 ms); a change made
     * while one is under way waits for it, so turning TBL morphs from table to table and a jump
     * (a pattern change) never clicks */
#ifdef RD_PACKED
    if (rd_xf >= 256) {
        int r = rd_ready;
        rd_ba = rd_bb;
        if (r >= 0) {                               /* the table TABLE asks for, unpacked */
            if (rd_slot[r] != rd_slot[rd_bb]) {
                rd_bb = r;
                rd_xf = 0;
            }
            rd_ready = -1;
        }
    } else {
        rd_xf += RD_XF_STEP;
        if (rd_xf > 256)
            rd_xf = 256;
    }
    f = rd_xf >= 256 ? 0 : rd_xf;
    ta = rd_buf[rd_xf >= 256 ? rd_bb : rd_ba];
    tb = rd_buf[rd_bb];
    rd_ta = rd_slot[rd_ba]; rd_tb = rd_slot[rd_bb];
#else
    if (rd_xf >= 256) {
        rd_ta = rd_tb;
        if (tgt != rd_tb) {
            rd_tb = tgt;
            rd_xf = 0;
        }
    } else {
        rd_xf += RD_XF_STEP;
        if (rd_xf > 256)
            rd_xf = 256;
    }
    f = rd_xf >= 256 ? 0 : rd_xf;
    ta = rd_tables[rd_xf >= 256 ? rd_tb : rd_ta];
    tb = rd_tables[rd_tb];
#endif
    digirads_ta = rd_ta; digirads_tb = rd_tb; digirads_xf = rd_xf;
    digirads_pa = ta; digirads_pb = tb;

    g = rd_gain[clampi(digirads_gan, 0, 127)];
    lim = (32767 << 8) / g + 1;                     /* from it on, the converter clips */
    inc = rd_sr_inc[clampi(digirads_srt, 0, 127)];

    /* the blend for this block (TABLE OFF: fade out) */
    {
        int w = (clampi(digirads_mix, 0, 127) * 1024 + 63) / 127;
        if (!digirads_active)
            w = 0;
        wm1 = w;
    }
    wm0 = rd_wm;
    dwm = (wm1 - wm0) * 2048 / RD_FRAMES;           /* Q11 a frame, ramped across the block */
    rd_wm = wm1;
    digirads_wm = wm1;

    /* The frames, in stereo (rd_frames) */
    {
        struct rd_k k;
        k.g = g; k.lim = lim; k.ev = rd_ev >> 9; k.f = f; k.ta = ta; k.tb = tb;
        k.inc = inc; k.ph = rd_ph; k.wm0 = wm0; k.dwm = dwm;
        k.full = wm0 == 1024 && wm1 == 1024;
        k.sh = inc < 65536u;
        rd_ph = rd_frames(in, out, rd_ch, &k, &pin, &pout);
        pin <<= 4;                            /* as if every frame, and both channels, had been */
        pout <<= 4;                           /* measured */
    }

    /* even, once a block: the effected signal's level towards the dry mix's, from their mean
     * powers over the last ~170 ms, within +-12 dB (as RADS for Live's); held through silence */
    {
        int tgt_lu;
        rd_pin += ((int)pin - rd_pin) / 256;
        rd_pout += ((int)pout - rd_pout) / 256;
        if (rd_pin > 1024) {                             /* the dry mix is not silent (~-54 dBFS) */
            tgt_lu = clampi((rd_log2((unsigned)rd_pin) - rd_log2((unsigned)rd_pout + 1)) / 2,
                            -510, 510);                  /* half: power to level */
            rd_ev += (rd_exp2(tgt_lu) * 256 - rd_ev) >> 9;             /* 340 ms */
        }
    }

}

static int rd_run;                      /* RADS holds (or is waiting for) the insert */

/* RADS has faded out */
int digirads_idle(void)
{
    return rd_wm == 0;
}

/* From the UI task, every frame (pump.c's owner sync): whether RADS wants the insert, as the
 * master FX with its mix (or soon) above 0, or is still fading out. Stopping: the filters start
 * clean next time. Starting: the table set is the table heard, with no crossfade from 00. */
int digirads_sync(void)
{
    int want = digirads_active && digirads_mix > 0;
#ifdef RD_PACKED
    rd_prepare();
    if ((!want && rd_wm == 0) || !rd_run) {         /* not heard: what TABLE asks for, at once */
        if (rd_ready >= 0) {
            rd_ba = rd_bb = rd_ready;
            rd_ready = -1;
            rd_xf = 256;
        }
        digirads_pa = digirads_pb = rd_buf[rd_bb];
    }
#endif
    if (!want && rd_wm == 0) {
        digirads_ta = digirads_tb = clampi(digirads_tbl, 0, 511);   /* the page shows TBL */
        digirads_xf = 256;
#ifndef RD_PACKED
        digirads_pa = digirads_pb = rd_tables[digirads_ta];
#endif
        if (rd_run) {
            int c;
            rd_run = 0;
            for (c = 0; c < 2; c++)
                rd_ch[c].dx = rd_ch[c].dy = rd_ch[c].idx = 0;
        }
        return 0;
    }
    if (!rd_run) {
        rd_ta = rd_tb = clampi(digirads_tbl, 0, 511);
        rd_xf = 256;
        rd_run = 1;
    }
    return 1;
}

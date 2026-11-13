/* SPDX-License-Identifier: GPL-2.0-or-later
 * pumpdt: the GESTALT master page on the Digitakt mk1, OS 1.53 and 1.54.
 * By Robbie. GESTALT is PUMP and RADS in one chain: RADS's Geiger Counter-style wave table
 * destruction (rads.c, in stereo), then the SP-303 Vinyl Sim-style compressor (pump.c), at the
 * master output. The page is the Digitone GESTALT's (pumpctl.c there); what holds it up is core
 * 4.1's master pages (core_mpages) and master output (ev_render_master), as PUMP for Digitakt's.
 *
 * GESTALT takes the stock compressor's place: its page among the master pages (FUNC + LFO), its
 * eight parameters for its settings, and its slot in the render (pump.c, digipump_comp).
 *
 *   A = TABLE (OFF, 00..FF, 00...FF.), B = DRIVE (into RADS), C = SRR, D = BLEND (RADS's dry /
 *   wet); E = COMP (OFF, then the compressor), F = RATIO (AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1,
 *   LIMIT), G = REL,
 *   H = OUT (the chain's level, in dB). Press a knob and turn it for big steps; FUNC + turn: each
 *   knob's snap points (TABLE: 16 tables a detent). FUNC + REC / STOP / PLAY: copy, paste, clear.
 *
 * The page: a small screen with the wave table's curve (OFF with TABLE OFF), bouncing with the
 * output's level; the eight controls with their values; a gain-reduction meter. YES: the curve
 * across the page, then a graph of the last 2.8 s of gain reduction. SETTINGS > GLOBAL FX/MIX >
 * GESTALT ANIMATION unticked: no animation and no meter.
 */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

/* firmware locations (mod.json's defsym, one set per OS) */
#define DN_FW  ((const unsigned char *)DT_FONT_W_ADDR)         /* the stock 5-px font: widths, */
#define DN_FO  ((const unsigned short *)DT_FONT_O_ADDR)        /* offsets, */
#define DN_FB  ((const unsigned char *)DT_FONT_B_ADDR)         /* columns */

#define DT_K_FUNC     1
#define DT_K_REC      9
#define DT_K_PLAY     10
#define DT_K_STOP     11
#define DT_K_YES      12
#define DT_K_PUSH_A   40           /* knob A pressed; .. H = 47 */
#define PC_FAST       8            /* pressed + turn: to the next multiple of 8 */
#define DT_PER_STEP   16           /* knob counts a detent on the Digitakt */
#define DG_TOP        53           /* rows 53..63 are the stock title bar */
#define PC_FRAME      75           /* audio blocks a frame: 1500 / 75 = 20 fps */
#define PC_FRAME_STILL 750         /* still: 2 fps */

extern int digipump_on, digipump_squash, digipump_pump, digipump_out, digipump_rat;
extern int digipump_gr, digipump_meter, digipump_lvl;
extern volatile unsigned digipump_blocks;
void digipump_owner_sync(void);
int digipump_running(void);
extern int digirads_tbl, digirads_gan, digirads_srt, digirads_mix, digirads_active;
extern int digirads_ta, digirads_tb, digirads_xf, digirads_wm;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- the settings: the stock compressor's eight parameters ------------------------------------
 * The compressor is gone (pump.c, digipump_comp); its parameters, ids 86..93, hold GESTALT's: the
 * firmware keeps them per pattern in the kit (or globally, with SETTINGS > GLOBAL FX/MIX's
 * compressor set so), saves them with the project and over a power-off, and the render hands them
 * to digipump_comp, which passes them here (pumpdt_params). They are 8.8 words, clamped to the stock
 * ranges (0..127; RAT 0..7; SCS 0..10). Eight are not enough for nine settings in whole steps, so
 * TABLE and SRR use the words' fractions:
 *   THR  COMP (0 OFF)          ATK  TABLE: 63 a table (0 OFF, 1..512), 0 .. 126.0
 *   REL  REL (127 - the knob)  MUP  DRIVE
 *   RAT  RATIO, 0..7           SCS  SRR: 2.0 + 1/16 a step, 2.0 .. 9.94 -- and GESTALT's mark:
 *   SCF  OUT                   MIX  BLEND          a word under 2.0 is PUMP for Digitakt's (0 off,
 *                                                  1 on) or the stock's
 * Should a fraction ever be lost, TABLE falls to a table in four and SRR to eight steps: still
 * GESTALT's, nothing worse. An edit goes through the compressor page's own edit (the master
 * view's vtable slot 22, 0x400388f6: id, a change in 8.8, kit or global as the stock decides,
 * clamped to the stock range), with the page shown taken for the compressor's (kind 11) for
 * that one call. */
#define CP_THR 86
#define DT_COMP_EDIT ((int32_t (*)(void *, int32_t, int32_t, int32_t, void *))0x400388f6)
#define GS_N 8
enum { W_THR, W_ATK, W_REL, W_MUP, W_RAT, W_SCS, W_SCF, W_MIX };
static int gs_kit[GS_N];                    /* the kit's eight words, as far as we know them */
static int gs_last[GS_N], gs_still;         /* the render's last words, and for how many blocks */
static int gs_seen;                         /* the render has handed them over (settled) */
static int gs_ours;                         /* they are in GESTALT's layout */

/* the eight settings, as the knobs sit (A..H) */
static int hy_tbl, hy_comp = 80, hy_rstep;  /* TABLE 0 OFF, else the table + 1; RATIO step 0..7 */
/* RATIO's eight steps, in the DSP's terms (pump.c's 0..127) */
static const unsigned char gs_rat_tab[8] = { 0, 1, 23, 39, 76, 88, 126, 127 };   /* 0.2: 8:1 for 6:1 */
static const char *const rat_names[8] = { "AUT", "2:1", "3:1", "4:1", "8:1", "10:1", "20:1", "LIM" };
static const char *const rat_long[8] = { "AUTO", "2:1", "3:1", "4:1", "8:1", "10:1", "20:1", "LIMIT" };
/* TABLE OFF, DRIVE 0 dB, SRR OFF, BLEND 100 %; COMP 80, RATIO AUTO, REL 64 (stored 63), OUT 0 dB */
static const short gs_def[GS_N] = { 0, 24, 127, 127, 80, 0, 63, 64 };
/* the stock compressor's defaults (THR .. MIX, whole steps): a kit nothing has touched */
static const unsigned char cp_stock[8] = { 32, 24, 32, 64, 3, 0, 80, 0 };

static int gs_r8(int w) { return clampi((w + 128) >> 8, 0, 127); }

static void gs_defaults(void)
{
    hy_tbl = gs_def[0]; digirads_gan = gs_def[1]; digirads_srt = gs_def[2]; digirads_mix = gs_def[3];
    hy_comp = gs_def[4]; hy_rstep = gs_def[5]; digipump_pump = gs_def[6]; digipump_out = gs_def[7];
}

/* the settings into the DSP's terms */
static void gs_apply(void)
{
    digipump_on = hy_comp > 0;
    if (hy_comp > 0)
        digipump_squash = hy_comp;
    digipump_rat = gs_rat_tab[hy_rstep & 7];
    digirads_active = hy_tbl > 0;
    if (hy_tbl > 0)
        digirads_tbl = hy_tbl - 1;
}

/* The kit's words into the settings: GESTALT's layout as it is; PUMP for Digitakt's (SCS 0 or 1) as
 * its compressor's settings, PUMP off as COMP OFF, with RADS's at its starting values; a kit nothing
 * has touched, GESTALT's starting values. The page writes GESTALT's layout back with its first edit. */
static void gs_decode(const int *w)
{
    int i, same = 1;
    if (w[W_SCS] >= 2 * 256 - 8) {                  /* GESTALT's */
        gs_ours = 1;
        hy_comp = gs_r8(w[W_THR]);
        hy_tbl = clampi((w[W_ATK] + 31) / 63, 0, 512);
        digipump_pump = gs_r8(w[W_REL]);
        digirads_gan = gs_r8(w[W_MUP]);
        hy_rstep = gs_r8(w[W_RAT]) & 7;
        digirads_srt = clampi((w[W_SCS] - 512 + 8) >> 4, 0, 127);
        digipump_out = gs_r8(w[W_SCF]);
        digirads_mix = gs_r8(w[W_MIX]);
    } else {
        gs_ours = 0;
        for (i = 0; i < GS_N; i++)
            if (gs_r8(w[i]) != cp_stock[i])
                same = 0;
        gs_defaults();
        if (!same) {                                 /* PUMP for Digitakt's */
            hy_comp = gs_r8(w[W_SCS]) ? gs_r8(w[W_THR]) : 0;
            digipump_pump = gs_r8(w[W_REL]);
            hy_rstep = gs_r8(w[W_RAT]) & 7;
            digipump_out = gs_r8(w[W_SCF]);
        }
    }
    gs_apply();
}

/* Once a block, from the render: the eight words as it has them. They glide to a new value over a
 * few ms (the firmware smooths its parameters), so they are read only once they have stood still
 * for 4 blocks; then, if they are not what we know the kit to hold (a pattern change, a kit
 * reload, GLOBAL FX/MIX), they are the settings. While a knob turns, the page's are. */
const int16_t *gs_p;                        /* where the render reads them (for the emulator's checks) */
void pumpdt_params(const int16_t *p)
{
    int i, moved = 0;
    gs_p = p;
    for (i = 0; i < GS_N; i++)
        if (p[i] != gs_last[i]) {
            gs_last[i] = p[i];
            moved = 1;
        }
    if (moved) {
        gs_still = 0;
        return;
    }
    if (gs_still < 4 && ++gs_still == 4) {
        for (i = 0; i < GS_N; i++)
            if (gs_last[i] != gs_kit[i])
                moved = 1;
        if (moved || !gs_seen) {
            for (i = 0; i < GS_N; i++)
                gs_kit[i] = gs_last[i];
            gs_decode(gs_kit);
        }
        gs_seen = 1;
    }
}

/* the words for the settings now */
static void gs_words(int *w)
{
    w[W_THR] = hy_comp << 8;
    w[W_ATK] = hy_tbl * 63;
    w[W_REL] = digipump_pump << 8;
    w[W_MUP] = digirads_gan << 8;
    w[W_RAT] = (hy_rstep & 7) << 8;
    w[W_SCS] = 512 + digirads_srt * 16;
    w[W_SCF] = digipump_out << 8;
    w[W_MIX] = digirads_mix << 8;
}

#ifdef PC_TEST_PARAMS
static int16_t pc_fake[GS_N] = { 32 << 8, 24 << 8, 32 << 8, 64 << 8, 3 << 8, 0, 80 << 8, 0 };
#endif

static int pc_kinds(char *view, int32_t **vp)
{
    int32_t *v = *(int32_t **)(view + 124), *e = *(int32_t **)(view + 128);
    *vp = v;
    if (!v || !e || e <= v || e - v > 32)
        return 0;
    return (int)(e - v);
}

/* parameter id (86..93) by a change of d (8.8), while our page is shown */
static void pc_edit(void *view, int id, int d)
{
    int32_t *v;
    int k = pc_kinds((char *)view, &v), i = *(int32_t *)((char *)view + 144);
    if (!d || i < 0 || i >= k || v[i] != 0)
        return;
    v[i] = 11;                                   /* the compressor's page, for the edit */
#ifdef PC_TEST_PARAMS
    pc_fake[id - CP_THR] = (int16_t)(pc_fake[id - CP_THR] + d);                   /* the emulator: no render, a kit of our own */
#else
    DT_COMP_EDIT(view, id, d, 0, 0);
#endif
    v[i] = 0;
}

/* the settings now into the kit's words: those that differ (all eight, the first time, so the
 * layout is GESTALT's from then on). The edit adds a change to what the kit holds, so it is worked
 * out from our copy of the kit, never from the gliding words. */
static void gs_commit(void *view)
{
    int w[GS_N], i;
    gs_words(w);
    for (i = 0; i < GS_N; i++)
        if (!gs_ours || w[i] != gs_kit[i]) {
            pc_edit(view, CP_THR + i, w[i] - gs_kit[i]);
            gs_kit[i] = w[i];
        }
    gs_ours = 1;
    gs_apply();
}


/* The stock compressor's page out of the master pages: ours follows it
 * (after = 11), so it takes its place. Done again whenever core or the
 * firmware builds the list anew. Core's places for the pages after it move
 * down one. */
extern struct core_mpage *core_mpages[];
static void pc_drop_comp(void *view)
{
    int32_t *v;
    int n = pc_kinds((char *)view, &v), i, j, cur;
    struct core_mpage **mp;
    for (i = 0; i < n; i++)
        if (v[i] == 11)
            break;
    if (i >= n || i + 1 >= n || v[i + 1] != 0)
        return;                                  /* none, or core has not placed ours */
    for (j = i; j < n - 1; j++)
        v[j] = v[j + 1];
    *(int32_t **)((char *)view + 128) = v + n - 1;
    cur = *(int32_t *)((char *)view + 144);
    if (cur > i)
        *(int32_t *)((char *)view + 144) = cur - 1;
    for (mp = core_mpages; *mp; mp++)
        if ((*mp)->view == view && (*mp)->pos > i)
            (*mp)->pos--;
    fw_invalidate(view);
}


/* ---- the page ------------------------------------------------------------------ */
struct core_mpage pumpdt_page;
static void *pc_view;               /* the master view, from its last draw */
static int pc_func;                 /* FUNC held: knob turns snap */
static int pc_eacc[10];             /* per-knob counts since the last step */
static unsigned pc_blk_last;        /* digipump_blocks at the last tick */
static int pc_due;                  /* blocks toward the next frame */
static unsigned pc_draws_seen;      /* core_mview_draws at the last tick */
static int pc_quiet;                /* ticks with no draw of the master view */

/* SETTINGS > GLOBAL FX/MIX > GESTALT ANIMATION (pumpdtset.s): byte 0 set = still */
unsigned char pumpctl_anim_off[4];
#define pc_still() (pumpctl_anim_off[0] != 0)

/* ---- drawing (direct framebuffer, 1 bpp column-major; y = 0 at the bottom,
 * the stock convention) -------------------------------------------------------*/
static unsigned char *dg_fb;

static void dg_fill(int x0, int y0, int x1, int y1, int c)
{
    int x, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 < y1) { t = y0; y0 = y1; y1 = t; }        /* y0 = the top row */
    if (x0 < 0) x0 = 0;
    if (x1 > 127) x1 = 127;
    if (y1 < 0) y1 = 0;
    if (y0 > 63) y0 = 63;
    for (x = x0; x <= x1; x++) {
        int rt = 63 - y0, rb = 63 - y1, p;
        unsigned char *col = dg_fb + (x << 3);
        for (p = rt >> 3; p <= (rb >> 3); p++) {
            unsigned char m = 0xffu;
            if (p == (rt >> 3)) m &= (unsigned char)(0xffu << (rt & 7));
            if (p == (rb >> 3)) m &= (unsigned char)(0xffu >> (7 - (rb & 7)));
            if (c) col[7 - p] |= m; else col[7 - p] &= (unsigned char)~m;
        }
    }
}


static int dg_strw(const char *s, int sc)
{
    int w = 0;
    for (; *s; s++)
        w += (DN_FW[(unsigned char)*s] + 1) * sc;
    return w;
}

static void dg_glyph(int x, int ytop, int ch, int sc)
{
    int w = DN_FW[ch & 0xff];
    int off = DN_FO[ch & 0xff];
    int c, r;
    for (c = 0; c < w; c++) {
        int bits = DN_FB[off + c];
        for (r = 0; r < 8; r++) {
            if (bits & (1 << r)) {              /* bit 0 = top of the glyph */
                int px = x + c * sc;
                int py = ytop - r * sc;
                if (sc == 1) {                  /* one pixel: set it, no fill */
                    if ((unsigned)px < 128u && (unsigned)py < 64u) {
                        int rr = 63 - py;
                        dg_fb[(7 - (rr >> 3)) + (px << 3)] |= (unsigned char)(1u << (rr & 7));
                    }
                } else {
                    dg_fill(px, py, px + sc - 1, py - sc + 1, 1);
                }
            }
        }
    }
}

static void dg_text(int x, int ytop, const char *s, int sc)
{
    for (; *s; s++) {
        dg_glyph(x, ytop, (unsigned char)*s, sc);
        x += (DN_FW[(unsigned char)*s] + 1) * sc;
    }
}

/* ---- the page, in screen coordinates (m_*: x right, y down from the top of
 * the panel, as the preview was drawn; rows 0..10 are the stock title bar) ---- */
static void m_fill(int x0, int y0, int x1, int y1, int c) { dg_fill(x0, 63 - y0, x1, 63 - y1, c); }
static void m_px(int x, int y, int c)
{
    int r;
    if ((unsigned)x > 127u || (unsigned)y > 63u)
        return;
    r = y;                                         /* screen row */
    if (c)
        dg_fb[(7 - (r >> 3)) + (x << 3)] |= (unsigned char)(1u << (r & 7));
    else
        dg_fb[(7 - (r >> 3)) + (x << 3)] &= (unsigned char)~(1u << (r & 7));
}
static int m_w(const char *s) { return dg_strw(s, 1) - 1; }
static void m_text(int x, int y, const char *s) { dg_text(x, 63 - y, s, 1); }
static void m_ctext(int cx, int y, const char *s) { m_text(cx - m_w(s) / 2, y, s); }
static void m_rtext(int xr, int y, const char *s) { m_text(xr - m_w(s), y, s); }
static void m_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, e2, n = 256;           /* n: a hard stop, whatever happens */
    while (n-- > 0) {
        m_px(x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;                         /* both tests on the same error */
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void m_itoa(char *b, int v)
{
    int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    if (v >= 100) b[n++] = (char)('0' + v / 100);
    if (v >= 10) b[n++] = (char)('0' + (v / 10) % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = 0;
}

/* The animation's state, moved on by pumpdt_tick */
#define PC_HIST 56                          /* graph: 2 px a redraw, 2.8 s */
static unsigned char pc_hist[PC_HIST];      /* gain reduction per redraw, dB */
static int pc_hpos;
static int pc_gr_db, pc_pk_db, pc_pk_hold;  /* the meter, its peak and hold */
static int pc_graph;                        /* YES: 0 the controls, 1 the table, 2 the graph */
static int pc_amp = 256;                    /* the curve's bounce, Q8 of its full height */
static int pc_db(int lu) { return lu > 0 ? (lu * 385) >> 14 : 0; }   /* lu -> dB */

/* a 24-bit peak in lu (log2 Q8), 0 = full scale; an approximation, for the page */
static int pc_lu(int p)
{
    int n = 23, f;
    if (p <= 0)
        return -6000;
    if (p > 0x7fffff)
        p = 0x7fffff;
    while (!(p & (1 << n)))
        n--;
    f = n >= 8 ? (p >> (n - 8)) & 255 : (p << (8 - n)) & 255;
    return ((n - 23) << 8) + f;
}

/* Each redraw: the gain reduction since the last one (its peak, from the
 * render), into the meter, its peak hold and the graph; and the output's
 * level, into the curve's bounce: full height at 0 dBFS, falling with the
 * level to 0.4 of it at -36 dB and below, dropping back at most an eighth a redraw. */
static void pc_meter_step(void)
{
    extern int digipump_grpk;
    int d = digipump_on ? pc_db(digipump_grpk) : 0, a;
    digipump_grpk = 0;
    pc_gr_db = d;
    if (d >= pc_pk_db) {
        pc_pk_db = d;
        pc_pk_hold = 20;                        /* 1 s */
    } else if (pc_pk_hold > 0) {
        pc_pk_hold--;
    } else if (pc_pk_db > 0) {
        pc_pk_db--;
    }
    pc_hist[pc_hpos] = (unsigned char)(d > 255 ? 255 : d);
    pc_hpos = (pc_hpos + 1) % PC_HIST;
    if (!digipump_running()) {
        a = 256;                                /* not measured: the curve as it is */
    } else {
        a = clampi(256 + pc_lu(digipump_lvl) / 10, 102, 256); /* -36 dB (1536 lu): 0.4 */
    }
    digipump_lvl = 0;
    pc_amp = a >= pc_amp ? a : (pc_amp - a > 32 ? pc_amp - 32 : a);
}

/* the meter: 12 segments of 2 dB from the top, a peak tick, the dB below */
static void m_meter(void)
{
    char b[6];
    int i, pi;
    m_ctext(124, 12, "GR");
    for (i = 0; i < 12; i++) {
        int y = 21 + i * 3;
        if (pc_gr_db > i * 2 + 1)
            m_fill(121, y, 126, y + 1, 1);
        else {
            m_px(121, y, 1);
            m_px(126, y, 1);
        }
    }
    pi = pc_pk_db / 2;
    if (pi > 11) pi = 11;
    if (pc_pk_db > 1)
        m_fill(120, 22 + pi * 3, 127, 22 + pi * 3, 1);
    m_itoa(b, pc_gr_db);
    m_ctext(124, 56, b);
}

/* OUT in tenths of a dB: 64 = 0, 0.5 dB a step below, 6/63 dB above */
static int pc_out_tenths(int v)
{
    return v <= 64 ? (v - 64) * 5 : ((v - 64) * 60) / 63;
}

/* ---- the wave table's curve ----------------------------------------------- */
#define RD_NO_TABLES
#include "rads_data.h"
extern const unsigned char *volatile digirads_pa, *volatile digirads_pb;   /* the tables heard */
extern int digirads_wm;

static int rs_hexdig(int v) { return v < 10 ? '0' + v : 'A' + v - 10; }
static void rs_hex(char *b, int t)          /* 00..FF, then 00...FF. */
{
    int n = t & 255;
    b[0] = (char)rs_hexdig(n >> 4);
    b[1] = (char)rs_hexdig(n & 15);
    b[2] = t >= 256 ? '.' : 0;
    b[3] = 0;
}

/* the chain's curve at 8-bit index i (0..255): the table heard (blended through a change),
 * blended with the straight line by BLEND as RADS applies it (TABLE OFF: the straight line) */
static int rs_curve_at(int i)
{
    const unsigned char *pa = digirads_pa, *pb = digirads_pb;
    int a = pa ? pa[i] : i, b = pb ? pb[i] : a;
    int f = digirads_xf >= 256 ? 0 : digirads_xf, w;
    int c = a + (((b - a) * f) >> 8);
#ifdef PC_TEST_CURVE
    if (1)                                  /* the emulator runs no audio: from the settings */
#else
    if (pc_still() || !digipump_running())
#endif
        w = hy_tbl ? (digirads_mix * 1024) / 127 : 0;
    else
        w = digirads_wm;
    return i + (((c - i) * w) >> 10);
}

/* the curve in a box x0..x1, y0 (top) .. y1, joined up, its height pc_amp of the box's */
static void rs_curve(int x0, int y0, int x1, int y1)
{
    int w = x1 - x0, h = y1 - y0, i, py = 0, mid2 = y0 + y1;
#ifdef PC_TEST_CURVE
    int amp = 192;
#else
    int amp = pc_still() ? 256 : pc_amp;
#endif
    for (i = 0; i <= w; i++) {
        int c = rs_curve_at((i * 255) / w) * 2 - 255;       /* -255 .. 255 */
        int y = (mid2 - (c * h * amp) / (255 * 256)) / 2;
        if (i)
            m_line(x0 + i - 1, py, x0 + i, y);
        else
            m_px(x0, y, 1);
        py = y;
    }
}

/* the small screen: a rounded frame, a dotted centre line, the curve */
static void rs_screen(int x0, int y0, int x1, int y1)
{
    int x;
    m_fill(x0 + 2, y0, x1 - 2, y0, 1); m_fill(x0 + 2, y1, x1 - 2, y1, 1);
    m_fill(x0, y0 + 2, x0, y1 - 2, 1); m_fill(x1, y0 + 2, x1, y1 - 2, 1);
    m_px(x0 + 1, y0 + 1, 1); m_px(x1 - 1, y0 + 1, 1);
    m_px(x0 + 1, y1 - 1, 1); m_px(x1 - 1, y1 - 1, 1);
    if (!hy_tbl) {                          /* TABLE OFF: OFF, no curve */
        m_ctext((x0 + x1) / 2, (y0 + y1) / 2 - 3, "OFF");
        return;
    }
    for (x = x0 + 3; x < x1 - 1; x += 3)
        m_px(x, (y0 + y1) / 2, 1);
    rs_curve(x0 + 2, y0 + 2, x1 - 2, y1 - 2);
}

/* little string helpers for the cells and the drop-downs */
static void rs_cat(char *b, const char *t) { while (*b) b++; while (*t) *b++ = *t++; *b = 0; }
static void rs_signed(char *b, int v)        /* "+12", "0", "-6" */
{
    if (v > 0) { b[0] = '+'; m_itoa(b + 1, v); } else m_itoa(b, v);
}

/* ---- the controls --------------------------------------------------------- */
/* the knob's value, 0..its maximum (REL: the knob, 127 - the stored value) */
static int hy_get(int id)
{
    switch (id) {
    case 1: return hy_tbl;
    case 2: return digirads_gan;
    case 3: return digirads_srt;
    case 4: return digirads_mix;
    case 5: return hy_comp;
    case 6: return hy_rstep;
    case 7: return 127 - digipump_pump;
    default: return digipump_out;
    }
}

static void hy_set(int id, int v)
{
    switch (id) {
    case 1: hy_tbl = clampi(v, 0, 512); break;
    case 2: digirads_gan = clampi(v, 0, 127); break;
    case 3: digirads_srt = clampi(v, 0, 127); break;
    case 4: digirads_mix = clampi(v, 0, 127); break;
    case 5: hy_comp = clampi(v, 0, 127); break;
    case 6: hy_rstep = clampi(v, 0, 7); break;
    case 7: digipump_pump = 127 - clampi(v, 0, 127); break;
    default: digipump_out = clampi(v, 0, 127); break;
    }
}

static const char *dg_pumpname(void)
{
    static const char *const n[3] = { "LEVEL", "GLUE", "PUMP" };
    int b = (digipump_pump * 3) >> 7;
    return n[b > 2 ? 2 : b];
}

/* the values as the cells show them (short); the drop-downs say them in full */
static void hy_cellval(char *b, int id)
{
    int v;
    b[0] = 0;
    switch (id) {
    case 1:                                         /* TABLE: OFF, 00..FF, 00...FF. */
        if (!hy_tbl) rs_cat(b, "OFF"); else rs_hex(b, hy_tbl - 1);
        return;
    case 2:                                         /* DRIVE: whole dB, signed */
        v = rd_gain_db10[digirads_gan];
        rs_signed(b, (v + (v < 0 ? -5 : 5)) / 10);
        rs_cat(b, "dB");
        return;
    case 3:                                         /* SRR: OFF, "800Hz", "4.8K", "24K" */
        v = rd_sr_hz10[digirads_srt];               /* Hz / 10 */
        if (digirads_srt >= 127) rs_cat(b, "OFF");
        else if (v < 100) { m_itoa(b, v * 10); rs_cat(b, "Hz"); }
        else if (v < 1000) { b[0] = (char)('0' + v / 100); b[1] = '.';
                             b[2] = (char)('0' + (v / 10) % 10); b[3] = 'K'; b[4] = 0; }
        else { m_itoa(b, (v + 50) / 100); rs_cat(b, "K"); }
        return;
    case 4:                                         /* BLEND: OFF, % */
        if (!digirads_mix) { rs_cat(b, "OFF"); return; }
        m_itoa(b, (digirads_mix * 100 + 63) / 127); rs_cat(b, "%");
        return;
    case 5:                                         /* COMP: OFF, 1..127 */
        if (!hy_comp) rs_cat(b, "OFF"); else m_itoa(b, hy_comp);
        return;
    case 6:                                         /* RATIO: AUT, 2:1 .. 20:1, LIM */
        rs_cat(b, rat_names[hy_rstep & 7]);
        return;
    case 7:
        m_itoa(b, 127 - digipump_pump);
        return;
    default:                                        /* OUT: whole dB, signed */
        v = pc_out_tenths(digipump_out);
        rs_signed(b, (v + (v < 0 ? -5 : 5)) / 10);
        rs_cat(b, "dB");
        return;
    }
}

static const char *const hy_lab[8] = { "TABLE", "DRIVE", "SRR", "BLEND", "COMP", "RATIO", "REL", "OUT" };

/* a cell: its name, its value and a bar (OUT's grows from the middle, 0 dB) */
static void hy_cell(int x, int y, int id)
{
    char b[8];
    int cx = x + 8, v = hy_get(id), vmax = id == 1 ? 512 : id == 6 ? 7 : 127;
    m_ctext(cx, y, hy_lab[id - 1]);
    hy_cellval(b, id);
    m_ctext(cx, y + 8, b);
    m_fill(x, y + 18, x + 16, y + 18, 1);          /* the track */
    if (id == 8) {
        int c = x + 8, e = x + 8 + ((v - 64) * 8) / 64;
        m_fill(c, y + 16, e, y + 18, 1);
        m_fill(c, y + 16, c, y + 19, 1);
    } else {
        m_fill(x, y + 16, x + (v * 16) / vmax, y + 18, 1);
    }
}

static void hy_cells(void)
{
    int x0 = 32, dx = 23, i;                    /* room between the five-letter names */
    for (i = 0; i < 4; i++) {
        hy_cell(x0 + i * dx, 13, i + 1);
        hy_cell(x0 + i * dx, 37, i + 5);
    }
}

/* the last 2.8 s of gain reduction, hanging from the top line; dotted lines at 6, 12 and
 * 18 dB; the compressor's settings under it */
static void m_graph(void)
{
    char b[8];
    int i, k, x;
    m_fill(1, 12, 112, 12, 1);
    for (k = 6; k <= 18; k += 6)
        for (x = 1; x < 112; x += 4)
            m_px(x, 12 + (k * 34) / 24, 1);
    for (i = 0; i < PC_HIST; i++) {
        int d = pc_hist[(pc_hpos + i) % PC_HIST];
        if (d > 24) d = 24;
        m_fill(1 + 2 * i, 12, 2 + 2 * i, 12 + (d * 34) / 24, 1);
    }
    x = 1;
    for (i = 5; i <= 7; i++) {
        m_text(x, 55, hy_lab[i - 1]); x += m_w(hy_lab[i - 1]) + 3;
        hy_cellval(b, i); m_text(x, 55, b); x += m_w(b) + 6;
    }
}

static void pc_page(void *bmp)
{
    char b[8];
    (void)bmp;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    if (!pc_still() && pc_graph == 1) {             /* YES: the curve, big */
        rs_screen(1, 12, 126, 54);
        hy_cellval(b, 1);
        m_text(2, 56, "GESTALT");
        m_rtext(126, 56, b);
        return;
    }
    if (!pc_still() && pc_graph == 2) {             /* YES again: the graph */
        m_graph();
        m_meter();
        return;
    }
    rs_screen(1, 13, 27, 50);
    m_text(0, 57, "GESTALT");               /* under the screen, below the COMP bar */
    hy_cells();
    if (!pc_still())                                /* no meter when still: nothing moves */
        m_meter();
}

/* FUNC + turn: to the next of the knob's snap points that way (on the knob's value) */
static const unsigned char hy_snap_comp[] = { 0, 32, 64, 80, 96, 127 };
static const unsigned char hy_snap_rat[] = { 0, 1, 2, 3, 4, 5, 6, 7 };         /* RATIO: every step */
static const unsigned char hy_snap_rel[] = { 0, 32, 64, 96, 127 };
static const unsigned char hy_snap_out[] = { 16, 40, 52, 58, 64, 96, 127 };     /* -24, -12, -6, -3, 0, +3, +6 dB */
static const unsigned char *const hy_snaps[9] = {
    0, 0, rd_snap_gain, rd_snap_srr, rd_snap_mix, hy_snap_comp, hy_snap_rat, hy_snap_rel, hy_snap_out,
};
static const unsigned char hy_nsnaps[9] = {
    0, 0, sizeof rd_snap_gain, sizeof rd_snap_srr, sizeof rd_snap_mix, sizeof hy_snap_comp,
    sizeof hy_snap_rat, sizeof hy_snap_rel, sizeof hy_snap_out,
};
static int hy_snap(int id, int v, int d)
{
    const unsigned char *p = hy_snaps[id];
    int i, n = hy_nsnaps[id];
    if (d > 0) {
        for (i = 0; i < n; i++)
            if (p[i] > v)
                return p[i];
    } else {
        for (i = n - 1; i >= 0; i--)
            if (p[i] < v)
                return p[i];
    }
    return v;
}


/* ---- core 4.1's master page ------------------------------------------------- */

/* draw(view, bmp), after the stock draw (the title bar). The screen's bitmap:
 * +4 width, +8 height, +12 longs a column, +16 the pixels: 128 columns of two
 * longs, bit 31 of a column's first long its row 0 (the bottom), the same
 * bytes the Digitone's frame buffer has, so the Digitone's drawing runs as
 * it is. Any other shape: the values in the stock's text. */
static void pumpdt_draw(void *view, void *bmp)
{
    char *b = (char *)bmp;
    pc_view = view;
    if (*(int32_t *)(b + 4) != 128 || *(int32_t *)(b + 8) != 64 ||
        *(int32_t *)(b + 12) != 2 || !*(unsigned char **)(b + 16)) {
        fw_fillrect(bmp, 0, 0, 127, DG_TOP - 1, 0);
        fw_textf(bmp, fw_font5, 2, 40, -1, "GESTALT TABLE %d DRIVE %d SRR %d BLEND %d",
                 hy_tbl, digirads_gan, digirads_srt, digirads_mix);
        fw_textf(bmp, fw_font5, 2, 24, -1, "COMP %d RATIO %s REL %d OUT %d",
                 hy_comp, rat_names[hy_rstep & 7], 127 - digipump_pump, digipump_out);
        return;
    }
    dg_fb = *(unsigned char **)(b + 16);
    pc_page(bmp);
}

static int pc_shown(void)
{
    void *v = core_mview;
    return v && core_mpage_shown(v) == &pumpdt_page;
}

/* ev_tick: RADS in or out of the chain, and a frame every 75 audio blocks (20 a second; still, 2)
 * while the page is on the screen */
void pumpdt_tick(void *ctrl)
{
    unsigned blk = digipump_blocks, d = blk - pc_blk_last;
    int up;
    pc_blk_last = blk;
    if (d > 3000)
        d = 3000;
#ifdef PC_TEST_PARAMS
    pumpdt_params(pc_fake);                 /* the emulator runs no render */
#endif
    digipump_owner_sync();
    if (core_mview)
        pc_drop_comp(core_mview);
    if (core_mview_draws != pc_draws_seen) {
        pc_draws_seen = core_mview_draws;
        pc_quiet = 0;
    } else if (pc_quiet < 1000) {
        pc_quiet++;
    }
    up = pc_shown() && pc_quiet <= 60;
    digipump_meter = up && !pc_still();     /* the output's level: only while it is shown */
    if (!up) {                              /* not up, or the view is closed */
        pc_due = 0;
        return;
    }
    pc_due += (int)d;
    if (pc_due < (pc_still() ? PC_FRAME_STILL : PC_FRAME))
        return;
    pc_due -= pc_still() ? PC_FRAME_STILL : PC_FRAME;
    if (pc_due > PC_FRAME)
        pc_due = 0;                         /* fell behind: no catching up */
    pc_meter_step();
    *((unsigned char *)ctrl + 0x20) = 1;    /* draw the screen again */
}

/* a value moved d detents: one a detent, or with the knob pressed, from
 * multiple of 8 to multiple of 8 (61 -> 64 -> 72; 0 .. 127) */
static unsigned char pc_push[8];            /* knob A..H pressed (keys 40..47) */
static int pc_move(int v, int d, int fast)
{
    if (fast)
        v = d > 0 ? ((v / PC_FAST) + d) * PC_FAST : (((v + PC_FAST - 1) / PC_FAST) + d) * PC_FAST;
    else
        v += d;
    return clampi(v, 0, 127);
}

/* one step a detent (16 counts on the Digitakt) */
static int pc_steps(int id, int d)
{
    int st;
    if ((d > 0 && pc_eacc[id] < 0) || (d < 0 && pc_eacc[id] > 0))
        pc_eacc[id] = 0;
    pc_eacc[id] += d;
    st = pc_eacc[id] / DT_PER_STEP;
    pc_eacc[id] -= st * DT_PER_STEP;
    return st;
}

/* The stock drop-down ("Name=value", as the stock pages show a knob's change):
 * the firmware's routine f(obj, fmt, ...) (DT_POPUP_ADDR), whose object is the
 * view's +108 (measured in digiemu on OS 1.53: the master view's is the one
 * the TRIG page passes) */
#define DT_VIEW_POPUP 108
static void pc_pop(void *view, const char *name, const char *val)
{
#ifdef DT_POPUP_ADDR
    uint32_t o = *(uint32_t *)((char *)view + DT_VIEW_POPUP);
    if (o >= 0x40000000u && o < 0x48000000u)
        ((void (*)(void *, const char *, ...))DT_POPUP_ADDR)((void *)o, "%s=%s", name, val);
#else
    (void)view; (void)name; (void)val;
#endif
}
static void pc_msg(void *view, const char *s)
{
#ifdef DT_POPUP_ADDR
    uint32_t o = *(uint32_t *)((char *)view + DT_VIEW_POPUP);
    if (o >= 0x40000000u && o < 0x48000000u)
        ((void (*)(void *, const char *, ...))DT_POPUP_ADDR)((void *)o, "%s", s);
#else
    (void)view; (void)s;
#endif
}

static void rs_num(char *b, int v) { while (*b) b++; m_itoa(b, v); }

/* the knobs' stock-style drop-downs */
static void hy_popup(void *view, int id)
{
    static const char *const nm[9] = { 0, "Table", "Drive", "Rate", "Blend", "Comp", "Ratio",
                                       "Release", "Out" };
    char b[16];
    int v, a;
    b[0] = 0;
    switch (id) {
    case 2:                                         /* Drive=+6dB, to a half */
        v = rd_gain_db10[digirads_gan]; a = v < 0 ? -v : v;
        rs_cat(b, v < 0 ? "-" : "+");
        rs_num(b, a / 10);
        if (a % 10) { rs_cat(b, "."); rs_num(b, a % 10); }
        rs_cat(b, "dB");
        break;
    case 3:                                         /* Rate=OFF, 800Hz, 4.8kHz */
        v = rd_sr_hz10[digirads_srt];
        if (digirads_srt >= 127) rs_cat(b, "OFF");
        else if (v < 100) { rs_num(b, v * 10); rs_cat(b, "Hz"); }
        else { rs_num(b, v / 100); rs_cat(b, "."); rs_num(b, (v / 10) % 10); rs_cat(b, "kHz"); }
        break;
    case 6:
        rs_cat(b, rat_long[hy_rstep & 7]);
        break;
    case 7:
        rs_cat(b, dg_pumpname()); rs_cat(b, " "); rs_num(b, 127 - digipump_pump);
        break;
    case 8:                                         /* Out=-3.0dB */
        v = pc_out_tenths(digipump_out); a = v < 0 ? -v : v;
        rs_cat(b, v < 0 ? "-" : "+"); rs_num(b, a / 10); rs_cat(b, "."); rs_num(b, a % 10);
        rs_cat(b, "dB");
        break;
    default:
        hy_cellval(b, id);                          /* Table=0A., Blend=50%, Comp=80 */
        break;
    }
    pc_pop(view, nm[id], b);
}

/* enc(view, event): +12 knob 1-8 = A-H (9 LEVEL), +16 the turn */
static int32_t pumpdt_enc(void *view, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12);
    int delta = *(int32_t *)((char *)ev + 16);
    int d, v, fast;
    if (id < 1 || id > 8)
        return 0;                               /* LEVEL: the stock's */
    d = pc_steps(id, delta);
    if (!d)
        return 1;
    if (!gs_seen)
        return 1;                               /* the render has not handed the settings over */
    fast = !pc_func && pc_push[id - 1];         /* press + turn: big steps */
    v = hy_get(id);
    if (id == 1) {                              /* TABLE: a table a detent; press + turn or FUNC +
                                                 * turn 16 (a row of the chart: 0A -> 1A); OFF first */
        if (pc_func || fast)
            v = (v == 0 && d > 0) ? 1 + (d - 1) * 16 : v + d * 16;
        else
            v += d;
    } else if (id == 6) {                       /* RATIO: its eight steps */
        v = clampi(v + d, 0, 7);
    } else if (pc_func) {                       /* FUNC + turn: the next snap point */
        v = hy_snap(id, v, d > 0 ? 1 : -1);
    } else {
        v = pc_move(v, d, fast);
    }
    hy_set(id, v);
    gs_commit(view);
    hy_popup(view, id);
    fw_invalidate(view);
    return 1;
}

/* Copy, paste, clear: FUNC + REC, STOP or PLAY on our page (and core's CLEAR, FUNC + held LFO +
 * PLAY), with a drop-down saying what was done */
static int pc_clip[GS_N], pc_clip_have;
static int *const gs_vals[GS_N] = { &hy_tbl, &digirads_gan, &digirads_srt, &digirads_mix,
                                    &hy_comp, &hy_rstep, &digipump_pump, &digipump_out };

static void pumpdt_clear(void *view)
{
    gs_defaults();
    gs_commit(view);
    fw_invalidate(view);
}

static int pc_copypaste(void *view, int key)
{
    int i;
    if (key == DT_K_REC) {
        for (i = 0; i < GS_N; i++)
            pc_clip[i] = *gs_vals[i];
        pc_clip_have = 1;
        pc_msg(view, "GESTALT settings copied");
        return 1;
    }
    if (key == DT_K_STOP) {
        if (!pc_clip_have) {
            pc_msg(view, "Nothing to paste");
            return 1;
        }
        for (i = 0; i < GS_N; i++)
            *gs_vals[i] = pc_clip[i];
        gs_commit(view);
        pc_msg(view, "GESTALT settings pasted");
    } else {
        gs_defaults();
        gs_commit(view);
        pc_msg(view, "GESTALT settings cleared");
    }
    fw_invalidate(view);
    return 1;
}

/* ev_key, every key on every screen, never taken: FUNC and the knob presses
 * (40..47 = A..H) down or up (a release comes with flags 0 or 0x10) */
int pumpdt_evkey(void *brain, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12), down = (*(int32_t *)((char *)ev + 16) & 1) != 0;
    (void)brain;
    if (id == DT_K_FUNC)
        pc_func = down;
    else if (id >= DT_K_PUSH_A && id < DT_K_PUSH_A + 8)
        pc_push[id - DT_K_PUSH_A] = (unsigned char)down;
    return 0;
}

/* key(view, event), while the page is up: YES cycles the views; FUNC + REC / STOP / PLAY copy,
 * paste, clear. Everything else is the stock's. */
static int32_t pumpdt_key(void *view, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12);
    int flags = *(int32_t *)((char *)ev + 16);
    int press = (flags & 1) && !(flags & 0x10) && !(flags & 8);
    if ((id == DT_K_REC || id == DT_K_STOP || id == DT_K_PLAY) && pc_func) {
        if (press && gs_seen)
            return pc_copypaste(view, id);
        return 1;                               /* its release and repeats: ours too */
    }
    if (id != DT_K_YES || (flags & 2) || pc_func)
        return 0;                               /* FUNC + YES: the stock's (TEMP SAVE) */
    if (press) {
        if (pc_still()) {
            pc_msg(view, "Animation off: SETTINGS");
        } else {
            static const char *const vn[3] = { "CONTROLS", "TABLE", "GRAPH" };
            pc_graph = (pc_graph + 1) % 3;
            pc_pop(view, "View", vn[pc_graph]);
            pc_due = 0;
            fw_invalidate(view);
        }
    }
    return 1;
}

/* in the stock compressor's place: after it, and pc_drop_comp takes it out */
struct core_mpage pumpdt_page = {
    11, "GESTALT", "Gestalt", pumpdt_draw, pumpdt_key, pumpdt_enc, 0, 0, pumpdt_clear
};

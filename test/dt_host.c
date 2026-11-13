/* Host test for GESTALT for Digitakt's chain (rads.c, packed tables, into pump.c), block by block,
 * as core 4.1's ev_render_master calls it (in place), with the UI tick between blocks. Prints levels and checks for clipping, integer overflow (with -fsanitize) and
 * clicks across settings changes. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../src/rads.c"
#include "../src/pump.c"
void pumpdt_params(const int16_t *p) { (void)p; }   /* the page's, not tested here */

#define FS 48000
static double rms(const double *x, int n) { double s = 0; for (int i = 0; i < n; i++) s += x[i] * x[i]; return sqrt(s / n); }
static double db(double v) { return 20 * log10(v > 1e-12 ? v : 1e-12); }

/* a 110 Hz FM-ish tone at -14 dBFS, and a kick every 500 ms at -8 dBFS peak */
static double in_gain = 1;
static double src(int n, int ch)
{
    double t = (double)n / FS, kt = fmod(t, 0.5);
    double tone = 0.2 * sin(2 * M_PI * 110 * t + 1.5 * sin(2 * M_PI * 220 * t));
    double kick = 0.4 * exp(-kt * 18) * sin(2 * M_PI * (45 + 120 * exp(-kt * 30)) * kt);
    return in_gain * (tone * (ch ? 0.9 : 1.0) + kick);
}

static int n_all;
static void run(const char *name, double secs, void (*setp)(int blk))
{
    int nb = (int)(secs * FS / 32), n = 0, over = 0, onb = 0;
    double *xi = malloc(sizeof(double) * nb * 32), *xo = malloc(sizeof(double) * nb * 32), last = 0, mj = 0;
    for (int b = 0; b < nb; b++) {
        int32_t in[64], out[64];
        if (setp) setp(b);
        digipump_meter = 1;
        digipump_owner_sync();
        for (int i = 0; i < 32; i++)
            for (int c = 0; c < 2; c++)
                in[2 * i + c] = (int32_t)lrint(src(n_all + n + i, c) * 2147483647.0 * 0.999);
        memcpy(out, in, sizeof out);
        if (digipump_running()) onb++;
        digipump_render(out);
        for (int i = 0; i < 32; i++) {
            double o = out[2 * i] / 2147483648.0;
            if (fabs(o) > 0.95) over++;
            xi[n + i] = in[2 * i] / 2147483648.0;
            xo[n + i] = o;
            if (n + i > 0 && fabs(o - last) > mj) mj = fabs(o - last);
            last = o;
        }
        n += 32;
    }
    n_all += n;
    int skip = n / 3;
    if (in_gain == 0) {                         /* silence in: the last second out, its largest */
        double mx = 0;
        for (int i = n - 48000; i < n; i++) if (fabs(xo[i]) > mx) mx = fabs(xo[i]);
        printf("  silence: the last second's largest output %.2e (%s)\n", mx, mx < 1e-6 ? "OK" : "NOT SILENT");
    }
    printf("%-36s in %6.1f dB  out %6.1f dB  >-0.45dBFS %5d  max step %.3f  blocks on %5d/%d  gr %3d\n",
           name, db(rms(xi + skip, n - skip)), db(rms(xo + skip, n - skip)), over, mj, onb, nb, digipump_gr);
    free(xi); free(xo);
}

static void set(int tbl, int gan, int srt, int mix, int comp, int rat, int out)
{
    digirads_active = tbl > 0; if (tbl) digirads_tbl = tbl - 1;
    digirads_gan = gan; digirads_srt = srt; digirads_mix = mix;
    digipump_on = comp > 0; if (comp) digipump_squash = comp;
    digipump_rat = rat; digipump_out = out;
}
static void p_off(int b) { (void)b; set(0, 24, 127, 127, 0, 0, 64); }
static void p_comp(int b) { (void)b; set(0, 24, 127, 127, 80, 0, 64); }
static void p_rads(int b) { (void)b; set(0x23, 48, 127, 127, 0, 0, 64); }
static void p_both(int b) { (void)b; set(0x23, 48, 60, 127, 80, 39, 64); }
static void p_crush(int b) { (void)b; set(0x141, 127, 30, 127, 127, 127, 127); }
static void p_outonly(int b) { (void)b; set(0, 24, 127, 127, 0, 0, b & 64 ? 16 : 127); }
static void p_toggle(int b) { (void)b; set((b / 200) & 1 ? 0x23 : 0, 48, 127, 95, (b / 300) & 1 ? 80 : 0, 0, 64); }
static void p_tables(int b) { set(1 + (b / 20) % 512, 60, 127, 127, 80, 0, 64); }

int main(void)
{
    run("all off (no insert)", 1.0, p_off);
    run("COMP 80", 2.0, p_comp);
    run("TABLE 22 DRIVE +12 (COMP OFF)", 2.0, p_rads);
    run("TABLE 22 + COMP 80 4:1 SRR", 2.0, p_both);
    run("worst: max drive, LIMIT, OUT +6", 2.0, p_crush);
    run("OUT only, jumping -24 / +6 dB", 2.0, p_outonly);
    run("toggling TABLE and COMP", 4.0, p_toggle);
    run("sweeping all tables", 6.0, p_tables);
    run("TABLE 22 + COMP 80 4:1 SRR, again", 1.0, p_both);
    in_gain = 0;
    run("then silence: must settle to 0", 3.0, p_both);
    in_gain = 1;
    run("all off again (fades out)", 1.0, p_off);
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* The M1's delay on the whole output (Owner's Manual p. 42, 50-53): the program's effect applies to the whole sound, so it sits here after
 * the engine's mix instead of as a bus per voice. The parameters come from the zone of the most recently started note (fm1b_program_fx):
 * time left and right 0..500 ms, feedback -99..99 % (crossed: left is fed from right and vice versa, Cross Delay), high damp
 * 0..99 % (a one-pole low-pass in the feedback) and the effect's share 0..100 (DRY:EFF: dry × (100 - share) + delay × share).
 * The delay line runs at half rate (22 050 Hz: average of two frames in, linear interpolation out), 16 bits per channel, 500 ms:
 * 43 KB in fm1-base's pool, which cannot hold 86 KB beside the cache. Everything is 32-bit arithmetic. */
#include "m1_fx.h"
#include "m1_cfg.h"
#include "fm1b.h"
#include "sf2fx.h"

enum { RATE = 22050, MAX_FRAMES = RATE / 2 + 1 };
static int16_t line_l[MAX_FRAMES] __attribute__((aligned(4), section(".pool")));
static int16_t line_r[MAX_FRAMES] __attribute__((aligned(4), section(".pool")));
static uint32_t pos, dl, dr;                     /* write position, delay in frames */
static int32_t fb_q14, keep_q14, wet_q14, lp_l, lp_r, crossed, active;
static int32_t half, acc_l, acc_r, prev_l, prev_r, cur_l, cur_r;   /* half rate */

static int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }

static void clear(void)
{
    uint32_t i;
    for (i = 0; i < MAX_FRAMES; i++) line_l[i] = line_r[i] = 0;
    lp_l = lp_r = 0;
    half = acc_l = acc_r = prev_l = prev_r = cur_l = cur_r = 0;
}

/* The program's chorus (tools/m1_sf2.py chorus_fx): rate, depth 0..15 and delay; the sweep is the depth × 67.5 % of the delay (previously 90 %, too strong compared with the original), so that
 * full depth at 5 ms (E.Piano 3) sweeps 0.5..9.5 ms. Without values: fm1-sf2's fixed chorus. */
static int16_t chorus_now[2] = {-1, -1};
static void chorus_setup(const int16_t *p)
{
    unsigned rate, depth, delay_us;
    if (p[4] == chorus_now[0] && p[5] == chorus_now[1])
        return;
    chorus_now[0] = p[4]; chorus_now[1] = p[5];
    rate = (unsigned)p[4] & 4095u; depth = ((unsigned)p[4] >> 12) & 15u; delay_us = (unsigned)p[5] * 100u;
    if (!p[5]) { sf2fx_chorus_shape(0, 0, 0); return; }
    sf2fx_chorus_shape(delay_us, delay_us * depth * 27u / 400u, rate * 10u);   /* depth 15 = 67.5 % of the delay (90 % × 0.75: the swing close to the original's median) */
}

static void setup(void)
{
    int16_t p[7];
    int32_t share, damp;
    unsigned given = fm1b_program_fx(p);
    if (given)
        chorus_setup(p);
    if (!given || !(p[3] & 127) || (p[0] <= 0 && p[1] <= 0)) {
        active = 0;
        return;
    }
    share = p[3] & 127; share = share > 100 ? 100 : share;
    damp = (p[3] >> 7) & 127; damp = damp > 99 ? 99 : damp;
    dl = (uint32_t)(p[0] < 0 ? 0 : p[0] > 500 ? 500 : p[0]) * RATE / 1000u;
    dr = (uint32_t)(p[1] < 0 ? 0 : p[1] > 500 ? 500 : p[1]) * RATE / 1000u;
    if (dl >= MAX_FRAMES) dl = MAX_FRAMES - 1u;
    if (dr >= MAX_FRAMES) dr = MAX_FRAMES - 1u;
    fb_q14 = (int32_t)p[2] * 16384 / 100;
    keep_q14 = 16384 - damp * 14746 / 99;       /* high damp 99: the low-pass takes 10 % of the step per frame */
    wet_q14 = share * 16384 / 100;
    crossed = (p[3] >> 14) & 1;
    if (!active) { clear(); active = 1; }
}

void m1_fx_render(int32_t *lr, uint32_t n)
{
    uint32_t i;
    setup();
    if (!active)
        return;
    for (i = 0; i < n; i++) {
        int32_t l = lr[2 * i], r = lr[2 * i + 1], yl, yr;
        acc_l += sat16(l >> 8); acc_r += sat16(r >> 8);                 /* Q23 -> 16 bits for the delay */
        if (half) {                                                     /* every other frame: one step in the delay line */
            uint32_t rl = pos >= dl ? pos - dl : pos + MAX_FRAMES - dl, rr = pos >= dr ? pos - dr : pos + MAX_FRAMES - dr;
            int32_t xl = acc_l >> 1, xr = acc_r >> 1;
            int32_t ol = dl ? line_l[rl] : xl, orr = dr ? line_r[rr] : xr;   /* 0 ms: straight through */
            int32_t fl = crossed ? orr : ol, fr = crossed ? ol : orr;
            lp_l += ((fl - lp_l) * keep_q14) >> 14;                     /* |difference| < 2^17, times < 2^14 */
            lp_r += ((fr - lp_r) * keep_q14) >> 14;
            line_l[pos] = (int16_t)sat16(xl + ((lp_l * fb_q14) >> 14));
            line_r[pos] = (int16_t)sat16(xr + ((lp_r * fb_q14) >> 14));
            if (++pos >= MAX_FRAMES) pos = 0;
            prev_l = cur_l; prev_r = cur_r; cur_l = ol; cur_r = orr;
            acc_l = acc_r = 0;
            yl = (prev_l + cur_l) >> 1; yr = (prev_r + cur_r) >> 1;     /* midway between two steps */
        } else {
            yl = cur_l; yr = cur_r;
        }
        half ^= 1;
        /* dry × (1 - share) + delay × share; the dry part at full resolution */
        lr[2 * i] = l - (((l >> 8) * wet_q14) >> 14) * 256 + ((yl * wet_q14) >> 14) * 256;
        lr[2 * i + 1] = r - (((r >> 8) * wet_q14) >> 14) * 256 + ((yr * wet_q14) >> 14) * 256;
    }
}

/* The final limiter on the stereo sum: a lookahead limiter with a smooth gain curve. The previous one cut the gain on a single sample at the first peak,
 * and a stepwise gain change in the middle of a period is clipping (the crackle in the attack, mostly with pianos and mallets). Now the
 * sound is delayed one block (128 frames, 2.9 ms); the gain moves linearly across the block from where the previous block ended to the block
 * target that both it and the next block can bear (the ceiling / the block's peak), so the peak never meets a higher gain than the target. Upwards
 * (release) the gain may rise at most 16/2048 per block (about 0.2 s for 6 dB). Ceiling -1 dB of Q23. 32-bit arithmetic: x >> 6 times
 * gain in Q11. The same code before and after the delay (the echoes can sum over the ceiling). */
enum { LIM_UNITY = 2048, LIM_CEIL6 = 7471104 >> 6, LIM_RISE = 16, LIM_MAX = 256 };
struct limiter {
    int32_t buf[2 * LIM_MAX];                    /* the block that is waiting (delayed one block) */
    int32_t g, t;                                /* the gain at the end of the previous output block; the waiting block's target, Q11 */
    uint32_t n, cut;
};
static struct limiter lim_pre, lim_post;

static int32_t lim_target(const int32_t *x, uint32_t n)
{
    uint32_t i;
    int32_t peak = 1, t;
    for (i = 0; i < 2 * n; i++) {
        int32_t v = x[i] >> 6;
        v = v < 0 ? -v : v;
        if (v > peak) peak = v;
    }
    t = peak > LIM_CEIL6 ? (LIM_CEIL6 * LIM_UNITY) / peak : LIM_UNITY;
    return t < 16 ? 16 : t;
}

static void limit(struct limiter *L, int32_t *lr, uint32_t n)
{
    uint32_t i;
    int32_t t_new, g0, g1;
    if (n > LIM_MAX)
        return;
    if (L->n != n) {                              /* first call: silent delay, gain 1 */
        for (i = 0; i < 2 * LIM_MAX; i++) L->buf[i] = 0;
        L->n = n; L->g = LIM_UNITY; L->t = LIM_UNITY;
    }
    t_new = lim_target(lr, n);
    g0 = L->g;
    g1 = L->t < t_new ? L->t : t_new;             /* a gain that both the waiting block and the next can bear */
    if (g1 > g0 + LIM_RISE) g1 = g0 + LIM_RISE;   /* release: slow rise */
    if (g0 > L->t) g0 = L->t;                     /* (cannot happen: previous g1 <= this target) */
    if (g0 == LIM_UNITY && g1 == LIM_UNITY) {
        for (i = 0; i < 2 * n; i++) { int32_t o = L->buf[i]; L->buf[i] = lr[i]; lr[i] = o; }
    } else {
        int32_t gq = g0 << 16, step = ((g1 - g0) * 65536) / (int32_t)n;      /* the gain as Q27, one step per frame */
        for (i = 0; i < n; i++) {
            int32_t g = gq >> 16;
            int32_t l = L->buf[2 * i] >> 6, r = L->buf[2 * i + 1] >> 6;
            L->buf[2 * i] = lr[2 * i]; L->buf[2 * i + 1] = lr[2 * i + 1];
            lr[2 * i] = (l * g) >> 11 << 6;
            lr[2 * i + 1] = (r * g) >> 11 << 6;
            gq += step;
        }
        L->cut += n;
    }
    L->g = g1; L->t = t_new;
}
void m1_limit_render(int32_t *lr, uint32_t n) { limit(&lim_pre, lr, n); }     /* before the delay */
void m1_limit_post(int32_t *lr, uint32_t n) { limit(&lim_post, lr, n); }      /* after the delay: the echoes sum over the ceiling */
uint32_t m1_limit_cut(void) { return lim_pre.cut + lim_post.cut; }

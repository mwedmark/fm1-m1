/* SPDX-License-Identifier: GPL-3.0-only */
/* M1:s delay på hela utgången (Owner's Manual s. 42, 50-53): programmets effekt gäller hela ljudet, så den ligger här efter
 * motorns mix i stället för som en buss per röst. Parametrarna kommer från den senast startade tonens zon (fm1b_program_fx):
 * tid vänster och höger 0..500 ms, feedback -99..99 % (korsad: vänster matas från höger och tvärtom, Cross Delay), high damp
 * 0..99 % (ett enpoligt lågpass i återkopplingen) och effektens andel 0..100 (DRY:EFF: torrt × (100 - andel) + delay × andel).
 * Delaylinjen går i halv takt (22 050 Hz: medel av två bildrutor in, linjär interpolation ut), 16 bitar per kanal, 500 ms:
 * 43 KB i fm1-base:s pool, som inte rymmer 86 KB bredvid cachen. Allt är 32-bitarsaritmetik. */
#include "m1_fx.h"
#include "m1_cfg.h"
#include "fm1b.h"
#include "sf2fx.h"

enum { RATE = 22050, MAX_FRAMES = RATE / 2 + 1 };
static int16_t line_l[MAX_FRAMES] __attribute__((aligned(4), section(".pool")));
static int16_t line_r[MAX_FRAMES] __attribute__((aligned(4), section(".pool")));
static uint32_t pos, dl, dr;                     /* skrivposition, fördröjning i bildrutor */
static int32_t fb_q14, keep_q14, wet_q14, lp_l, lp_r, crossed, active;
static int32_t half, acc_l, acc_r, prev_l, prev_r, cur_l, cur_r;   /* halva takten */

static int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }

static void clear(void)
{
    uint32_t i;
    for (i = 0; i < MAX_FRAMES; i++) line_l[i] = line_r[i] = 0;
    lp_l = lp_r = 0;
    half = acc_l = acc_r = prev_l = prev_r = cur_l = cur_r = 0;
}

/* Programmets chorus (tools/m1_sf2.py chorus_fx): hastighet, djup 0..15 och delay; svepet är djupet × 67,5 % av delayen (förut 90 %, för kraftigt mot originalet), så att
 * fullt djup vid 5 ms (E.Piano 3) sveper 0,5..9,5 ms. Utan värden: fm1-sf2:s fasta chorus. */
static int16_t chorus_now[2] = {-1, -1};
static void chorus_setup(const int16_t *p)
{
    unsigned rate, depth, delay_us;
    if (p[4] == chorus_now[0] && p[5] == chorus_now[1])
        return;
    chorus_now[0] = p[4]; chorus_now[1] = p[5];
    rate = (unsigned)p[4] & 4095u; depth = ((unsigned)p[4] >> 12) & 15u; delay_us = (unsigned)p[5] * 100u;
    if (!p[5]) { sf2fx_chorus_shape(0, 0, 0); return; }
    sf2fx_chorus_shape(delay_us, delay_us * depth * 27u / 400u, rate * 10u);   /* djup 15 = 67,5 % av delayen (90 % × 0,75: svängningen nära originalets median) */
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
    keep_q14 = 16384 - damp * 14746 / 99;       /* high damp 99: lågpasset tar 10 % av steget per bildruta */
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
        acc_l += sat16(l >> 8); acc_r += sat16(r >> 8);                 /* Q23 -> 16 bitar för delayen */
        if (half) {                                                     /* var annan bildruta: ett steg i delaylinjen */
            uint32_t rl = pos >= dl ? pos - dl : pos + MAX_FRAMES - dl, rr = pos >= dr ? pos - dr : pos + MAX_FRAMES - dr;
            int32_t xl = acc_l >> 1, xr = acc_r >> 1;
            int32_t ol = dl ? line_l[rl] : xl, orr = dr ? line_r[rr] : xr;   /* 0 ms: rakt igenom */
            int32_t fl = crossed ? orr : ol, fr = crossed ? ol : orr;
            lp_l += ((fl - lp_l) * keep_q14) >> 14;                     /* |skillnad| < 2^17, gånger < 2^14 */
            lp_r += ((fr - lp_r) * keep_q14) >> 14;
            line_l[pos] = (int16_t)sat16(xl + ((lp_l * fb_q14) >> 14));
            line_r[pos] = (int16_t)sat16(xr + ((lp_r * fb_q14) >> 14));
            if (++pos >= MAX_FRAMES) pos = 0;
            prev_l = cur_l; prev_r = cur_r; cur_l = ol; cur_r = orr;
            acc_l = acc_r = 0;
            yl = (prev_l + cur_l) >> 1; yr = (prev_r + cur_r) >> 1;     /* mitt emellan två steg */
        } else {
            yl = cur_l; yr = cur_r;
        }
        half ^= 1;
        /* torrt × (1 - andel) + delay × andel; den torra delen i full upplösning */
        lr[2 * i] = l - (((l >> 8) * wet_q14) >> 14) * 256 + ((yl * wet_q14) >> 14) * 256;
        lr[2 * i + 1] = r - (((r >> 8) * wet_q14) >> 14) * 256 + ((yr * wet_q14) >> 14) * 256;
    }
}

/* Slutlimitern på stereosumman: en lookahead-limiter med mjuk gainkurva. Den förra sänkte gainet på ett enda prov vid första toppen,
 * och en sådan stegvis gainändring mitt i en period är klippning (knastret i anslaget, mest hos pianon och mallets). Nu fördröjs
 * ljudet ett block (128 bildrutor, 2,9 ms); gainet går linjärt över blocket från där förra blocket slutade till det block-
 * mål som både det och nästa block tål (taket / blockets topp), så toppen aldrig möter ett högre gain än målet. Uppåt
 * (släpp) får gainet stiga högst 16/2048 per block (cirka 0,2 s för 6 dB). Tak -1 dB av Q23. 32-bitarsaritmetik: x >> 6 gånger
 * gain i Q11. Samma kod före och efter delayen (ekona kan summera över taket). */
enum { LIM_UNITY = 2048, LIM_CEIL6 = 7471104 >> 6, LIM_RISE = 16, LIM_MAX = 256 };
struct limiter {
    int32_t buf[2 * LIM_MAX];                    /* blocket som väntar (fördröjt ett block) */
    int32_t g, t;                                /* gainet i slutet av förra utblocket; väntande blockets mål, Q11 */
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
    if (L->n != n) {                              /* första anropet: tyst fördröjning, gain 1 */
        for (i = 0; i < 2 * LIM_MAX; i++) L->buf[i] = 0;
        L->n = n; L->g = LIM_UNITY; L->t = LIM_UNITY;
    }
    t_new = lim_target(lr, n);
    g0 = L->g;
    g1 = L->t < t_new ? L->t : t_new;             /* ett gain som både det väntande och nästa block tål */
    if (g1 > g0 + LIM_RISE) g1 = g0 + LIM_RISE;   /* släpp: långsam stigning */
    if (g0 > L->t) g0 = L->t;                     /* (kan inte hända: förra g1 <= detta mål) */
    if (g0 == LIM_UNITY && g1 == LIM_UNITY) {
        for (i = 0; i < 2 * n; i++) { int32_t o = L->buf[i]; L->buf[i] = lr[i]; lr[i] = o; }
    } else {
        int32_t gq = g0 << 16, step = ((g1 - g0) * 65536) / (int32_t)n;      /* gainet som Q27, ett steg per bildruta */
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
void m1_limit_render(int32_t *lr, uint32_t n) { limit(&lim_pre, lr, n); }     /* före delayen */
void m1_limit_post(int32_t *lr, uint32_t n) { limit(&lim_post, lr, n); }      /* efter delayen: ekona summerar över taket */
uint32_t m1_limit_cut(void) { return lim_pre.cut + lim_post.cut; }

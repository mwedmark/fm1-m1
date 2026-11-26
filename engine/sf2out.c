/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "sf2out.h"
#include "fm1b.h"
#include "sf2fx.h"
#include "engine.h"
#include <stddef.h>

/* The limiter: the dry mix gets LIMIT_MAKEUP (the zone levels left the mix about 6 dB under where it was) and its peaks are held
 * at LIMIT_CEILING, under full scale so the effects and the pan have room. The gain drops at once on a peak and comes back
 * with a time constant of 2^LIMIT_RELEASE frames (128 ms at 32 kHz). It is one gain for the whole mix, so it scales the
 * buses and the pan's sides alike: the voices keep their balance. */
enum { LIMIT_MAKEUP = 2, LIMIT_CEILING = 33554431 /* fm1-m1: +12 dB, 5938679 (-3 dB) before: an instant gain drop at the first peak is clipping; app.c's lookahead limiter holds the ceiling */, LIMIT_RELEASE = 12 };
#define LIMIT_UNITY ((int32_t)LIMIT_MAKEUP << 16)
static int32_t limit_gain = LIMIT_UNITY;              /* Q16 */
static unsigned limit_frames;                         /* frames held under the makeup gain, since start (for the tests) */
static inline __attribute__((always_inline)) int32_t scale(int32_t x, int32_t g) { return (int32_t)(((int64_t)x * g) >> 16); }
static FM1_DSP_CODE void limit(int32_t *dry, int32_t *rev, int32_t *cho, int32_t *left, int32_t *right, unsigned buses, unsigned n)
{
    int32_t g = limit_gain;
    for (unsigned i = 0; i < n; ++i) {
        g += ((LIMIT_UNITY - g) >> LIMIT_RELEASE) + (g < LIMIT_UNITY);   /* + 1: the last 2^RELEASE steps too */
        int32_t x = dry[i], a = x < 0 ? -x : x;
        if ((int64_t)a * g > (int64_t)LIMIT_CEILING << 16)   /* ceiling / a, in Q16 within 32 bits: a is over 2^21 here */
            g = (int32_t)(((uint32_t)LIMIT_CEILING << 8) / ((uint32_t)a >> 8));
        if (g < LIMIT_UNITY - 64) ++limit_frames;
        dry[i] = scale(x, g);
        if (buses & 1U) rev[i] = scale(rev[i], g);
        if (buses & 2U) cho[i] = scale(cho[i], g);
        if (buses & 4U) left[i] = scale(left[i], g);
        if (buses & 8U) right[i] = scale(right[i], g);
    }
    limit_gain = g;
}
unsigned sf2out_limited(void) { return limit_frames; }

#if FM1B_RATE == 44100U
static int32_t melodic[FM1_BLOCK_FRAMES], reverb_bus[FM1_BLOCK_FRAMES], chorus_bus[FM1_BLOCK_FRAMES];
static int32_t cut_left[FM1_BLOCK_FRAMES], cut_right[FM1_BLOCK_FRAMES];
FM1_DSP_CODE void sf2out_render(int32_t *out)
{
    uint8_t sends[2];
    int32_t *sides[2] = {cut_left, cut_right};
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) melodic[i] = 0;
    unsigned buses = fm1b_render_sends(NULL, melodic, reverb_bus, chorus_bus, sides, FM1_BLOCK_FRAMES, sends);
    limit(melodic, reverb_bus, chorus_bus, cut_left, cut_right, buses, FM1_BLOCK_FRAMES);
    /* dry on both channels less what the panned voices take from each, plus reverb and chorus: from the dry mix while all
     * channels send alike, else from the buses */
    sf2fx_mix_sends(melodic, buses & 4U ? cut_left : NULL, buses & 8U ? cut_right : NULL,
                    buses & 1U ? reverb_bus : melodic, buses & 1U ? 127U : sends[0],
                    buses & 2U ? chorus_bus : melodic, buses & 2U ? 127U : sends[1], out, FM1_BLOCK_FRAMES);
}
#else
/* The player renders at FM1B_RATE and is resampled to 44.1 kHz here: at 32 kHz a block of 128 output frames takes 92 or 93
 * new player frames. They are kept as 16-bit samples, one array a channel, after the last RESAMPLE_HISTORY frames before
 * them; position is where output frame 0 of the next block sits in those arrays, in frames, Q24. While the player and the
 * history are silent the filter is skipped. The filter is resample_table.inc (make_resampler.js). */
#include "resample_table.inc"
enum { RESAMPLE_HISTORY = RESAMPLE_TAPS, PLAYER_FRAMES = FM1_BLOCK_FRAMES * FM1B_RATE / 44100U + 3U };
#define RESAMPLE_STEP ((uint32_t)((((uint64_t)FM1B_RATE << 24) + 22050U) / 44100U))
static int32_t melodic[PLAYER_FRAMES], reverb_bus[PLAYER_FRAMES], chorus_bus[PLAYER_FRAMES], stereo[PLAYER_FRAMES * 2];
static int32_t cut_left[PLAYER_FRAMES], cut_right[PLAYER_FRAMES];
static int16_t left_in[RESAMPLE_HISTORY + PLAYER_FRAMES], right_in[RESAMPLE_HISTORY + PLAYER_FRAMES];
static uint32_t position = (uint32_t)(RESAMPLE_TAPS / 2 - 1) << 24;
static unsigned silent = RESAMPLE_HISTORY;               /* frames of silence at the end of the arrays (counted up to the history) */
static inline __attribute__((always_inline)) int32_t sat24(int32_t v) { return v > 33554431 ? 33554431 : v < -33554432 ? -33554432 : v; }   /* fm1-m1: 12 dB over full scale, see sf2fx.c */

/* fm1-m1: a lookahead limiter on the stereo mix at the player's rate, in front of the 16-bit conversion. The mix may be up to 12 dB over
 * full scale here (sat24 at 2^25: chorus, reverb and the dry sound add up past it), and the cast to int16 wrapped what went over: a
 * chord at full level turned to distortion (E.Piano 3: C-E-G at velocity 127). Each sample waits LOOK frames (2 ms); its output gain g is
 * the one a straight line from the last gain reaches under the ceiling for every sample in the window (the step to each of them is
 * (t - g) / (frames to it), the smallest wins), and when nothing binds it rises by RELEASE a frame (about 0.17 s for 6 dB). The gain
 * falls before a peak arrives and never steps, so there is no click and no wrap. Q15 throughout: |x| < 2^17, gains up to 2^15, and a
 * product is a result under the ceiling times 2^15, within 31 bits. */
enum { LOOK = 64, LOOK_MASK = 63, LOOK_CEIL = 29184 /* -1 dB of full scale, Q15 */, LOOK_UNITY = 32768, LOOK_RELEASE = 1 };
static int32_t look_l[LOOK], look_r[LOOK], look_t[LOOK];       /* entry k is at k & LOOK_MASK; the window is the last LOOK entries */
static uint32_t look_pn;                                       /* the index of the next entry */
static unsigned look_pending;                                  /* entries in the window that need a gain under 1 */
static int32_t look_g = LOOK_UNITY, look_recip[LOOK + 1];      /* look_recip[m] = 2^15 / m, made at the first call */
static uint32_t look_bk;                                       /* the binding entry: the one whose line from g is the steepest down */
static int32_t look_bt;
static int look_bv;
/* The step that takes g to t in m frames, Q15, floor (never over). While g follows the binding entry's line every other entry's
 * step is at least as large, and stays so; the window is scanned only when the binding entry has left it. */
static inline int32_t look_need(int32_t t, int32_t g, unsigned m) { return ((t - g) * look_recip[m]) >> 15; }
static FM1_DSP_CODE void look_limit(int32_t xl, int32_t xr, int32_t *ol, int32_t *orr)
{
    int32_t a = xl < 0 ? -xl : xl, b = xr < 0 ? -xr : xr, t = LOOK_UNITY, g = look_g, step = LOOK_RELEASE;
    if (!look_recip[1]) {
        for (int32_t m = 1; m <= LOOK; ++m) look_recip[m] = 32768 / m;
        for (unsigned k = 0; k < LOOK; ++k) look_t[k] = LOOK_UNITY;       /* an empty window needs no gain change */
    }
    if (b > a) a = b;
    if (a > LOOK_CEIL)
        t = (int32_t)(((uint32_t)LOOK_CEIL << 15) / (uint32_t)a);        /* the gain that takes this sample to the ceiling, Q15 */
    const uint32_t pn = look_pn++, o = pn - LOOK;                       /* o: the entry that leaves the window and is output now */
    const unsigned slot = pn & LOOK_MASK;
    const int32_t out_l = look_l[slot], out_r = look_r[slot];
    if (look_t[slot] < LOOK_UNITY) --look_pending;
    look_l[slot] = xl; look_r[slot] = xr; look_t[slot] = t;
    if (t < LOOK_UNITY) ++look_pending;
    if (look_pending) {
        if (look_bv && (int32_t)(look_bk - o) <= 0)
            look_bv = 0;                                                /* the binding entry has been output */
        if (!look_bv) {                                                 /* scan the window for the steepest line */
            int32_t best = 0x7fffffff;
            for (unsigned m = 1; m <= LOOK; ++m) {
                int32_t tk = look_t[(o + m) & LOOK_MASK];
                if (tk < LOOK_UNITY) {
                    int32_t need = look_need(tk, g, m);
                    if (need < best) { best = need; look_bk = o + m; look_bt = tk; look_bv = 1; }
                }
            }
        } else if (t < LOOK_UNITY && look_need(t, g, LOOK) < look_need(look_bt, g, look_bk - o)) {
            look_bk = pn; look_bt = t;                                  /* the new entry asks for a steeper line */
        }
        if (look_bv) {
            int32_t need = look_need(look_bt, g, look_bk - o);
            if (need < step) step = need;
        }
    }
    g += step;
    if (g > LOOK_UNITY) g = LOOK_UNITY;
    if (g < 64) g = 64;
    look_g = g;
    if (g == LOOK_UNITY) { *ol = out_l; *orr = out_r; }
    else {
        *ol = (out_l * g) >> 15;
        *orr = (out_r * g) >> 15;
    }
    if (*ol > 32767) *ol = 32767; else if (*ol < -32768) *ol = -32768;
    if (*orr > 32767) *orr = 32767; else if (*orr < -32768) *orr = -32768;
}

FM1_DSP_CODE void sf2out_render(int32_t *out)
{
    const uint32_t last = position + (FM1_BLOCK_FRAMES - 1U) * RESAMPLE_STEP;
    /* the last output frame reads up to sample floor(last) + TAPS / 2, which must be the newest one: HISTORY + n - 1 */
    const unsigned n = (last >> 24) + RESAMPLE_TAPS / 2 + 1U - RESAMPLE_HISTORY;
    uint8_t sends[2];
    int32_t *sides[2] = {cut_left, cut_right};
    for (unsigned i = 0; i < n; ++i) { melodic[i] = 0; stereo[2 * i] = stereo[2 * i + 1] = 0; }
    unsigned buses = fm1b_render_sends(NULL, melodic, reverb_bus, chorus_bus, sides, n, sends);
    limit(melodic, reverb_bus, chorus_bus, cut_left, cut_right, buses, n);
    sf2fx_mix_sends(melodic, buses & 4U ? cut_left : NULL, buses & 8U ? cut_right : NULL,
                    buses & 1U ? reverb_bus : melodic, buses & 1U ? 127U : sends[0],
                    buses & 2U ? chorus_bus : melodic, buses & 2U ? 127U : sends[1], stereo, n);
    int16_t *l = left_in + RESAMPLE_HISTORY, *r = right_in + RESAMPLE_HISTORY;
    for (unsigned i = 0; i < n; ++i) {                   /* Q23 (up to 2^25) to Q15 through the lookahead limiter: never wraps */
        int32_t ol, orr;
        look_limit(stereo[2 * i] >> 8, stereo[2 * i + 1] >> 8, &ol, &orr);
        l[i] = (int16_t)ol;
        r[i] = (int16_t)orr;
        silent = (l[i] | r[i]) ? 0U : silent < RESAMPLE_HISTORY + PLAYER_FRAMES ? silent + 1U : silent;
    }
    uint32_t at = position;
    if (silent < RESAMPLE_HISTORY + n)                   /* something to hear in what this block reads */
        for (unsigned j = 0; j < FM1_BLOCK_FRAMES; ++j, at += RESAMPLE_STEP) {
            const int16_t *c = resample_table[(at >> 16) & (RESAMPLE_PHASES - 1U)];
            const unsigned from = (at >> 24) - (RESAMPLE_TAPS / 2 - 1);
            const int16_t *xl = left_in + from, *xr = right_in + from;
            int32_t sl = 0, sr = 0;
            for (unsigned k = 0; k < RESAMPLE_TAPS; ++k) {    /* Q15 samples times Q14 taps: the sum stays within 31 bits */
                sl += xl[k] * c[k];
                sr += xr[k] * c[k];
            }
            out[2 * j] = sat24(out[2 * j] + (sl >> 6));
            out[2 * j + 1] = sat24(out[2 * j + 1] + (sr >> 6));
        }
    else
        at += FM1_BLOCK_FRAMES * RESAMPLE_STEP;
    position = at - (n << 24);
    for (unsigned i = 0; i < RESAMPLE_HISTORY; ++i) {    /* the newest frames are the next block's history */
        left_in[i] = left_in[n + i];
        right_in[i] = right_in[n + i];
    }
}
#endif

enum { GUARD_HIGH = 80, GUARD_LOW = 60, GUARD_HOLD = 8 /* blocks after a cut: the faded voices' load leaves over 5 ms */,
       GUARD_CALM = 86 /* blocks under GUARD_LOW before a voice comes back: 0.25 s */, GUARD_FLOOR = 4 };
static unsigned guard_limit = FM1B_VOICES, guard_hold, guard_calm;
static volatile unsigned guard_cap = FM1B_VOICES;         /* fm1-m1: the player's own cap (the FM-1 Setup page): the guard never raises the limit over it */
static volatile unsigned guard_cap_new;                  /* a cap waiting for the audio interrupt (it is the one that moves the limit) */
void sf2out_voice_cap(unsigned cap)
{
    guard_cap_new = cap < 1U ? 1U : cap > FM1B_VOICES ? FM1B_VOICES : cap;
}
static uint32_t load_average;                           /* Q8 percent, a block weighs 1/32: about 0.1 s */
static volatile uint32_t load_peak;
void sf2out_cpu(unsigned *average, unsigned *peak)
{
    *average = (load_average + 128U) >> 8;
    *peak = load_peak;
    load_peak = 0;                                      /* a block that ends just now may be lost from the next peak: harmless */
}
FM1_DSP_CODE void sf2out_load(uint32_t used, uint32_t block)
{
    uint64_t percent = block ? (uint64_t)used * 100U / block : 0;
    uint32_t p = percent > 999U ? 999U : (uint32_t)percent;
    load_average += ((int32_t)(p << 8) - (int32_t)load_average) >> 5;
    if (p > load_peak)
        load_peak = p;
    if (guard_cap_new) {                                  /* a new cap: take it here, in the interrupt that owns the limit */
        guard_cap = guard_cap_new;
        guard_cap_new = 0;
        if (guard_limit > guard_cap) {
            guard_limit = guard_cap;
            fm1b_voice_limit(guard_limit);
        } else if (guard_limit < guard_cap) {
            guard_limit = guard_cap;                      /* raised: up at once; the guard lowers it again if the CPU is over */
            fm1b_voice_limit(guard_limit);
        }
    }
    if (guard_hold) {
        --guard_hold;
        return;
    }
    if (percent > GUARD_HIGH) {
        unsigned sounding = fm1b_voices_sounding();
        unsigned limit = (sounding < guard_limit ? sounding : guard_limit) - (percent > 95U ? 2U : 1U);   /* far over: two at once */
        if (sounding > 2U && limit >= GUARD_FLOOR && limit < guard_limit) {
            guard_limit = limit;
            fm1b_voice_limit(limit);
            guard_hold = GUARD_HOLD;
        }
        guard_calm = 0;
    } else if (percent < GUARD_LOW && guard_limit < guard_cap) {
        if (++guard_calm >= GUARD_CALM) {
            fm1b_voice_limit(++guard_limit);
            guard_calm = 0;
        }
    } else
        guard_calm = 0;
}

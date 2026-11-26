/* SPDX-License-Identifier: GPL-3.0-only */
/* M1:s programeffekter som motorn (reverb, chorus) och m1_fx.c (delay) inte har. Programmets effektdata (typ, DRY:EFF-balans och
 * parametrar för effekt 1 och 2, M1 Owner's Manual s. 40-53 och tabell *11-3) ligger i en tabell sist i banken; programmet är det
 * som den senast startade tonen hör till (fm1b_program_fx). Typerna är manualens nummer minus 1:
 *   13 stereo flanger, 14 cross flanger, 17/18 stereo tremolo 1/2, 20 overdrive, 21 distortion, 22 exciter, 23 symphonic ensemble, 24 rotary.
 * De övriga (reverb, chorus, delay, phaser, EQ ...) görs av motorn och m1_fx.c eller saknas (inget fabriksprogram använder phaser och EQ).
 * Allt är 32-bitsaritmetik (inga 64-bitsmultiplikationer, inga flyttal): sampel i Q15, förstärkningar i Q15, LFO:er som 32-bitsfas.
 * Effekternas egna EQ (parametrarna 6 och 7) används inte. Skalorna för djup, hastighet och nivå är uppskattade, utom där manualen anger dem. */
#include "m1_fx2.h"
#include "m1_cfg.h"
#include "fm1b.h"

enum { T_FLANGER = 13, T_XFLANGER = 14, T_TREMOLO1 = 17, T_TREMOLO2 = 18, T_OVERDRIVE = 20, T_DISTORTION = 21, T_EXCITER = 22,
       T_SYMPHONIC = 23, T_ROTARY = 24 };
enum { ENTRY = 20, LINE = 1024, MAXFX = 2, BLOCK = 128 };

static const uint8_t *tab;
static uint32_t count;

typedef struct {
    uint8_t type, share;                 /* effektens typ (0 = ingen av våra) och DRY:EFF-andelen 0..100 */
    uint8_t p[8];
    uint8_t pad[2];
    uint32_t ph[3];                      /* LFO-faser */
    int32_t lp[4];                       /* enpoliga filter */
    int32_t at;                          /* skrivposition i fördröjningslinjen, 0..LINE-1 */
} fx_t;
/* fördröjningslinjerna (flanger, symphonic, rotary) delas av platserna: bara en effekt åt gången använder dem (RAM är knappt; 23 ms räcker
 * för fabriksprogrammens längsta flanger- och symphonic-svep, cirka 17 ms) */
static int16_t line_l[LINE], line_r[LINE];
static int line_owner = -1;
static fx_t slot[MAXFX];
static int cur = -2;                     /* programmet vars effekter är inställda */
static int16_t sin_tab[257], tanh_tab[513];
static int32_t bl[BLOCK], br[BLOCK];

static int32_t s8(uint32_t b) { return b > 127u ? (int32_t)b - 256 : (int32_t)b; }
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* MG-hastighet (tabell *11-3-2): 0..99 = 0,03..3,00 Hz i steg om 0,03; 100..199 = 3,1..13,0 Hz i steg om 0,1; 200..216 = 14..30 Hz. mHz. */
static uint32_t mg_mhz(uint32_t v)
{
    if (v <= 99u) return 30u * (v + 1u);
    if (v <= 199u) return 3100u + 100u * (v - 100u);
    return 14000u + 1000u * (v > 216u ? 16u : v - 200u);
}
/* fasstegen per bildruta vid 44,1 kHz för mHz: 2^32 / 44100 / 1000 = 97,39 per mHz */
static uint32_t ph_step(uint32_t mhz) { return mhz * 97u + (mhz * 39u) / 100u; }

static void tables(void)
{
    int32_t x = 0, y = 32767, k, pk = 1;             /* en sinus i 256 steg ur en magisk oscillator, normaliserad */
    for (k = 0; k < 256; k++) {
        sin_tab[k] = (int16_t)x;
        x += (804 * y) >> 15;
        y -= (804 * x) >> 15;
    }
    for (k = 0; k < 256; k++) { int32_t a = sin_tab[k] < 0 ? -sin_tab[k] : sin_tab[k]; if (a > pk) pk = a; }
    for (k = 0; k < 256; k++) sin_tab[k] = (int16_t)((sin_tab[k] * 32767) / pk);
    sin_tab[256] = sin_tab[0];
    for (k = 0; k <= 512; k++) {                     /* mjuk mättning t / sqrt(1 + t^2), t = k / 64 */
        int32_t t = k * 64;                          /* Q12: 4096 = 1,0 */
        uint32_t d = 4096u + (uint32_t)((t * t) >> 12), g = 4096u;
        int32_t it;
        for (it = 0; it < 14; it++) g = (g + ((d << 12) / g)) >> 1;   /* sqrt(d) i Q12 */
        tanh_tab[k] = (int16_t)((t * 32767) / (int32_t)g);
    }
}
static int32_t lfo_sin(uint32_t ph)
{
    uint32_t i = ph >> 24, fr = (ph >> 16) & 255u;
    int32_t a = sin_tab[i], b = sin_tab[i + 1];
    return a + (((b - a) * (int32_t)fr) >> 8);
}
static int32_t lfo_tri(uint32_t ph)
{
    int32_t v = (int32_t)(ph >> 16);                 /* 0..65535 */
    v = v < 32768 ? v : 65535 - v;                   /* 0..32767 upp och ner */
    return (v - 16384) * 2;
}
static int32_t soft(int32_t v)                       /* v Q15 (kan vara långt över 1,0): tanh-lik mättning, ut Q15 */
{
    int32_t a = (v < 0 ? -v : v) >> 3, i = a >> 6, y;
    if (i >= 512) y = tanh_tab[512];
    else y = tanh_tab[i] + (((tanh_tab[i + 1] - tanh_tab[i]) * (a & 63)) >> 6);
    return v < 0 ? -y : y;
}

static void select_program(int prog)
{
    uint32_t i, k;
    cur = prog;
    for (i = 0; i < MAXFX; i++) { slot[i].type = 0; slot[i].share = 0; }
    line_owner = -1;
    if (prog < 0 || !tab || (uint32_t)prog >= count)
        return;
    {
        const uint8_t *e = tab + (uint32_t)prog * ENTRY;
        for (k = 0; k < 2u; k++) {
            uint32_t t = e[k], sh = e[2 + k];
            fx_t *f = &slot[k];
            if (t != T_FLANGER && t != T_XFLANGER && t != T_TREMOLO1 && t != T_TREMOLO2 && t != T_OVERDRIVE && t != T_DISTORTION &&
                t != T_EXCITER && t != T_SYMPHONIC && t != T_ROTARY)
                continue;
            for (i = 0; i < 3u; i++) f->ph[i] = 0;
            for (i = 0; i < 4u; i++) f->lp[i] = 0;
            f->at = 0;
            if (t == T_FLANGER || t == T_XFLANGER || t == T_SYMPHONIC || t == T_ROTARY) {
                if (line_owner >= 0 && line_owner != (int)k) {                  /* en andra fördröjningseffekt: ryms inte, hoppas över */
                    continue;
                }
                line_owner = (int)k;
                for (i = 0; i < LINE; i++) line_l[i] = line_r[i] = 0;
            }
            f->type = (uint8_t)t;
            f->share = (uint8_t)(sh > 100u ? 100u : sh);
            for (i = 0; i < 8u; i++) f->p[i] = e[4 + 8 * k + i];
        }
    }
}

void m1_fx2_init(const uint8_t *bank, uint32_t bytes)
{
    tab = 0;
    count = 0;
    cur = -2;
    tables();
    if (bank && bytes > 16u && bank[bytes - 4] == 'M' && bank[bytes - 3] == '1' && bank[bytes - 2] == 'F' && bank[bytes - 1] == 'X') {
        uint32_t n = bank[bytes - 8] | (uint32_t)bank[bytes - 7] << 8, sz = bank[bytes - 6] | (uint32_t)bank[bytes - 5] << 8;
        if (sz == ENTRY && n && n * sz + 8u <= bytes) {
            count = n;
            tab = bank + bytes - 8u - n * sz;
        }
    }
}

/* ------------------------------------------------------------------ effekterna --- */
/* l, r: Q15 in och ut, på plats, n bildrutor */
static void fx_tremolo(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int32_t depth = (int32_t)f->p[0] * 32768 / 99 * f->share / 100;
    const uint32_t step = ph_step(mg_mhz(f->p[1]));
    const int inv = f->type == T_TREMOLO1, tri = f->p[2] & 1;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t m = (tri ? lfo_tri(f->ph[0]) : lfo_sin(f->ph[0])) >> 1;          /* -16383..16383 */
        int32_t gl = 32768 - ((depth * (16384 + m)) >> 15);                       /* 1 - djup * (0,5 + 0,5 lfo) */
        int32_t gr = 32768 - ((depth * (16384 + (inv ? -m : m))) >> 15);
        l[i] = (l[i] * gl) >> 15;
        r[i] = (r[i] * gr) >> 15;
        f->ph[0] += step;
    }
}

/* overdrive och distortion: förförstärkning, mättning, lågpass mot fräs, utnivå. Drive 0..99 (100 klampas), nivå 0..99 */
static void fx_drive(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int hard = f->type == T_DISTORTION;
    const int32_t drive = clampi((int32_t)f->p[2], 0, 99), level = clampi((int32_t)f->p[3], 0, 99);
    int32_t gain = hard ? 1024 : 256, k;                                         /* Q8: overdrive 1x, distortion 4x, exponentiellt i drive */
    for (k = 0; k < drive; k++) gain = gain * (hard ? 1052 : 1030) >> 10;
    gain = clampi(gain, 256, 32000);                                              /* högst 125x: x * gain stannar i 32 bitar */
    {
        int32_t out_g = 2000 + (30000 * level) / 99;                              /* utnivå Q15: nivå 8 ger cirka 0,14 */
        if (!hard) out_g = clampi(out_g * 49 / 10, 0, 32767);                     /* overdrive: +14 dB, mjuk mättning ger lägre nivå än klippning */
        const int32_t wet = f->share * 32768 / 100, dry = 32768 - wet;
        const int32_t alpha = hard ? 23000 : 30000;                               /* enpolig lågpass efter mättningen: ca 5 respektive 7 kHz */
        uint32_t i;
        for (i = 0; i < n; i++) {
            int32_t u = clampi((l[i] * gain) >> 8, -(1 << 20), 1 << 20), v = clampi((r[i] * gain) >> 8, -(1 << 20), 1 << 20);
            if (hard) { u = clampi(u, -24000, 24000); v = clampi(v, -24000, 24000); }
            else { u = soft(u); v = soft(v); }
            f->lp[0] += ((u - f->lp[0]) * alpha) >> 15;
            f->lp[1] += ((v - f->lp[1]) * alpha) >> 15;
            l[i] = ((l[i] * dry) >> 15) + ((((f->lp[0] * out_g) >> 15) * wet) >> 15);
            r[i] = ((r[i] * dry) >> 15) + ((((f->lp[1] * out_g) >> 15) * wet) >> 15);
        }
    }
}

/* exciter: de höga frekvenserna över ett hörn (betonad punkt 0..9: 1,2 kHz upp till 9 kHz) mättas och läggs till; blend -99..99 */
static void fx_exciter(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int32_t blend = s8(f->p[0]) * (int32_t)f->share / 100, point = clampi((int32_t)f->p[1], 0, 9);
    int32_t fc = 1200, alpha, k;
    uint32_t i;
    for (k = 0; k < point; k++) fc = (fc * 5) >> 2;
    alpha = clampi(fc * 4668 / 1000, 500, 30000);                                 /* 2 pi fc / fs i Q15 */
    for (i = 0; i < n; i++) {
        int32_t hx, hy;
        f->lp[0] += ((l[i] - f->lp[0]) * alpha) >> 15;
        f->lp[1] += ((r[i] - f->lp[1]) * alpha) >> 15;
        hx = l[i] - f->lp[0];
        hy = r[i] - f->lp[1];
        l[i] += (soft(hx * 3) * blend / 99) >> 1;
        r[i] += (soft(hy * 3) * blend / 99) >> 1;
    }
}

/* fördröjningslinjens värde delay_q8 prov (Q8) bakom skrivpositionen */
static int32_t tap(const int16_t *line, int32_t at, int32_t delay_q8)
{
    int32_t pos = (at << 8) - delay_q8, i = (pos >> 8) & (LINE - 1), fr = pos & 255;
    int32_t a = line[i], b = line[(i + 1) & (LINE - 1)];
    return a + (((b - a) * fr) >> 8);
}

/* symphonic ensemble: tre moduleringsbara fördröjningar (12 ms, upp till +-5 ms vid djup 99) som blandas till vänster och höger */
static void fx_symphonic(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int32_t depth = f->p[0] > 99u ? 99 : (int32_t)f->p[0];
    const int32_t base = 12 * 44 * 256, sweep = depth * (5 * 44 * 256 / 99);      /* Q8 prov */
    const int32_t wet = (int32_t)f->share * 32768 / 100, dry = 32768 - wet;
    const uint32_t s0 = ph_step(500), s1 = ph_step(330), s2 = ph_step(470);
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t d0 = base + ((sweep * lfo_sin(f->ph[0])) >> 15), d1 = base + ((sweep * lfo_sin(f->ph[1])) >> 15),
                d2 = base + ((sweep * lfo_sin(f->ph[2])) >> 15);
        int32_t ol, orr;
        line_l[f->at] = (int16_t)clampi((l[i] + r[i]) >> 1, -32768, 32767);
        ol = (tap(line_l, f->at, d0) + tap(line_l, f->at, d1)) >> 1;
        orr = (tap(line_l, f->at, d1) + tap(line_l, f->at, d2)) >> 1;
        l[i] = ((l[i] * dry) >> 15) + ((ol * wet) >> 15);
        r[i] = ((r[i] * dry) >> 15) + ((orr * wet) >> 15);
        f->at = (f->at + 1) & (LINE - 1);
        f->ph[0] += s0; f->ph[1] += s1; f->ph[2] += s2;
    }
}

/* flanger och cross flanger: djup 0..99, hastighet, fördröjning 0..50 ms, feedback -99..99, status (bit 0 triangel, bit 1 180 graders fas) */
static void fx_flanger(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int32_t depth = f->p[0] > 99u ? 99 : (int32_t)f->p[0], base_ms = clampi((int32_t)f->p[2], 1, 40);
    const int32_t base = base_ms * 44 * 256 + 5 * 44 * 256 / 10;                   /* Q8 prov: delayen och 0,5 ms */
    const int32_t sweep = (base_ms * 44 * 256 / 10) * 9 / 99 * depth;              /* +- djup * 90 % av grunddelayen */
    const int32_t fb = clampi(s8(f->p[3]), -99, 99) * 29491 / 99;                  /* Q15, högst 0,9 */
    const uint32_t step = ph_step(mg_mhz(f->p[1]));
    const int tri = f->p[4] & 1, inv = f->type == T_FLANGER || (f->p[4] & 2), cross = f->type == T_XFLANGER;
    const int32_t wet = (int32_t)f->share * 32768 / 100, dry = 32768 - wet;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t m = tri ? lfo_tri(f->ph[0]) : lfo_sin(f->ph[0]);
        int32_t ds = (sweep >> 4) * m >> 11;                                       /* sweep * m / 32768 */
        int32_t ol = tap(line_l, f->at, base + ds), orr = tap(line_r, f->at, base + (inv ? -ds : ds));
        int32_t fl = cross ? orr : ol, fr = cross ? ol : orr;
        line_l[f->at] = (int16_t)clampi(l[i] + ((fl * fb) >> 15), -32768, 32767);
        line_r[f->at] = (int16_t)clampi(r[i] + ((fr * fb) >> 15), -32768, 32767);
        l[i] = ((l[i] * dry) >> 15) + ((ol * wet) >> 15);
        r[i] = ((r[i] * dry) >> 15) + ((orr * wet) >> 15);
        f->at = (f->at + 1) & (LINE - 1);
        f->ph[0] += step;
    }
}

/* rotary speaker: delningsfilter vid 800 Hz; tutan (höga) och trumman (låga) snurrar med olika hastighet, tutan med amplitudmodulering,
 * en kort Doppler-fördröjning och panorering. Djup 0..99, hastighetskvot -10..10 (tutan mot trumman) */
static void fx_rotary(fx_t *f, int32_t *l, int32_t *r, uint32_t n)
{
    const int32_t depth = f->p[0] > 99u ? 99 : (int32_t)f->p[0], ratio = clampi(s8(f->p[2]), -10, 10);
    const uint32_t horn = ph_step(900u + 90u * (uint32_t)(ratio + 10)), drum = ph_step(700u);   /* tuta 0,9..2,7 Hz, trumma 0,7 Hz */
    const int32_t wet = (int32_t)f->share * 32768 / 100, dry = 32768 - wet, alpha = 5300;       /* lågpass ca 800 Hz */
    const int32_t dop = depth * (12 * 256) / 99;                                                /* Doppler +- 12 prov = 0,27 ms (Q8) */
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t m = (l[i] + r[i]) >> 1, lo, hi, sh = lfo_sin(f->ph[0]), cs = lfo_sin(f->ph[0] + 0x40000000u), sd = lfo_sin(f->ph[1]);
        int32_t hd, hl, hr, am_h, am_d, ol, orr, pan = depth * (sh >> 1) / 99;
        f->lp[0] += ((m - f->lp[0]) * alpha) >> 15;
        lo = f->lp[0];
        hi = m - lo;
        line_l[f->at] = (int16_t)clampi(hi, -32768, 32767);
        hd = tap(line_l, f->at, 14 * 256 + ((dop * sh) >> 15));
        am_h = 32768 - (depth * (16384 + (cs >> 1)) / 99) / 2;                      /* tutans amplitud: 1 - 0,5 djup (0,5 + 0,5 cos) */
        am_d = 32768 - (depth * (16384 + (sd >> 1)) / 99) / 6;                      /* trummans: mycket mindre */
        hl = (hd * (32768 + pan)) >> 15;                                            /* panorering: tutan flyttar sig mellan kanalerna */
        hr = (hd * (32768 - pan)) >> 15;
        ol = ((hl * am_h) >> 15) + ((lo * am_d) >> 15);
        orr = ((hr * am_h) >> 15) + ((lo * am_d) >> 15);
        l[i] = ((l[i] * dry) >> 15) + ((ol * wet) >> 15);
        r[i] = ((r[i] * dry) >> 15) + ((orr * wet) >> 15);
        f->at = (f->at + 1) & (LINE - 1);
        f->ph[0] += horn;
        f->ph[1] += drum;
    }
}

void m1_fx2_render(int32_t *lr, uint32_t n)
{
    int16_t p[7];
    int prog = -1;
    uint32_t i, k;
    if (!tab || n > BLOCK)
        return;
    if (fm1b_program_fx(p))
        prog = (int)p[6] - 1;
    if (prog != cur)
        select_program(prog);
    if (!slot[0].type && !slot[1].type)
        return;
    for (i = 0; i < n; i++) { bl[i] = lr[2 * i] >> 8; br[i] = lr[2 * i + 1] >> 8; }
    for (k = 0; k < MAXFX; k++) {
        fx_t *f = &slot[k];
        switch (f->type) {
        case T_TREMOLO1: case T_TREMOLO2: fx_tremolo(f, bl, br, n); break;
        case T_OVERDRIVE: case T_DISTORTION: fx_drive(f, bl, br, n); break;
        case T_EXCITER: fx_exciter(f, bl, br, n); break;
        case T_SYMPHONIC: fx_symphonic(f, bl, br, n); break;
        case T_FLANGER: case T_XFLANGER: fx_flanger(f, bl, br, n); break;
        case T_ROTARY: fx_rotary(f, bl, br, n); break;
        default: break;
        }
    }
    for (i = 0; i < n; i++) {
        lr[2 * i] = clampi(bl[i], -(1 << 17), 1 << 17) * 256;
        lr[2 * i + 1] = clampi(br[i], -(1 << 17), 1 << 17) * 256;
    }
}

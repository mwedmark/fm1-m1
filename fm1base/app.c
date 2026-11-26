/* SPDX-License-Identifier: GPL-3.0-only */
/* M1 på fm1-base, första steget: fm1-sf2:s sampelmotor (engine/, m1_unit.c) spelar banken i resursregionen från
 * tangenterna och MIDI. Allt som skriver till motorns kö (fm1b_*) görs från huvudloopen, som fm1b.h kräver (en skrivare);
 * ljudavbrottet bara renderar. Del av plattformens unity-build: gfx.c och announce.c finns i scope. */
#include "m1_cfg.h"                             /* före fm1b.h: live cache (FM1B_PACKED) */
#include "fm1b.h"
#include "sf2out.h"
#include "sf2fx.h"
#include "m1_fx.h"
#include "m1_fx2.h"
#include "m1_setup.h"                          /* FM-1 Setup: icke-standardinställningar, skilda från M1:s menyer */

const int16_t *m1_sea5_dqt(void);               /* m1_unit.c */

#if APP_BLOCK != 128u
#error "APP_BLOCK måste vara FM1_BLOCK_FRAMES (engine/engine.h)"
#endif

#define SET_MAGIC 0x3153314Du                    /* "M1S1" */
typedef struct {
    uint32_t magic;
    int8_t octave;                               /* -3 .. +3: Kick 1 i M1:s kittar ligger på tangent 24 */
    uint8_t program;                             /* 0..127 */
    uint8_t setup;                               /* FM-1 Setup (m1_setup.h): röstgränsen; 0 i en äldre post = standard */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty, bank_ok;

/* live cache: MP3-ljuden avkodas hit (BFP8) när en not behöver dem. fm1-base:s pool (gfx.c och andra tar resten) */
static uint8_t cache_pool[FM1B_CACHE_KB * 1024u] __attribute__((aligned(4), section(".pool")));

/* ------------------------------------------------------------------- data --- */
/* banken: resursregionens innehåll. På enheten ger plat_res den; i webbemulatorn läses regionen som den ligger (commit-posten
 * "RES1", längd och CRC-32), som i fm1-d50 app.c app_d50_data */
static uint32_t crc32_add(uint32_t c, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t k;
    while (n--) {
        c ^= *b++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}
static uint32_t res_crc32(const void *p, uint32_t n) { return ~crc32_add(0xFFFFFFFFu, p, n); }
static const uint8_t *bank_data(uint32_t *len)
{
#if FM1B_RES
    {
        uint32_t n = 0;
        const uint8_t *p = plat_res(&n);
        if (p && n) {
            *len = n;
            return p;
        }
    }
#endif
    {
        const uint8_t *r = plat_data(APP_RES_BASE);
        uint32_t h[8];
        if (r) {
            memcpy(h, r, sizeof h);
            if (h[0] == 0x31534552u && h[7] == res_crc32(h, 28u) && h[1] && h[1] <= APP_RES_SECTORS * 4096u - 32u &&
                res_crc32(r + 32, h[1]) == h[2]) {
                *len = h[1];
                return plat_data(APP_RES_BASE + 32u);
            }
        }
    }
    *len = 0;
    return 0;
}
static uint32_t res_stamp(void)                  /* commit-postens CRC: ändras när ny data skrivits över USB */
{
    uint32_t crc = 0;
    const uint8_t *r = plat_data(APP_RES_BASE);
    if (r)
        memcpy(&crc, r + 8, sizeof crc);
    return crc;
}

/* ------------------------------------------------------------------ audio --- */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (!bank_ok)
        return;
    sf2out_render(lr);
    m1_fx_render(lr, n);                         /* programmets delay på hela utgången */
    m1_fx2_render(lr, n);                        /* tremolo, overdrive, distortion, exciter, symphonic, flanger, rotary */
    m1_limit_post(lr, n);
    for (i = 0; i < 2u * n; i++)                 /* MASTER (0..4096) */
        lr[i] = (int32_t)(((int64_t)lr[i] * (int32_t)master_q12) >> 12);
    sf2out_load(plat_cpu_pct(), 100u);
}

void app_panic(void)
{
    if (bank_ok)
        fm1b_panic();
}

/* ------------------------------------------------------------------- MIDI --- */
static volatile uint32_t midi_count;
static void midi_in(void)                        /* huvudloopen: allt som kommit in, till motorns kö */
{
    uint32_t pkt;
    int port;
    while (fm1b_queue_room() > 4u && plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st >= 0xF0u || !bank_ok)
            continue;
        midi_count++;
        switch (st & 0xF0u) {                    /* kanal 10 (ch 9): M1:s trumkittar i bank 128, programbyte väljer kitt */
        case 0x80u:
            if (ch == 9u) fm1b_channel_drum_note(ch, d1, 0, 127u); else fm1b_channel_note(ch, d1, 0, 127u);
            break;
        case 0x90u:
            if (ch == 9u) fm1b_channel_drum_note(ch, d1, d2, 127u); else fm1b_channel_note(ch, d1, d2, 127u);
            break;
        case 0xB0u: fm1b_channel_control(ch, d1, d2); break;
        case 0xC0u: fm1b_channel_program(ch, ch == 9u ? 128u : 0u, d1); break;
        case 0xE0u: fm1b_channel_bend(ch, d1 | d2 << 7); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------ mätning --- */
/* F0 7D 'M' '1' 01 F7: CPU (sf2out_cpu: medel och topp sedan förra frågan; plat_cpu_pct nu), missade ljudblock (plat_xruns),
 * röster (nu, gräns, kapade), CPU-vaktens kapningar och live cachens räknare (fm1b_cache_stats). Värdena räknar från start
 * (utom CPU-toppen): den som frågar tar skillnaden. tools/m1_measure.py läser dem. */
int app_sysex(const uint8_t *p, uint32_t n)
{
    uint8_t r[5 + 16 * 5 + 1] = {0xF0, 0x7D, 'M', '1', 0x41}, *d = r + 5;
    uint32_t v[16] = {0}, i, k;
    unsigned avg = 0, peak = 0;
    struct fm1b_status st;
    struct fm1b_cache_stats cs;
    if (n != 4u || p[0] != 0x7Du || p[1] != 'M' || p[2] != '1' || p[3] != 1u)
        return 0;
    memset(&cs, 0, sizeof cs);
    memset(&st, 0, sizeof st);
    if (bank_ok) {
        sf2out_cpu(&avg, &peak);
        fm1b_status(&st);
        fm1b_cache_stats(&cs);
    }
    v[0] = avg;
    v[1] = peak;
    v[2] = plat_cpu_pct();
    v[3] = plat_xruns();
    v[4] = bank_ok ? fm1b_voices_sounding() : 0u;
    v[5] = st.voice_limit;
    v[6] = st.voices_cut;
    v[7] = st.drops_guard;
    v[8] = cs.hits;
    v[9] = cs.misses;
    v[10] = cs.evictions;
    v[11] = cs.no_room;
    v[12] = cs.decoded_mp3_frames;
    v[13] = cs.requested;
    v[14] = cs.lost;
    v[15] = cs.waiting;
    for (i = 0; i < 16u; i++)
        for (k = 0; k < 5u; k++)
            *d++ = (uint8_t)(v[i] >> (7u * k) & 0x7Fu);
    *d = 0xF7;
    (void)plat_sysex_send(r, sizeof r);
    return 1;
}

/* --------------------------------------------------------------- settings --- */
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.octave < -3 || set.octave > 3 || set.program > 127u) {
        set.magic = SET_MAGIC;
        set.octave = 0;
        set.program = 0;
        set.setup = 0;
    }
    set.setup = m1_setup_clean(set.setup);
}

int app_backup_playing(void) { return bank_ok && fm1b_voices_sounding() != 0u; }
void app_backup_flush(void) {}
void app_backup_hold(int on) { (void)on; }
void app_backup_restored(void) { settings_load(); }

static void program_select(int32_t step)         /* nästa program som banken har, åt det hållet */
{
    int32_t p = set.program, k;
    for (k = 0; k < 128; k++) {
        p = (p + step + 128) & 127;
        if (fm1b_has_program(0, (unsigned)p))
            break;
    }
    if (k == 128)
        return;
    set.program = (uint8_t)p;
    set_dirty = 1;
    fm1b_program(0, (unsigned)p);
}

void app_init(void)
{
    uint32_t n = 0, bank_len;
    const uint8_t *p = bank_data(&n);
    settings_load();
    fm1b_cache_attach(cache_pool, sizeof cache_pool);
    /* bankens längd står i dess header (offset 40); resursen kan ha effekttabellen efter den (tools/m1_fxtable.py) */
    bank_len = p && n >= 48u && p[0] == 'F' && p[1] == 'M' && p[2] == '1' && p[3] == 'B' ?
               (uint32_t)p[40] | (uint32_t)p[41] << 8 | (uint32_t)p[42] << 16 | (uint32_t)p[43] << 24 : n;
    if (bank_len > n)
        bank_len = n;
    bank_ok = p && fm1b_bind(p, bank_len, m1_sea5_dqt()) == 0;
    /* M1:s effektandel (DRY:EFF 0..100) står i varje zons chorus-send (tools/m1_sf2.py): chorusens masternivå 95 (0,75 av full: svängningen i 2-4 kHz nära originalets), så att 50 %
     * ger lika mycket chorus som torr signal (fm1-sf2:s standard 30 av 127 gav E.Piano 3 en tredjedel av M1:s stereobredd) */
    sf2fx_set(sf2fx_reverb(), 95u);
    m1_setup_apply(set.setup);
    m1_fx2_init(p, n);                           /* programmens effektdata: tabellen sist i banken */
    if (bank_ok && fm1b_program(0, set.program) < 0) {
        set.program = 127;
        program_select(1);
    }
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until, keys, btn;
    uint8_t redraw;
    uint8_t page;                                /* 0 spelsidan, 1 FM-1 Setup (EDIT växlar) */
    uint8_t field;                               /* Setup-sidans fält */
} ui;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on())
        ann_say(ui.msg[0], 0, ANN_NOW);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    if (bank_ok)
        fm1b_cache_service(FM1B_CACHE_FRAMES);   /* live cache: svara på notternas önskemål, avkoda några MP3-ramar */
    midi_in();
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS && bank_ok; k++) {     /* tangent 0 är F3 (MIDI 53); panelen spelar kanal 1 */
        uint32_t bit = 1u << k, note = (uint32_t)(53 + (int32_t)k + 12 * set.octave);
        if (fm1b_queue_room() < 2u)
            break;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            fm1b_note(0, note, v ? v : 1u, 127u);
            plat_midi_send(0x09u | 0x90u << 8 | note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            fm1b_note(0, note, 0, 127u);
            plat_midi_send(0x08u | 0x80u << 8 | note << 16, TO_USB);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if ((k == B_OCTDN || k == B_OCTUP) && bank_ok) {
            int32_t o = clamp(set.octave + (k == B_OCTUP ? 1 : -1), -3, 3);
            if (o != set.octave) {
                fm1b_panic();                    /* en hållen tangent hittar annars inte sin note off */
                set.octave = (int8_t)o;
                set_dirty = 1;
                ui.redraw = 1;
            }
        } else if (k == B_EDIT) {                /* FM-1 Setup (som fm1-d50:s Setup-sida): EDIT in och ut */
            ui.page = ui.page ? 0u : 1u;
            ui.field = 0;
            ui.redraw = 1;
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        }
    }
    if (ui.page) {                               /* Setup-sidan: K1 flyttar fältet, K2 ändrar värdet */
        if ((e = plat_enc(EN_K1)) != 0) {
            ui.field = (uint8_t)clamp((int32_t)ui.field + (e > 0 ? 1 : -1), 0, M1_SETUP_FIELDS - 1);
            ui.redraw = 1;
        }
        if ((e = plat_enc(EN_K2)) != 0 || (e = plat_enc(EN_PRESET)) != 0) {
            set.setup = m1_setup_set(set.setup, ui.field, m1_setup_get(set.setup, ui.field) + (e > 0 ? 1 : -1));
            m1_setup_apply(set.setup);
            set_dirty = 1;                       /* sparas med SAVE */
            ui.redraw = 1;
        }
    } else if ((e = plat_enc(EN_PRESET)) != 0 && bank_ok) {
        program_select(e > 0 ? 1 : -1);
        ui.redraw = 1;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u), keys);
}

void app_input_only(void) { input(); }

/* Setup-sidan i text. När LCD-grafiken kommer byts bara den här funktionen ut: modellen och lagringen ligger i m1_setup.h. */
static void m1_setup_draw(const struct fm1b_status *st, unsigned avg)
{
    char t[24];
    uint32_t i, v;
    (void)st;
    cv_begin(240, 140, C_BLACK);
    cv_text(10, 6, &FONT_B, "FM-1 SETUP", C_WHITE);
    for (i = 0; i < M1_SETUP_FIELDS; i++) {
        const char *n = m1_setup_fields[i].name;
        uint32_t k = 0;
        for (; n[k] && k < 10u; k++)
            t[k] = n[k];
        t[k] = 0;
        cv_text(10, 50 + 30 * i, &FONT_M, t, i == ui.field ? C_HI : C_WHITE);
        v = (uint32_t)m1_setup_get(set.setup, i);
        t[0] = (char)('0' + v / 10 % 10);
        t[1] = (char)('0' + v % 10);
        t[2] = 0;
        cv_text(150, 50 + 30 * i, &FONT_M, t, i == ui.field ? C_HI : C_WHITE);
    }
    cv_blit(0, 0);
    cv_begin(240, 100, C_BLACK);
    cv_text(10, 8, &FONT_XS, "K2 CHANGE   SAVE KEEPS", C_GRAY);
    t[0] = 'C'; t[1] = 'P'; t[2] = 'U'; t[3] = ' ';
    t[4] = (char)('0' + avg / 100 % 10); t[5] = (char)('0' + avg / 10 % 10); t[6] = (char)('0' + avg % 10);
    t[7] = '%'; t[8] = 0;
    cv_text(10, 40, &FONT_XS, t, C_GRAY);
    cv_blit(0, 140);
}

static void draw(void)
{
    char t[24];
    uint32_t now = plat_ms();
    struct fm1b_status st;
    unsigned avg = 0, peak = 0;
    if (bank_ok) {
        fm1b_status(&st);
        sf2out_cpu(&avg, &peak);
    }
    if (ui.page) {
        m1_setup_draw(&st, avg);
        return;
    }
    /* ritytan rymmer 240 x 144 (gfx.c APP_CANVAS_PX): två remsor */
    cv_begin(240, 140, C_BLACK);
    cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
    if (!bank_ok) {
        cv_text(10, 100, &FONT_M, "NO M1 DATA", C_HI);
    } else {
        t[0] = (char)('0' + (set.program + 1) / 100);
        t[1] = (char)('0' + (set.program + 1) / 10 % 10);
        t[2] = (char)('0' + (set.program + 1) % 10);
        t[3] = 0;
        cv_text(10, 60, &FONT_M, t, C_HI);
        cv_text(10, 100, &FONT_B, st.name, C_WHITE);
    }
    cv_blit(0, 0);
    cv_begin(240, 100, C_BLACK);
    if (now < ui.msg_until) {
        cv_text(10, 20, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(120, 20, &FONT_B, ui.msg[1], C_HI);
    }
    if (bank_ok) {
        t[0] = 'O'; t[1] = 'C'; t[2] = 'T'; t[3] = ' ';
        t[4] = set.octave < 0 ? '-' : '+';
        t[5] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
        t[6] = 0;
        cv_text(10, 60, &FONT_XS, t, C_GRAY);
        t[0] = 'V'; t[1] = ' ';
        t[2] = (char)('0' + st.voices / 10 % 10); t[3] = (char)('0' + st.voices % 10);
        t[4] = ' '; t[5] = 'C'; t[6] = 'P'; t[7] = 'U'; t[8] = ' ';
        t[9] = (char)('0' + avg / 100 % 10); t[10] = (char)('0' + avg / 10 % 10); t[11] = (char)('0' + avg % 10);
        t[12] = '%'; t[13] = 0;
        cv_text(80, 60, &FONT_XS, t, C_GRAY);
    }
    cv_blit(0, 140);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t stamp0, restart_at, last_draw;
    static uint8_t stamped;
    uint32_t now = plat_ms();
    if (!stamped) {
        stamped = 1;
        stamp0 = res_stamp();
    } else if (!restart_at && res_stamp() != stamp0) {   /* ny bank skriven: starta om på den */
        restart_at = (now + 2500u) | 1u;
        app_say("NEW DATA", "RESTART");
    }
    if (restart_at && (int32_t)(now - restart_at) >= 0)
        plat_restart();
    input();
    ann_tick(now);
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (bank_ok && now - last_draw > 250u)       /* röster och CPU */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

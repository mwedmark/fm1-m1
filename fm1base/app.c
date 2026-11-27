/* SPDX-License-Identifier: GPL-3.0-only */
/* M1 on fm1-base, the first step: fm1-sf2's sample engine (engine/, m1_unit.c) plays the bank in the resource region from
 * the keys and MIDI. Everything that writes to the engine's queue (fm1b_*) is done from the main loop, as fm1b.h requires (one writer);
 * the audio interrupt only renders. Part of the platform's unity build: gfx.c and announce.c are in scope. */
#include "m1_cfg.h"                             /* before fm1b.h: live cache (FM1B_PACKED) */
#include "fm1b.h"
#include "sf2out.h"
#include "sf2fx.h"
#include "m1_fx.h"
#include "m1_fx2.h"
#include "m1_combi.h"                         /* the M1's combinations: timbres on the engine's channels 0..7 */
#include "m1_setup.h"                          /* FM-1 Setup: non-standard settings, kept apart from the M1's menus */

const int16_t *m1_sea5_dqt(void);               /* m1_unit.c */

#if APP_BLOCK != 128u
#error "APP_BLOCK must be FM1_BLOCK_FRAMES (engine/engine.h)"
#endif

#define SET_MAGIC 0x3153314Du                    /* "M1S1" */
typedef struct {
    uint32_t magic;
    int8_t octave;                               /* -3 .. +3: Kick 1 in the M1's kits is on key 24 */
    uint8_t program;                             /* 0..127 */
    uint8_t setup;                               /* FM-1 Setup (m1_setup.h): the voice limit; 0 in an older record = default */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty, bank_ok;

/* live cache: the MP3 sounds are decoded here (BFP8) when a note needs them. fm1-base's pool (gfx.c and others take the rest) */
static uint8_t cache_pool[FM1B_CACHE_KB * 1024u] __attribute__((aligned(4), section(".pool")));

/* ------------------------------------------------------------------- data --- */
/* the bank: the resource region's contents. On the device plat_res provides it; in the web emulator the region is read as it lies (the commit record
 * "RES1", length and CRC-32), as in fm1-d50 app.c app_d50_data */
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
static uint32_t res_stamp(void)                  /* the commit record's CRC: changes when new data has been written over USB */
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
    m1_fx_render(lr, n);                         /* the program's delay on the whole output */
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
static uint8_t combi_no;                         /* the combination that PRESET / MIDI program change selected (0..99), meaningful in combination mode */
static struct {
    char msg[2][24];
    uint32_t msg_until, keys, btn;
    uint8_t redraw;
    uint8_t page;                                /* 0 the play page, 1 FM-1 Setup (EDIT toggles) */
    uint8_t field;                               /* the Setup page's field */
} ui;

static void midi_in(void)                        /* the main loop: everything that has come in, to the engine's queue */
{
    uint32_t pkt;
    int port;
    while (fm1b_queue_room() > 4u && plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st >= 0xF0u || !bank_ok)
            continue;
        midi_count++;
        if (m1_combi_active() >= 0 && ch != 9u) {   /* a combination: the timbres take the notes (channel 10 stays the drum kits') */
            switch (st & 0xF0u) {
            case 0x80u: m1_combi_note(ch, d1, 0); break;
            case 0x90u: m1_combi_note(ch, d1, d2); break;
            case 0xB0u: m1_combi_control(ch, d1, d2); break;
            case 0xC0u:                                  /* program change selects the combination (as on the M1 in COMBI mode) */
                if (d1 < m1_combi_count() && m1_combi_select(d1) == 0) {
                    combi_no = (uint8_t)d1;
                    ui.redraw = 1;
                }
                break;
            case 0xE0u: m1_combi_bend(ch, d1 | d2 << 7); break;
            default: break;
            }
            continue;
        }
        switch (st & 0xF0u) {                    /* channel 10 (ch 9): the M1's drum kits in bank 128, program change selects the kit */
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

/* ------------------------------------------------------------ measurement --- */
/* F0 7D 'M' '1' 01 F7: CPU (sf2out_cpu: average and peak since the previous query; plat_cpu_pct now), missed audio blocks (plat_xruns),
 * voices (now, limit, cut), the CPU guard's cuts and the live cache's counters (fm1b_cache_stats). The values count from start
 * (except the CPU peak): whoever asks takes the difference. tools/m1_measure.py reads them. */
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

static void program_select(int32_t step)         /* the next program that the bank has, in that direction */
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
    /* the bank's length is in its header (offset 40); the resource may have the effect table after it (tools/m1_fxtable.py) */
    bank_len = p && n >= 48u && p[0] == 'F' && p[1] == 'M' && p[2] == '1' && p[3] == 'B' ?
               (uint32_t)p[40] | (uint32_t)p[41] << 8 | (uint32_t)p[42] << 16 | (uint32_t)p[43] << 24 : n;
    if (bank_len > n)
        bank_len = n;
    bank_ok = p && fm1b_bind(p, bank_len, m1_sea5_dqt()) == 0;
    /* The M1's effect share (DRY:EFF 0..100) is in each zone's chorus send (tools/m1_sf2.py): the chorus's master level 95 (0.75 of full: the swing at 2-4 kHz close to the original's), so that 50 %
     * gives as much chorus as dry signal (fm1-sf2's default 30 of 127 gave E.Piano 3 a third of the M1's stereo width) */
    sf2fx_set(sf2fx_reverb(), 95u);
    m1_setup_apply(set.setup);
    m1_fx2_init(p, n);                           /* the programs' effect data: the table at the end of the bank */
    m1_combi_init(p, n);                         /* the combinations (tools/m1_combi.py): the table in front of it, or none */
    if (bank_ok && fm1b_program(0, set.program) < 0) {
        set.program = 127;
        program_select(1);
    }
}

/* --------------------------------------------------------------------- UI --- */

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
        fm1b_cache_service(FM1B_CACHE_FRAMES);   /* live cache: answer the notes' requests, decode a few MP3 frames */
    midi_in();
    m1_combi_service();                          /* applies a combination select/leave, as the engine's queue has room */
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS && bank_ok; k++) {     /* key 0 is F3 (MIDI 53); the panel plays channel 1 */
        uint32_t bit = 1u << k, note = (uint32_t)(53 + (int32_t)k + 12 * set.octave);
        if (fm1b_queue_room() < 2u)
            break;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            if (m1_combi_active() >= 0) m1_combi_note(M1_COMBI_GLOBAL, note, v ? v : 1u); else fm1b_note(0, note, v ? v : 1u, 127u);
            plat_midi_send(0x09u | 0x90u << 8 | note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (m1_combi_active() >= 0) m1_combi_note(M1_COMBI_GLOBAL, note, 0); else fm1b_note(0, note, 0, 127u);
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
                fm1b_panic();                    /* a held key would otherwise not find its note off */
                set.octave = (int8_t)o;
                set_dirty = 1;
                ui.redraw = 1;
            }
        } else if (k == B_PLAY && bank_ok) {     /* PROGRAM <-> COMBINATION mode (the M1's PROG and COMBI) */
            if (m1_combi_active() >= 0 || m1_combi_busy()) {
                fm1b_panic();
                m1_combi_leave();
                app_say("PROGRAM", "");
            } else if (m1_combi_count() && m1_combi_select(combi_no) == 0) {
                fm1b_panic();
                app_say("COMBI", "");
            } else {
                app_say("NO COMBI", "DATA");
            }
            ui.redraw = 1;
        } else if (k == B_EDIT) {                /* FM-1 Setup (like fm1-d50's Setup page): EDIT in and out */
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
    if (ui.page) {                               /* the Setup page: K1 moves the field, K2 changes the value */
        if ((e = plat_enc(EN_K1)) != 0) {
            ui.field = (uint8_t)clamp((int32_t)ui.field + (e > 0 ? 1 : -1), 0, M1_SETUP_FIELDS - 1);
            ui.redraw = 1;
        }
        if ((e = plat_enc(EN_K2)) != 0 || (e = plat_enc(EN_PRESET)) != 0) {
            set.setup = m1_setup_set(set.setup, ui.field, m1_setup_get(set.setup, ui.field) + (e > 0 ? 1 : -1));
            m1_setup_apply(set.setup);
            set_dirty = 1;                       /* saved with SAVE */
            ui.redraw = 1;
        }
    } else if ((e = plat_enc(EN_PRESET)) != 0 && bank_ok) {
        if (m1_combi_active() >= 0 || m1_combi_busy()) {         /* combination mode: PRESET steps through the combinations */
            uint32_t n = m1_combi_count();
            uint32_t c = ((uint32_t)combi_no + (e > 0 ? 1u : n - 1u)) % n;
            if (m1_combi_select(c) == 0)
                combi_no = (uint8_t)c;
        } else {
            program_select(e > 0 ? 1 : -1);
        }
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

/* The Setup page as text. When the LCD graphics arrive only this function is replaced: the model and the storage are in m1_setup.h. */
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
    /* the canvas holds 240 x 144 (gfx.c APP_CANVAS_PX): two strips */
    cv_begin(240, 140, C_BLACK);
    cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
    if (!bank_ok) {
        cv_text(10, 100, &FONT_M, "NO M1 DATA", C_HI);
    } else {
        if (m1_combi_active() >= 0 || m1_combi_busy()) {         /* combination mode: C01 and the combination's name */
            char cn[11];
            t[0] = 'C';
            t[1] = (char)('0' + (combi_no + 1) / 10 % 10);
            t[2] = (char)('0' + (combi_no + 1) % 10);
            t[3] = 0;
            cv_text(10, 60, &FONT_M, t, C_HI);
            m1_combi_name(combi_no, cn);
            cv_text(10, 100, &FONT_B, cn, C_WHITE);
        } else {
            t[0] = (char)('0' + (set.program + 1) / 100);
            t[1] = (char)('0' + (set.program + 1) / 10 % 10);
            t[2] = (char)('0' + (set.program + 1) % 10);
            t[3] = 0;
            cv_text(10, 60, &FONT_M, t, C_HI);
            cv_text(10, 100, &FONT_B, st.name, C_WHITE);
        }
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
    } else if (!restart_at && res_stamp() != stamp0) {   /* new bank written: restart on it */
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
    if (bank_ok && now - last_draw > 250u)       /* voices and CPU */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

/* ------------------------------------------------------------- host tests --- */
#ifdef PLAT_HOST                                 /* the simulator's checks (fm1base/tests/m1_combi.txt): not in the device build */
int app_expect(const char *what, int *got)
{
    unsigned ch = 0;
    const char *q;
    struct fm1b_channel c;
    if (!strcmp(what, "combi")) { *got = m1_combi_active(); return 1; }
    if (!strcmp(what, "combi_busy")) { *got = (int)m1_combi_busy(); return 1; }
    if (!strcmp(what, "voices")) { *got = (int)fm1b_voices_sounding(); return 1; }
    if (what[0] != 'c' || what[1] != 'h')
        return 0;
    for (q = what + 2; *q >= '0' && *q <= '9'; q++)
        ch = ch * 10u + (unsigned)(*q - '0');
    if (q == what + 2 || ch >= FM1B_CHANNELS || *q != '_')
        return 0;
    fm1b_channel_info(ch, &c);
    if (!strcmp(q, "_notes")) *got = c.notes;
    else if (!strcmp(q, "_voices")) *got = c.voices;
    else if (!strcmp(q, "_program")) *got = c.program;
    else if (!strcmp(q, "_own")) *got = c.own;
    else if (!strcmp(q, "_volume")) *got = c.volume;
    else if (!strcmp(q, "_pan")) *got = c.pan;
    else return 0;
    return 1;
}
#endif

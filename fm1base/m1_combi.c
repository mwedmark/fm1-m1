/* SPDX-License-Identifier: GPL-3.0-only */
/* The M1's combinations on the engine's 16 MIDI channels. Timbre k of the active combination plays on engine channel k (0..7: channel 9 is the
 * drum kits'). A note that comes in is checked against each timbre's key and velocity range, transposed, and sent to the channels that take it;
 * the notes that sound are remembered, so a note off ends exactly them even if the combination has been changed meanwhile.
 *
 * Single, Layer, Split and Velocity Switch differ only in the ranges the data holds. In Multi a timbre only takes the MIDI channel it is set to
 * (the panel's keys reach all of them).
 *
 * Not done: the combination's own effects (the last played program's effect is the one heard), the timbres' detune (the engine has no fine tuning per
 * channel), the control filter (which controllers a timbre accepts) and the output assignments. The pan value's scale (0..15) is estimated.
 * See docs/M1_COMBINATIONS.md.
 *
 * The engine's queue is 32 events deep and is emptied once per audio block, so a select is applied step by step from m1_combi_service(). */
#include <string.h>
#include "m1_cfg.h"                             /* before fm1b.h, as in m1_fx2.c */
#include "m1_combi.h"
#include "fm1b.h"

enum { MAX_T = 8, T_BYTES = 10, NAME_B = 10, HEAD_B = NAME_B + 2, MAX_HELD = 64, DRUM_CH = 9 };

typedef struct {
    uint8_t program, level, pan, key_top, key_bottom, vel_top, vel_bottom, channel;
    int8_t transpose, detune;
} timbre_t;

static const uint8_t *tab;                                 /* the table's first byte: the offsets are from here */
static uint32_t count;

static int cur = -1;                                       /* the active combination, -1: program mode */
static int want = -1;                                      /* the one being set up */
static timbre_t tim[MAX_T];
static unsigned nt, ctype;

typedef struct { uint8_t in_ch, in_note, out_ch, out_note; } held_t;
static held_t held[MAX_HELD];
static unsigned nheld;

static enum { P_IDLE, P_RELEASE, P_SETUP, P_LEAVE } phase;
static unsigned step;

/* walk back from the end over the other tables ("M1FX" is fm1base/m1_fx2.c's: count, size, magic) to "M1CB" (count, length, magic) */
void m1_combi_init(const uint8_t *bank, uint32_t bytes)
{
    uint32_t end = bytes, guard;
    tab = 0;
    count = 0;
    cur = want = -1;
    nheld = 0;
    nt = 0;
    phase = P_IDLE;
    for (guard = 0; bank && guard < 4u && end > 16u; guard++) {
        const uint8_t *t = bank + end - 8u;
        uint32_t n = t[0] | (uint32_t)t[1] << 8, v = t[2] | (uint32_t)t[3] << 8;
        if (t[4] == 'M' && t[5] == '1' && t[6] == 'C' && t[7] == 'B') {
            if (n && v >= 2u * n && v + 8u <= end) {
                tab = bank + end - 8u - v;
                count = n;
            }
            return;
        }
        if (t[4] == 'M' && t[5] == '1' && t[6] == 'F' && t[7] == 'X' && v && n * v + 8u <= end) {
            end -= n * v + 8u;
            continue;
        }
        break;
    }
}

/* the record of a combination, or 0 if it is outside the table */
static const uint8_t *record(unsigned i)
{
    uint32_t o;
    if (!tab || i >= count)
        return 0;
    o = tab[2u * i] | (uint32_t)tab[2u * i + 1u] << 8;
    return tab + o;
}

unsigned m1_combi_count(void) { return count; }

void m1_combi_name(unsigned i, char out[11])
{
    const uint8_t *r = record(i);
    unsigned k;
    for (k = 0; k < NAME_B; k++)
        out[k] = r && r[k] >= 32u && r[k] < 127u ? (char)r[k] : ' ';
    out[NAME_B] = 0;
}

unsigned m1_combi_type(unsigned i) { const uint8_t *r = record(i); return r ? r[NAME_B] : 0u; }
unsigned m1_combi_timbres(unsigned i) { const uint8_t *r = record(i); return r ? r[NAME_B + 1] : 0u; }
int m1_combi_active(void) { return cur; }
unsigned m1_combi_busy(void) { return phase != P_IDLE; }

/* the combination's timbres into tim[] (at most MAX_T) */
static void load(unsigned i)
{
    const uint8_t *r = record(i);
    unsigned k;
    ctype = r[NAME_B];
    nt = r[NAME_B + 1];
    if (nt > MAX_T)
        nt = MAX_T;
    for (k = 0; k < nt; k++) {
        const uint8_t *b = r + HEAD_B + T_BYTES * k;
        timbre_t *t = &tim[k];
        t->program = b[0];
        t->level = b[1];
        t->transpose = (int8_t)b[2];
        t->detune = (int8_t)b[3];
        t->pan = b[4];
        t->key_top = b[5];
        t->key_bottom = b[6];
        t->vel_top = b[7];
        t->vel_bottom = b[8];
        t->channel = (uint8_t)(b[9] & 15u);
    }
}

int m1_combi_select(unsigned i)
{
    if (!record(i))
        return -1;
    want = (int)i;
    phase = P_RELEASE;                                     /* the notes that sound end first, then the timbres are set up */
    step = 0;
    return 0;
}

void m1_combi_leave(void)
{
    want = -1;
    phase = P_RELEASE;
    step = 0;
}

/* M1 level 0..99 -> CC 7 0..127; pan 0..15 (estimated: 0 left, 15 right, 7/8 the centre) -> CC 10 */
static unsigned cc_level(unsigned lv) { return lv >= 99u ? 127u : (lv * 127u + 49u) / 99u; }
static unsigned cc_pan(unsigned pan) { return (pan & 15u) * 127u / 15u; }

void m1_combi_service(void)
{
    if (phase == P_IDLE)
        return;
    if (phase == P_RELEASE) {
        while (nheld) {
            if (fm1b_queue_room() < 2u)
                return;
            nheld--;
            fm1b_channel_note(held[nheld].out_ch, held[nheld].out_note, 0, 127u);
        }
        if (want < 0) {
            phase = P_LEAVE;
            step = 0;
        } else {
            load((unsigned)want);
            cur = want;
            phase = P_SETUP;
            step = 0;
        }
    }
    if (phase == P_SETUP) {
        while (step < nt) {
            const timbre_t *t = &tim[step];
            if (fm1b_queue_room() < 6u)
                return;
            fm1b_channel_control(step, 121u, 0);           /* reset the channel's levels, bend, wheel and pedal */
            fm1b_channel_program(step, 0, t->program);
            fm1b_channel_control(step, 7u, cc_level(t->level));
            fm1b_channel_control(step, 10u, cc_pan(t->pan));
            step++;
        }
        phase = P_IDLE;
        return;
    }
    if (phase == P_LEAVE) {
        while (step < MAX_T) {
            if (fm1b_queue_room() < 4u)
                return;
            fm1b_channel_follow(step);
            fm1b_channel_control(step, 121u, 0);
            fm1b_channel_control(step, 7u, 127u);
            fm1b_channel_control(step, 10u, 64u);
            step++;
        }
        cur = -1;
        nt = 0;
        phase = P_IDLE;
    }
}

static unsigned takes(unsigned k, unsigned in_ch)         /* does timbre k take notes on in_ch? */
{
    if (in_ch == M1_COMBI_GLOBAL)
        return 1u;
    if (in_ch == DRUM_CH)
        return 0u;
    return ctype == 4u ? tim[k].channel == in_ch : 1u;
}

void m1_combi_note(unsigned in_ch, unsigned note, unsigned velocity)
{
    unsigned k, i;
    if (cur < 0 || phase == P_RELEASE || phase == P_LEAVE)
        return;
    if (velocity == 0u) {                                  /* note off: end what this note started */
        for (i = 0; i < nheld;) {
            if (held[i].in_ch == in_ch && held[i].in_note == note) {
                if (fm1b_queue_room() < 2u)
                    return;                                /* the rest follows with the next off (the key's release repeats it) */
                fm1b_channel_note(held[i].out_ch, held[i].out_note, 0, 127u);
                held[i] = held[--nheld];
            } else {
                i++;
            }
        }
        return;
    }
    for (k = 0; k < nt; k++) {
        const timbre_t *t = &tim[k];
        int out = (int)note + t->transpose;
        if (!takes(k, in_ch) || note > t->key_top || note < t->key_bottom || velocity > t->vel_top || velocity < t->vel_bottom)
            continue;
        if (out < 0 || out > 127 || nheld >= MAX_HELD || fm1b_queue_room() < 2u)
            continue;
        held[nheld].in_ch = (uint8_t)in_ch;
        held[nheld].in_note = (uint8_t)note;
        held[nheld].out_ch = (uint8_t)k;
        held[nheld].out_note = (uint8_t)out;
        nheld++;
        fm1b_channel_note(k, (unsigned)out, velocity, 127u);
    }
}

void m1_combi_control(unsigned in_ch, unsigned cc, unsigned value)
{
    unsigned k;
    if (cur < 0 || cc == 7u || cc == 10u || cc == 121u || phase != P_IDLE)
        return;                                            /* (level and pan are the combination's own) */
    for (k = 0; k < nt; k++)
        if (takes(k, in_ch) && fm1b_queue_room() >= 2u)
            fm1b_channel_control(k, cc, value);
    if (cc == 120u || cc == 123u)
        nheld = 0;                                         /* all notes were ended by the engine */
}

void m1_combi_bend(unsigned in_ch, unsigned value)
{
    unsigned k;
    if (cur < 0 || phase != P_IDLE)
        return;
    for (k = 0; k < nt; k++)
        if (takes(k, in_ch) && fm1b_queue_room() >= 2u)
            fm1b_channel_bend(k, value);
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* "FM-1 Setup": M1:s icke-standardinställningar, skilda från M1:s egna menyer (som i fm1-d50:s Setup-sida, issue #22). Här ligger bara
 * modellen: vilka värden som finns, deras gränser, hur de lagras i inställningsposten och hur de tillämpas. Hur sidan ritas och vilka
 * knappar som ändrar den ligger kvar i app.c (m1_setup_draw / input); när LCD-grafiken kommer byts bara ritningen ut.
 *
 * Lagring: `setup`-byten i settings_t, ett värde per fält i egna bitar. Fältet lagras som skillnad mot sitt standardvärde, så att 0
 * (en äldre post, som saknade byten) alltid är standard:
 *   bit 0..4  röstgräns: FM1B_VOICES minus antalet, 0..31 (standard 32 röster)
 *   bit 5..7  reserverade (nästa fält: kurva för tangenternas velocity, kanal-/MIDI-val ...)
 * Ett fält till läggs till i m1_setup_fields, i get/set/clean/apply och i bitkartan ovan. */
#pragma once
#include <stdint.h>

enum { M1_SETUP_VOICES, M1_SETUP_FIELDS };

typedef struct {
    const char *name;                    /* på skärmen, versaler */
    int16_t lo, hi;                      /* värdets gränser */
} m1_setup_field_t;

static const m1_setup_field_t m1_setup_fields[M1_SETUP_FIELDS] = {
    {"VOICES", 1, (int16_t)FM1B_VOICES}, /* röstgränsen: motorns tak, som CPU-vakten aldrig höjer över */
};

#define M1_SETUP_VOICES_MASK 0x1Fu       /* bit 0..4 */
#define M1_SETUP_USED (M1_SETUP_VOICES_MASK)

/* postens byte som den ska vara: okända bitar bort, röstgränsen inom 1..FM1B_VOICES */
static uint8_t m1_setup_clean(uint8_t setup)
{
    setup &= (uint8_t)M1_SETUP_USED;
    if ((setup & M1_SETUP_VOICES_MASK) > FM1B_VOICES - 1u)
        setup &= (uint8_t)~M1_SETUP_VOICES_MASK;                 /* ur gränserna: standard */
    return setup;
}

static int32_t m1_setup_get(uint8_t setup, uint32_t field)
{
    if (field == M1_SETUP_VOICES)
        return (int32_t)FM1B_VOICES - (int32_t)(setup & M1_SETUP_VOICES_MASK);
    return 0;
}

static uint8_t m1_setup_set(uint8_t setup, uint32_t field, int32_t value)
{
    if (field == M1_SETUP_VOICES) {
        if (value < m1_setup_fields[field].lo) value = m1_setup_fields[field].lo;
        if (value > m1_setup_fields[field].hi) value = m1_setup_fields[field].hi;
        setup = (uint8_t)((setup & ~M1_SETUP_VOICES_MASK) | ((uint32_t)FM1B_VOICES - (uint32_t)value));
    }
    return setup;
}

/* värdena till motorn: kallas vid start och efter varje ändring, från huvudloopen */
static void m1_setup_apply(uint8_t setup)
{
    sf2out_voice_cap((unsigned)m1_setup_get(setup, M1_SETUP_VOICES));
}

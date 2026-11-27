/* SPDX-License-Identifier: GPL-3.0-only */
/* "FM-1 Setup": the M1's non-standard settings, kept apart from the M1's own menus (as in fm1-d50's Setup page, issue #22). Only the model
 * lives here: which values exist, their limits, how they are stored in the settings record and how they are applied. How the page is drawn and which
 * buttons change it stays in app.c (m1_setup_draw / input); when the LCD graphics arrive only the drawing is replaced.
 *
 * Storage: the `setup` byte in settings_t, one value per field in bits of its own. A field is stored as the difference from its default, so that 0
 * (an older record, which lacked the byte) is always the default:
 *   bit 0..4  voice limit: FM1B_VOICES minus the count, 0..31 (default 32 voices)
 *   bit 5..7  reserved (next field: curve for the keys' velocity, channel/MIDI choice ...)
 * One more field is added to m1_setup_fields, to get/set/clean/apply and to the bit map above. */
#pragma once
#include <stdint.h>

enum { M1_SETUP_VOICES, M1_SETUP_FIELDS };

typedef struct {
    const char *name;                    /* on the screen, upper case */
    int16_t lo, hi;                      /* the value's limits */
} m1_setup_field_t;

static const m1_setup_field_t m1_setup_fields[M1_SETUP_FIELDS] = {
    {"VOICES", 1, (int16_t)FM1B_VOICES}, /* the voice limit: the engine's ceiling, which the CPU guard never raises above */
};

#define M1_SETUP_VOICES_MASK 0x1Fu       /* bit 0..4 */
#define M1_SETUP_USED (M1_SETUP_VOICES_MASK)

/* the record's byte as it should be: unknown bits removed, the voice limit within 1..FM1B_VOICES */
static uint8_t m1_setup_clean(uint8_t setup)
{
    setup &= (uint8_t)M1_SETUP_USED;
    if ((setup & M1_SETUP_VOICES_MASK) > FM1B_VOICES - 1u)
        setup &= (uint8_t)~M1_SETUP_VOICES_MASK;                 /* out of range: default */
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

/* the values to the engine: called at start and after every change, from the main loop */
static void m1_setup_apply(uint8_t setup)
{
    sf2out_voice_cap((unsigned)m1_setup_get(setup, M1_SETUP_VOICES));
}

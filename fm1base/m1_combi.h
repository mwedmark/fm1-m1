/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* The M1's combinations (m1_combi.c): up to 8 timbres, each a program with level, pan, transpose, key and velocity range, played on
 * the engine's channels 0..7. The table is in the bank (tools/m1_combi.py, "M1CB"); m1_combi_init finds it.
 *
 * Everything that writes to the engine (fm1b_*) runs from the main loop only: m1_combi_service() must be called there every pass. */

#define M1_COMBI_GLOBAL 0xFFu            /* input channel for the panel's keys: reaches every timbre, also in Multi */

void m1_combi_init(const uint8_t *bank, uint32_t bytes);   /* looks for the table; none: m1_combi_count() is 0 */
unsigned m1_combi_count(void);
void m1_combi_name(unsigned index, char out[11]);          /* 10 characters, NUL terminated */
unsigned m1_combi_type(unsigned index);                    /* 0 single, 1 layer, 2 split, 3 velocity switch, 4 multi */
unsigned m1_combi_timbres(unsigned index);                 /* the timbres that are on */

int m1_combi_select(unsigned index);                       /* make it the active combination (set up on the following service calls); -1 if none */
void m1_combi_leave(void);                                 /* back to a single program: the channels follow the panel's program again */
int m1_combi_active(void);                                 /* the index, or -1 in program mode */
unsigned m1_combi_busy(void);                              /* 1 while a select or leave is still being applied */

void m1_combi_service(void);                               /* main loop: applies a select/leave, as much as the engine's queue has room for */

/* in_ch: the MIDI channel 0..15, or M1_COMBI_GLOBAL. velocity 0 is a note off. A note that arrives while no combination is active is ignored. */
void m1_combi_note(unsigned in_ch, unsigned note, unsigned velocity);
void m1_combi_control(unsigned in_ch, unsigned controller, unsigned value);   /* wheel, pedal, sends, all notes off to the timbres' channels; 7 and 10 are the combination's own */
void m1_combi_bend(unsigned in_ch, unsigned value);

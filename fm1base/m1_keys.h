/* SPDX-License-Identifier: GPL-3.0-only */
/* The link between the FM-1's buttons and the M1's keys. One table: which M1 function a button stands for, what it is called, what it does, and
 * whether the firmware does it yet. Part of app.c's unity build (B_* come from plat.h).
 *
 * Like the M1 (docs/M1_LCD.md, "the key map") a button acts when it is RELEASED. Held for M1_HELP_MS or more it shows its function on the LCD
 * for a short while instead, and does nothing when released.
 *
 * The M1 positions (row, column in the panel matrix) are noted for each function that has been found, so that the firmware's pages can be checked
 * against the OS's own (tools/m1_lcd_emu.py --tap ROW,COL). The FM-1 has no key for the digits, PAGE, +1/-1 or the eight quick parameters: those
 * are on the encoders (PRESET is +1/-1, SELECT will be PAGE) and the keys' numbers are not used yet. */
#pragma once
#include <stdint.h>

#define M1_HELP_MS 3000u                 /* held this long: show what the button is */
#define M1_HELP_SHOW_MS 2500u            /* and for this long */

enum { M1K_NONE, M1K_PROG, M1K_COMBI, M1K_SEQ, M1K_GLOBAL, M1K_EDIT, M1K_SETUP, M1K_SAVE, M1K_OCTDN, M1K_OCTUP };

typedef struct {
    const char *fm;                      /* the button's name on the FM-1 */
    const char *m1;                      /* the M1's key, or 0 for a button that is the FM-1's own */
    const char *what;                    /* one line (at most 40 characters with the " (soon)" that is added when done is 0) */
    uint8_t fn;                          /* M1K_* */
    uint8_t done;                        /* the firmware does it */
} m1_keymap_t;

static const m1_keymap_t m1_keymap[NB] = {
    [B_HOME] = {"HOME", "PROG", "Program play mode", M1K_PROG, 1},                   /* M1 1,2 */
    [B_PLAY] = {"PLAY", "COMBI", "Combination play mode", M1K_COMBI, 0},             /* M1 0,4 */
    [B_EDIT] = {"EDIT", "EDIT", "Edit the program or combination", M1K_EDIT, 0},    /* M1 1,3 (program) and 0,5 (combination) */
    [B_GLO] = {"GLO", "GLOBAL", "Global settings", M1K_GLOBAL, 1},                  /* M1 1,5 */
    [B_SEQ] = {"SEQ", "SEQ", "Sequencer mode", M1K_SEQ, 0},                         /* M1: position not found yet */
    [B_ARP] = {"ARP", 0, "FM-1 setup: voices, view, color", M1K_SETUP, 1},
    [B_SAVE] = {"SAVE", 0, "Keeps the FM-1 settings", M1K_SAVE, 1},
    [B_OCTDN] = {"OCT-", 0, "Octave down for the keys", M1K_OCTDN, 1},
    [B_OCTUP] = {"OCT+", 0, "Octave up for the keys", M1K_OCTUP, 1},
    [B_FX] = {"FX", 0, "No M1 function yet", M1K_NONE, 1},
    [B_SEL] = {"SEL", 0, "No M1 function yet", M1K_NONE, 1},
    [B_ENV] = {"ENV", 0, "No M1 function yet", M1K_NONE, 1},
    [B_LFO] = {"LFO", 0, "No M1 function yet", M1K_NONE, 1},
    [B_REC] = {"REC", 0, "No M1 function yet", M1K_NONE, 1},
};

static void m1_keys_put(char *row, uint32_t col, const char *s)    /* text into a 40-character row, clipped */
{
    uint32_t i;
    for (i = 0; s && s[i] && col + i < 40u; i++)
        row[col + i] = s[i];
}

/* the two rows of the help for button k: `HOME = M1 PROG` / `Program play mode` (`ARP  FM-1 key` for a button of the FM-1's own) */
static void m1_keys_help(uint32_t k, char row[2][40])
{
    const m1_keymap_t *m = &m1_keymap[k < NB ? k : 0];
    uint32_t i;
    for (i = 0; i < 40u; i++)
        row[0][i] = row[1][i] = ' ';
    m1_keys_put(row[0], 0, m->fm);
    if (m->m1) {
        m1_keys_put(row[0], 5, "= M1");
        m1_keys_put(row[0], 10, m->m1);
    } else {
        m1_keys_put(row[0], 5, "FM-1 key");
    }
    m1_keys_put(row[1], 0, m->what);
    if (!m->done)
        m1_keys_put(row[1], 40u - 7u, "(soon)");
}

/* what a button that the firmware does not do yet shows when it is pressed and released briefly: `COMBI` / `not done yet` */
static void m1_keys_notyet(uint32_t k, char row[2][40])
{
    const m1_keymap_t *m = &m1_keymap[k < NB ? k : 0];
    uint32_t i;
    for (i = 0; i < 40u; i++)
        row[0][i] = row[1][i] = ' ';
    m1_keys_put(row[0], 0, m->m1 ? m->m1 : m->fm);
    m1_keys_put(row[1], 5, "not done yet");
}

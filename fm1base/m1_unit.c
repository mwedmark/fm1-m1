/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* M1:s enda -O2-enhet (fm1.toml o2): fm1-sf2:s sampelmotor (engine/, kopierad från fm1-sf2 5a4f267). Här och inte i plattformens
 * unity-build, där deras statiska namn skulle krocka med plattformens. */
#include "../engine/fm1b.c"

/* SEA5-tabellen till fm1b_bind (fm1b_dqt.inc är static, så den ges ut härifrån) */
#include "../engine/fm1b_dqt.inc"
const int16_t *m1_sea5_dqt(void) { return fm1b_dqt; }

/* minimp3 (live_cache/mp3_sound.c) behöver memmove; felucca-bsp:s libc har bara memset, memcpy och memcmp (host och webb har sin egen) */
#ifndef PLAT_HOST
void *memmove(void *d, const void *s, unsigned n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
#endif

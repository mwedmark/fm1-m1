/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The M1's only -O2 unit (fm1.toml o2): fm1-sf2's sample engine (engine/, copied from fm1-sf2 5a4f267). Here and not in the platform's
 * unity build, where their static names would collide with the platform's. */
#include "../engine/fm1b.c"

/* the SEA5 table for fm1b_bind (fm1b_dqt.inc is static, so it is handed out from here) */
#include "../engine/fm1b_dqt.inc"
const int16_t *m1_sea5_dqt(void) { return fm1b_dqt; }

/* minimp3 (live_cache/mp3_sound.c) needs memmove; felucca-bsp's libc only has memset, memcpy and memcmp (host and web have their own) */
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

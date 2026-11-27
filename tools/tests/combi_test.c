/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of fm1base/m1_combi.c against a stub engine: tools/tests/test_m1_combi.py builds the bank image (synthetic data, nothing of Korg's)
 * and runs this with its path.  cc -Iengine fm1base/m1_combi.c tools/tests/combi_test.c -o combi_test && ./combi_test bank.bin */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fm1base/m1_combi.h"
#include "fm1b.h"

typedef struct { char kind; unsigned ch, a, b, c; } ev_t;
static ev_t ev[512];
static unsigned nev, room = 32;
static int fails;

unsigned fm1b_queue_room(void) { return room; }
void fm1b_channel_note(unsigned ch, unsigned note, unsigned vel, unsigned lvl) { (void)lvl; ev[nev++] = (ev_t){'n', ch, note, vel, 0}; }
void fm1b_channel_program(unsigned ch, unsigned bank, unsigned prog) { ev[nev++] = (ev_t){'p', ch, bank, prog, 0}; }
void fm1b_channel_follow(unsigned ch) { ev[nev++] = (ev_t){'f', ch, 0, 0, 0}; }
void fm1b_channel_control(unsigned ch, unsigned cc, unsigned v) { ev[nev++] = (ev_t){'c', ch, cc, v, 0}; }
void fm1b_channel_bend(unsigned ch, unsigned v) { ev[nev++] = (ev_t){'b', ch, v, 0, 0}; }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static unsigned count_kind(char k) { unsigned i, n = 0; for (i = 0; i < nev; i++) n += ev[i].kind == k; return n; }
static void settle(void) { unsigned g; for (g = 0; g < 50 && m1_combi_busy(); g++) m1_combi_service(); CHECK(!m1_combi_busy()); }
static int has_note(unsigned ch, unsigned note, unsigned vel) { unsigned i; for (i = 0; i < nev; i++) if (ev[i].kind == 'n' && ev[i].ch == ch && ev[i].a == note && ev[i].b == vel) return 1; return 0; }

int main(int argc, char **argv)
{
    static uint8_t bank[65536];
    size_t n;
    char nm[11];
    FILE *f;
    if (argc < 2) return 2;
    f = fopen(argv[1], "rb");
    if (!f) return 2;
    n = fread(bank, 1, sizeof bank, f);
    fclose(f);

    m1_combi_init(bank, (uint32_t)n);
    CHECK(m1_combi_count() == 5);
    m1_combi_name(1, nm);
    CHECK(strcmp(nm, "Layer Test") == 0);
    CHECK(m1_combi_type(2) == 2 && m1_combi_timbres(2) == 2);
    CHECK(m1_combi_timbres(0) == 1);          /* the OFF timbre of combination 0 is left out */
    CHECK(m1_combi_select(99) == -1);
    CHECK(m1_combi_active() == -1);

    /* notes while no combination is active: nothing */
    m1_combi_note(M1_COMBI_GLOBAL, 60, 100);
    CHECK(nev == 0);

    /* 1: layer, two timbres: program 0 at level 99 pan 8, program 1 at level 50 transpose +12 */
    CHECK(m1_combi_select(1) == 0);
    settle();
    CHECK(m1_combi_active() == 1);
    CHECK(count_kind('p') == 2);
    CHECK(ev[1].kind == 'p' && ev[1].ch == 0 && ev[1].a == 0 && ev[1].b == 0);   /* after CC 121: bank 0, program 0 */
    {   unsigned i, ok0 = 0, ok1 = 0;
        for (i = 0; i < nev; i++) {
            if (ev[i].kind == 'p' && ev[i].ch == 0 && ev[i].b == 0) ok0 = 1;
            if (ev[i].kind == 'p' && ev[i].ch == 1 && ev[i].b == 1) ok1 = 1;
            if (ev[i].kind == 'c' && ev[i].ch == 1 && ev[i].a == 7) CHECK(ev[i].b == 64);   /* 50 * 127 / 99 = 64 */
            if (ev[i].kind == 'c' && ev[i].ch == 0 && ev[i].a == 7) CHECK(ev[i].b == 127);
        }
        CHECK(ok0 && ok1);
    }
    nev = 0;
    m1_combi_note(M1_COMBI_GLOBAL, 60, 100);
    CHECK(has_note(0, 60, 100) && has_note(1, 72, 100) && count_kind('n') == 2);
    nev = 0;
    m1_combi_note(M1_COMBI_GLOBAL, 60, 0);
    CHECK(has_note(0, 60, 0) && has_note(1, 72, 0) && count_kind('n') == 2);
    nev = 0;
    m1_combi_note(M1_COMBI_GLOBAL, 60, 0);          /* a second off does nothing */
    CHECK(nev == 0);

    /* 2: split at key 60: program 1 below (0..59), program 2 above (60..127) */
    CHECK(m1_combi_select(2) == 0); settle(); nev = 0;
    m1_combi_note(0, 59, 90);
    CHECK(count_kind('n') == 1 && has_note(0, 59, 90));
    nev = 0;
    m1_combi_note(0, 60, 90);
    CHECK(count_kind('n') == 1 && has_note(1, 60, 90));
    m1_combi_note(0, 59, 0); m1_combi_note(0, 60, 0);

    /* 3: velocity switch: soft (1..63) on timbre 0, hard (64..127) on timbre 1 */
    CHECK(m1_combi_select(3) == 0); settle(); nev = 0;
    m1_combi_note(0, 60, 63); CHECK(has_note(0, 60, 63) && count_kind('n') == 1);
    nev = 0; m1_combi_note(0, 60, 64); CHECK(has_note(1, 60, 64) && count_kind('n') == 1);
    m1_combi_note(0, 60, 0); m1_combi_note(0, 60, 0);

    /* 4: multi: timbre 0 on MIDI channel 2, timbre 1 on channel 5; the panel reaches both; drum channel 10 (9) none */
    CHECK(m1_combi_select(4) == 0); settle(); nev = 0;
    m1_combi_note(2, 60, 80);  CHECK(has_note(0, 60, 80) && count_kind('n') == 1);
    nev = 0; m1_combi_note(5, 61, 80); CHECK(has_note(1, 61, 80) && count_kind('n') == 1);
    nev = 0; m1_combi_note(9, 62, 80); CHECK(count_kind('n') == 0);
    nev = 0; m1_combi_note(M1_COMBI_GLOBAL, 63, 80); CHECK(count_kind('n') == 2);
    nev = 0; m1_combi_control(2, 64, 127); CHECK(count_kind('c') == 1 && ev[0].ch == 0);
    nev = 0; m1_combi_control(2, 7, 10); CHECK(nev == 0);       /* volume is the combination's own */
    nev = 0; m1_combi_bend(5, 9000); CHECK(count_kind('b') == 1 && ev[0].ch == 1);

    /* a note on the combination and a select: the held note gets its off first, even though the new combination differs */
    nev = 0; m1_combi_note(M1_COMBI_GLOBAL, 70, 100);
    CHECK(count_kind('n') == 2);
    nev = 0; CHECK(m1_combi_select(0) == 0); settle();
    CHECK(count_kind('n') >= 3 && has_note(0, 70, 0) && has_note(1, 70, 0));      /* offs for 63 (two) and 70 (two): 63 still held too */

    /* the queue is short of room: nothing is lost, the service waits */
    nev = 0; room = 3; CHECK(m1_combi_select(1) == 0);
    m1_combi_service(); CHECK(m1_combi_busy());
    room = 32; settle(); CHECK(m1_combi_active() == 1);

    /* leave: the channels follow the panel again */
    nev = 0; m1_combi_leave(); settle();
    CHECK(m1_combi_active() == -1 && count_kind('f') == 8);
    nev = 0; m1_combi_note(M1_COMBI_GLOBAL, 60, 100); CHECK(nev == 0);

    /* a bank with the effect table last but no combination table */
    {   static uint8_t plain[64]; memset(plain, 0, sizeof plain);
        memcpy(plain + 56, "\x02\x00\x14\x00M1FX", 8);
        m1_combi_init(plain, 64);
        CHECK(m1_combi_count() == 0);
    }
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails != 0;
}

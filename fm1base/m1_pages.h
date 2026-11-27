/* SPDX-License-Identifier: GPL-3.0-only */
/* The M1's pages (Global, to start with) as the firmware draws and edits them. The pages themselves, their texts and their values, are not in this
 * file: they are read out of the OS of your own M1 by tools/m1_pages.py into a table in the resource region ("M1PG" at the end of the bank), as the
 * effect table is. Without the table there are no pages. Part of app.c's unity build.
 *
 * A page is its 40 x 2 screen as the OS first draws it and its parameters. A parameter has a cursor position, a label that appears on the page while
 * the parameter is selected, a slot of the screen with the text of its present value, and the list of the values' texts. Its present value is
 * kept in the settings by id (index + 1; 0 = the initial value the OS has). Format: tools/m1_pages.py. */
#pragma once
#include <stdint.h>

#define M1_PG_MAX_PAGES 24u
#define M1_PG_MAX_PARAMS 48u
#define M1_PG_IDS 32u                    /* settings slots */

typedef struct {
    const uint8_t *label;                /* text put at lab_start while selected */
    const uint8_t *vals;                 /* nvals texts of slot_w bytes */
    uint8_t cursor, lab_start, lab_w, slot_start, slot_w, nvals, init, wraps, id;
} m1_pg_param_t;

static uint8_t m1_pg_pages;
static const uint8_t *m1_pg_screen[M1_PG_MAX_PAGES];
static uint8_t m1_pg_first[M1_PG_MAX_PAGES], m1_pg_count[M1_PG_MAX_PAGES];
static m1_pg_param_t m1_pg_par[M1_PG_MAX_PARAMS];
static const m1_pg_param_t *m1_pg_by_id[M1_PG_IDS];

/* Finds the table at the end of the resource and returns the length that is left for the bank (and what follows it). Without a table: n, no pages. */
static uint32_t m1_pg_init(const uint8_t *res, uint32_t n)
{
    uint32_t len, i, np = 0, pg;
    const uint8_t *b, *e;
    m1_pg_pages = 0;
    for (i = 0; i < M1_PG_IDS; i++)
        m1_pg_by_id[i] = 0;
    if (!res || n < 16u || res[n - 4] != 'M' || res[n - 3] != '1' || res[n - 2] != 'P' || res[n - 1] != 'G')
        return n;
    len = res[n - 8] | (uint32_t)res[n - 7] << 8 | (uint32_t)res[n - 6] << 16 | (uint32_t)res[n - 5] << 24;
    if (len < 2u || len + 8u > n)
        return n;
    b = res + n - 8u - len;
    e = b + len;
    if (b[0] != 1u || b[1] == 0 || b[1] > M1_PG_MAX_PAGES)
        return n;
    pg = b[1];
    b += 2;
    for (i = 0; i < pg; i++) {
        uint32_t k, nparam;
        if (b + 81 > e)
            return n;
        nparam = b[0];
        m1_pg_screen[i] = b + 1;
        m1_pg_first[i] = (uint8_t)np;
        m1_pg_count[i] = (uint8_t)nparam;
        b += 81;
        if (np + nparam > M1_PG_MAX_PARAMS)
            return n;
        for (k = 0; k < nparam; k++) {
            m1_pg_param_t *p = &m1_pg_par[np++];
            if (b + 3 > e)
                return n;
            p->cursor = b[0];
            p->lab_start = b[1];
            p->lab_w = b[2];
            p->label = b + 3;
            b += 3u + p->lab_w;
            if (b + 6 > e)
                return n;
            p->slot_start = b[0];
            p->slot_w = b[1];
            p->nvals = b[2];
            p->init = b[3];
            p->wraps = b[4];
            p->id = b[5];
            p->vals = b + 6;
            b += 6u + (uint32_t)p->nvals * p->slot_w;
            if (b > e || p->nvals == 0 || p->init >= p->nvals || p->slot_start + p->slot_w > 80u || p->lab_start + p->lab_w > 80u)
                return n;
            if (p->id < M1_PG_IDS)
                m1_pg_by_id[p->id] = p;
        }
    }
    m1_pg_pages = (uint8_t)pg;
    return n - 8u - len;
}

/* the present value of parameter p: g[id] holds index + 1, 0 the OS's initial value */
static uint32_t m1_pg_value(const uint8_t *g, const m1_pg_param_t *p)
{
    uint32_t v = p->id < M1_PG_IDS ? g[p->id] : 0u;
    return v && v - 1u < p->nvals ? v - 1u : p->init;
}

/* the text of value i of p, converted to a number: its sign and digits (spaces before them are left out) */
static int32_t m1_pg_number(const m1_pg_param_t *p, uint32_t i)
{
    const uint8_t *t = p->vals + (uint32_t)(i < p->nvals ? i : 0u) * p->slot_w;
    uint32_t k = 0;
    int32_t v = 0, neg = 0;
    while (k < p->slot_w && t[k] == ' ')
        k++;
    if (k < p->slot_w && (t[k] == '+' || t[k] == '-'))
        neg = t[k++] == '-';
    for (; k < p->slot_w && t[k] >= '0' && t[k] <= '9'; k++)
        v = v * 10 + (t[k] - '0');
    return neg ? -v : v;
}

/* one step of p's value: dir +1 / -1 (the OS's wrap or clamp); returns 1 if the value changed */
static int m1_pg_step(uint8_t *g, const m1_pg_param_t *p, int32_t dir)
{
    int32_t v = (int32_t)m1_pg_value(g, p), n = (int32_t)p->nvals, w = v + dir;
    if (w < 0 || w >= n)
        w = p->wraps ? (w + n) % n : v;
    if (w == v || p->id >= M1_PG_IDS)
        return 0;
    g[p->id] = (uint8_t)(w + 1);
    return 1;
}

/* page `page` into row[2][40] with parameter `sel` selected: the picture, every parameter's present value, the selected one's label.
 * Returns the cursor (0..79), or -1 if the page has no parameters. */
static int32_t m1_pg_render(uint32_t page, uint32_t sel, const uint8_t *g, char row[2][40])
{
    const m1_pg_param_t *p;
    uint32_t i, k, f, n;
    char *flat = &row[0][0];
    if (page >= m1_pg_pages)
        return -1;
    for (i = 0; i < 80u; i++)
        flat[i] = (char)m1_pg_screen[page][i];
    f = m1_pg_first[page];
    n = m1_pg_count[page];
    for (i = 0; i < n; i++) {
        p = &m1_pg_par[f + i];
        for (k = 0; k < p->slot_w; k++)
            flat[p->slot_start + k] = (char)p->vals[m1_pg_value(g, p) * p->slot_w + k];
    }
    if (!n)
        return -1;
    p = &m1_pg_par[f + (sel < n ? sel : 0u)];
    for (k = 0; k < p->lab_w; k++)
        flat[p->lab_start + k] = (char)p->label[k];
    return p->cursor;
}

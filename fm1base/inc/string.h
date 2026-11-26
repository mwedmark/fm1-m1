/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-base har inga libc-huvuden: felucca-bsp:s chip/libc.c definierar memset, memcpy och memcmp (som fm1-d50/fm1base/inc). */
#pragma once
#include <stddef.h>
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
void *memmove(void *d, const void *s, unsigned n);

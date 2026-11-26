/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* M1:s delay på hela utgången (m1_fx.c): lr är n stereobildrutor, Q23, som sf2out_render lämnar dem */
void m1_fx_render(int32_t *lr, uint32_t n);
/* slutlimitern: lr efter motorn och delayen, före MASTER (m1_fx.c). m1_limit_cut: bildrutor som den sänkt, sedan start */
void m1_limit_render(int32_t *lr, uint32_t n);
void m1_limit_post(int32_t *lr, uint32_t n);    /* samma limiter efter delayen */
uint32_t m1_limit_cut(void);

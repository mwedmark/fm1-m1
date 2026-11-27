/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* The M1's delay on the whole output (m1_fx.c): lr is n stereo frames, Q23, as sf2out_render leaves them */
void m1_fx_render(int32_t *lr, uint32_t n);
/* the final limiter: lr after the engine and the delay, before MASTER (m1_fx.c). m1_limit_cut: frames it has reduced, since start */
void m1_limit_render(int32_t *lr, uint32_t n);
void m1_limit_post(int32_t *lr, uint32_t n);    /* the same limiter after the delay */
uint32_t m1_limit_cut(void);

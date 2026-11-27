/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* The M1's program effects that the engine lacks (m1_fx2.c): tremolo, overdrive, distortion, exciter, symphonic ensemble, flanger and rotary
 * speaker. The program's effect data is in a table at the end of the bank (tools/m1_fxtable.py); m1_fx2_init looks it up. */
void m1_fx2_init(const uint8_t *bank, uint32_t bytes);
/* lr: n stereo frames, Q23, after the engine and the delay; the effects of the most recently started program are applied */
void m1_fx2_render(int32_t *lr, uint32_t n);

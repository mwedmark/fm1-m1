/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* M1:s programeffekter som motorn inte har (m1_fx2.c): tremolo, overdrive, distortion, exciter, symphonic ensemble, flanger och rotary
 * speaker. Programmets effektdata ligger i en tabell sist i banken (tools/m1_fxtable.py); m1_fx2_init letar upp den. */
void m1_fx2_init(const uint8_t *bank, uint32_t bytes);
/* lr: n stereobildrutor, Q23, efter motorn och delayen; effekterna för det senast startade programmet läggs på */
void m1_fx2_render(int32_t *lr, uint32_t n);

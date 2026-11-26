/* SPDX-License-Identifier: GPL-3.0-only */
/* Motorns konfiguration, samma i varje enhet (fm1.toml o2_include) och i app.c: live cache (fm1-sf2 FM1B_PACKED), banken BFP8 och
 * MP3 i 32 kHz (pack_bank.js). Poolen ligger i fm1-base:s .pool (app.c), som har långt mer plats än fm1_open:s heap. */
#pragma once
#define FM1B_PACKED 1
#define FM1B_BFP8_ONLY 1
#define FM1B_CACHE_KB 192
#define FM1B_CACHE_FRAMES 2U
#define FM1B_WARM 0
#define FM1B_DROP_HEADS 0
#define FM1B_VOICES 32U                          /* 40 gav ett missat ljudblock med 40 toner (2026-11-25); M1 har 16 */

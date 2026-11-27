/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine's configuration, the same in every unit (fm1.toml o2_include) and in app.c: live cache (fm1-sf2 FM1B_PACKED), the bank BFP8 and
 * MP3 at 32 kHz (pack_bank.js). The pool lives in fm1-base's .pool (app.c), which has far more room than fm1_open's heap. */
#pragma once
#define FM1B_PACKED 1
#define FM1B_BFP8_ONLY 1
#define FM1B_CACHE_KB 192
#define FM1B_CACHE_FRAMES 2U
#define FM1B_WARM 0
#define FM1B_DROP_HEADS 0
#define FM1B_VOICES 32U                          /* 40 gave a missed audio block with 40 notes (2026-11-25); the M1 has 16 */

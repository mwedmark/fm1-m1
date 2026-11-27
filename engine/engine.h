/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* fm1_open's engine.h, replaced for fm1-base: fm1b.c, sf2fx.c and sf2out.c only use the section macros and the block size.
 * FM1_DSP_CODE is empty for now: the engine runs from XIP. Moving it to RAM (PLAT_RAM_CODE) requires that it only
 * calls RAM code (fm1-base tools/ramcode.py), as fm1-d50 did with D5_HOT. */
#ifndef FM1_ENGINE_H
#define FM1_ENGINE_H
#define FM1_DSP_CODE
#define FM1_DSP_DATA
#define FM1_BLOCK_FRAMES 128U   /* = APP_BLOCK in fm1base/app.h */
#endif

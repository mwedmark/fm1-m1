/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* fm1_open:s engine.h ersatt för fm1-base: fm1b.c, sf2fx.c och sf2out.c använder bara sektionsmakrona och blockstorleken.
 * FM1_DSP_CODE är tom än så länge: motorn körs från XIP. Att flytta den till RAM (PLAT_RAM_CODE) kräver att den bara
 * anropar RAM-kod (fm1-base tools/ramcode.py), som fm1-d50 gjorde med D5_HOT. */
#ifndef FM1_ENGINE_H
#define FM1_ENGINE_H
#define FM1_DSP_CODE
#define FM1_DSP_DATA
#define FM1_BLOCK_FRAMES 128U   /* = APP_BLOCK i fm1base/app.h */
#endif

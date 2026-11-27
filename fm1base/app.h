/* SPDX-License-Identifier: GPL-3.0-only */
/* M1 on fm1-base: the app's half of the contract with the platform (fm1-base device/unity.c). The layout is fm1-d50's. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "M1"
#define APP_SPLASH_RGB RGB(230, 230, 230)
#define APP_BLOCK 128u                   /* = FM1_BLOCK_FRAMES in engine/engine.h */
#define APP_TOUCH                        /* strike from the keys (lib/touch.h) */

/* the store: the settings and the panel's calibration. The data area has the bank from data_lo (fm1.toml) up to 0xDE000 */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDE000u}

/* the sound bank (FM1B) as a resource region: a 32-byte header and the bank in 170 sectors from 0x34000 to 0xDE000 (696 288 B bank) */
#define APP_RES_BASE 0x34000u
#define APP_RES_SECTORS 170u
#define APP_RESTART 1
#define APP_BACKUP_OBJS {OBJ_SETTINGS}
#define APP_BACKUP_MAGIC 0x3130314Du            /* "M101" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* measurement over USB (app.c app_sysex): F0 7D 'M' '1' 01 F7 answers F0 7D 'M' '1' 41, then 16 values of 5 x 7 bits each, F7 */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

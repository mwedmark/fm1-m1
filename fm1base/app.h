/* SPDX-License-Identifier: GPL-3.0-only */
/* M1 på fm1-base: appens halva av kontraktet med plattformen (fm1-base device/unity.c). Layouten är fm1-d50:s. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "M1"
#define APP_SPLASH_RGB RGB(230, 230, 230)
#define APP_BLOCK 128u                   /* = FM1_BLOCK_FRAMES i engine/engine.h */
#define APP_TOUCH                        /* anslag från tangenterna (lib/touch.h) */

/* lagret: inställningarna och panelens kalibrering. Dataområdet har banken från data_lo (fm1.toml) upp till 0xDE000 */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDE000u}

/* ljudbanken (FM1B) som resursregion: 32 byte huvud och banken i 170 sektorer från 0x34000 till 0xDE000 (696 288 B bank) */
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

/* mätning över USB (app.c app_sysex): F0 7D 'M' '1' 01 F7 svarar F0 7D 'M' '1' 41, sedan 16 värden à 5 x 7 bitar, F7 */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/*
 *   This file is part of Presence3DS
 *   Copyright (C) 2026 LeonLeBreton
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#pragma once

#include <3ds/types.h>
#include <3ds/result.h>
#include <stdbool.h>

#define USER_PREFS_PATH "/presence3ds/user_prefs.conf"

// Identifier for each user preference (also used as index into g_user_prefs)
typedef enum {
    PREFS_HIDE_MII,
    PREFS_HIDE_HOME,
    PREFS_AUTO_START,
    PREFS_FORCE_ENGLISH,
    PREFS_ALLOW_UNSAFE,
    PREFS_DISABLE_CUSTOMRPC,
    PREFS_COUNT
} UserPrefId;

// Metadata describing a single user preference
typedef struct {
    const char *key;   // key in the config file (e.g. "HIDE_MII")
    const char *label; // label shown in the menu (e.g. "Hide Mii in Presence")
    bool        def;   // default value
} UserPrefMeta;

// One entry per preference

// Metadatas of the preferences (key, label, default value)
extern const UserPrefMeta g_user_prefs[PREFS_COUNT];

// Values of the preferences (true/false)
extern bool g_pref_values[PREFS_COUNT];

extern bool g_prefs_loaded;

// Load preferences from SD card (or apply defaults if file missing)
Result UserPrefs_Load(void);

// Save current preferences to SD card
Result UserPrefs_Save(void);
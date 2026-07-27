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

#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include "discord/user_prefs.h"
#include "discord/utils/config_reader.h"
#include "discord/discord_log.h"

bool g_pref_hide_mii = false;    // Default: don't hide Mii info
bool g_pref_hide_home = false;  // Default: don't hide Home Menu activity
bool g_pref_auto_start = false; // Default: manual start only
bool g_prefs_loaded = false;

// Helper: parse "true"/"false" (case-insensitive) into *out
static bool parse_bool(const char *val, bool *out)
{
    // Skip leading whitespace
    while(*val == ' ' || *val == '\t') val++;

    if(strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0)
    {
        *out = true;
        return true;
    }
    if(strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

// Callback for ConfigReader_Parse
static bool prefs_handler(const char *key, const char *value, void *userdata)
{
    (void)userdata;

    if(strcasecmp(key, "HIDE_MII") == 0)
        parse_bool(value, &g_pref_hide_mii);
    else if(strcasecmp(key, "HIDE_HOME") == 0)
        parse_bool(value, &g_pref_hide_home);
    else if(strcasecmp(key, "AUTO_START_AT_BOOT") == 0)
        parse_bool(value, &g_pref_auto_start);

    return true; // continue parsing
}

Result UserPrefs_Load(void)
{
    // Reset to defaults first
    g_pref_hide_mii = false;
    g_pref_hide_home = false;
    g_pref_auto_start = false;

    Result res = ConfigReader_Parse(USER_PREFS_PATH, prefs_handler, NULL);
    if(R_FAILED(res))
    {
        // File missing is not an error — defaults are already applied
        DiscordLog_Printf("[PREFS] Can't open " USER_PREFS_PATH
            " (0x%08lx), using defaults\n", (u32)res);
        g_prefs_loaded = false;
        return 0; // Success with defaults
    }

    g_prefs_loaded = true;
    DiscordLog_Printf("[PREFS] Loaded: hide_mii=%d hide_home=%d auto_start=%d\n",
        g_pref_hide_mii, g_pref_hide_home, g_pref_auto_start);
    return 0;
}

Result UserPrefs_Save(void)
{
    Result res;
    Handle fileHandle;

    // Create or truncate the file
    res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
                                  fsMakePath(PATH_EMPTY, ""),
                                  fsMakePath(PATH_ASCII, USER_PREFS_PATH),
                                  FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[PREFS] Can't create " USER_PREFS_PATH " (0x%08lx)\n", (u32)res);
        return res;
    }

    // Build content in a buffer
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "HIDE_MII=%s\n"
        "HIDE_HOME=%s\n"
        "AUTO_START_AT_BOOT=%s\n",
        g_pref_hide_mii ? "true" : "false",
        g_pref_hide_home ? "true" : "false",
        g_pref_auto_start ? "true" : "false");

    // Write to file
    u32 written;
    res = FSFILE_Write(fileHandle, &written, 0, buf, len, FS_WRITE_FLUSH);
    FSFILE_Close(fileHandle);

    if(R_FAILED(res))
    {
        DiscordLog_Printf("[PREFS] Write failed (0x%08lx)\n", (u32)res);
        return res;
    }

    DiscordLog_Printf("[PREFS] Saved (%d bytes): hide_mii=%d hide_home=%d auto_start=%d\n",
        len, g_pref_hide_mii, g_pref_hide_home, g_pref_auto_start);
    return 0;
}
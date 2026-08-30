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
#include <3ds.h>
#include "discord/utils/printf.h"
#include "discord/user_prefs.h"
#include "discord/utils/config_reader.h"
#include "discord/discord_log.h"

// All user preferences:
// key (config file), label (menu), default value.
// Empty label means the preference is hidden in the menu.
const UserPrefMeta g_user_prefs[PREFS_COUNT] = {
    [PREFS_HIDE_MII]     = { "HIDE_MII",           "Hide Mii in Presence", false },
    [PREFS_HIDE_HOME]    = { "HIDE_HOME",          "Hide Home activity",   false },
    [PREFS_AUTO_START]   = { "AUTO_START_AT_BOOT", "Auto-start at boot",   false },
    [PREFS_FORCE_ENGLISH] = { "FORCE_ENGLISH",     "Force English name of the game", false },
    [PREFS_DISABLE_CUSTOMRPC] = { "DISABLE_CUSTOMRPC", "Disable displaying game state", false },
    [PREFS_ALLOW_UNSAFE] = { "ALLOW_UNSAFE",       "",                     false },
};

bool g_pref_values[PREFS_COUNT] = { false };
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

// Callback for ConfigReader_Parse using the metadata table
static bool prefs_handler(const char *key, const char *value, void *userdata)
{
    (void)userdata;

    for(u32 i = 0; i < PREFS_COUNT; i++)
    {
        if(strcasecmp(key, g_user_prefs[i].key) == 0)
        {
            parse_bool(value, &g_pref_values[i]);
            break;
        }
    }

    return true; // continue parsing
}

Result UserPrefs_Load(void)
{
    // Reset to defaults first
    for(u32 i = 0; i < PREFS_COUNT; i++)
        g_pref_values[i] = g_user_prefs[i].def;

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
    DiscordLog_Printf("[PREFS] Loaded");
    for(u32 i = 0; i < PREFS_COUNT; i++)
        DiscordLog_Printf(" %s=%d", g_user_prefs[i].key, g_pref_values[i]);
    DiscordLog_Printf("\n");
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
    int len = 0;
    int n = 0;
    for(u32 i = 0; i < PREFS_COUNT && len < (int)sizeof(buf); i++)
    {
        n = snprintf(buf + len, sizeof(buf) - len, "%s=%s\n",
                     g_user_prefs[i].key,
                     g_pref_values[i] ? "true" : "false");
        if(n < 0)
        {
            res = -1;
            FSFILE_Close(fileHandle);
            return res;
        }
        len += n;
    }

    // Write to file
    u32 written;
    res = FSFILE_Write(fileHandle, &written, 0, buf, len, FS_WRITE_FLUSH);
    FSFILE_Close(fileHandle);

    if(R_FAILED(res))
    {
        DiscordLog_Printf("[PREFS] Write failed (0x%08lx)\n", (u32)res);
        return res;
    }

    DiscordLog_Printf("[PREFS] Saved (%d bytes)", len);
    for(u32 i = 0; i < PREFS_COUNT; i++)
        DiscordLog_Printf(" %s=%d", g_user_prefs[i].key, g_pref_values[i]);
    DiscordLog_Printf("\n");
    return 0;
}
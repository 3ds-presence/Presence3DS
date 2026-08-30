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
#include <stdlib.h>
#include <3ds.h>
#include "discord/utils/printf.h"
#include "discord/customRPC/memory_config.h"
#include "discord/customRPC/read_memory.h"
#include "discord/discord_log.h"

// ---------------------------------------------------------------------------
//  Internal structures
// ---------------------------------------------------------------------------

typedef struct
{
    u32 address;
    char type; // 'b', 'h', or 'w'
} AddressEntry;

// ---------------------------------------------------------------------------
//  Static state (BSS)
// ---------------------------------------------------------------------------

static AddressEntry g_entries[CUSTOMRPC_MAX_ENTRIES];
static int g_entry_count;
static u64 g_loaded_titleid; // 0 = no config loaded for current title
static bool g_tried;         // true if we already attempted to load for this titleId

// ---------------------------------------------------------------------------
//  Internal helpers
// ---------------------------------------------------------------------------

// Parse "AAAAAAAAx" entries (8 hex digits + type b/h/w) separated by ',' or
// whitespace into g_entries. buf is modified in place.
static void parse_entries(char *buf)
{
    char *saveptr = NULL;
    for(char *token = strtok_r(buf, ",\t\r\n ", &saveptr);
        token && g_entry_count < CUSTOMRPC_MAX_ENTRIES;
        token = strtok_r(NULL, ",\t\r\n ", &saveptr))
    {
        if(strlen(token) != 9) // exactly 8 hex digits + type letter
            continue;

        char type = token[8];
        if(type != 'b' && type != 'h' && type != 'w')
            continue;

        token[8] = '\0';
        u32 addr = (u32)strtoul(token, NULL, 16);
        if(addr == 0)
            continue;

        // Store valid entry
        g_entries[g_entry_count].address = addr;
        g_entries[g_entry_count].type = type;
        g_entry_count++;
    }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

bool CustomRPC_LoadConfigFromString(u64 titleId, const char *data)
{
    char buf[CUSTOMRPC_EXTRA_SIZE];

    CustomRPC_ClearConfig();

    if(data == NULL)
        return false;

    strncpy(buf, data, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    DiscordLog_Printf("[RPC] Server code (%u bytes): %s\n", (u32)strlen(buf), buf);
    parse_entries(buf);

    g_loaded_titleid = titleId;
    g_tried = true;

    if(g_entry_count > 0)
    {
        DiscordLog_Printf("[RPC] Loaded %d server address(es) for %016llX\n",
                          g_entry_count, titleId);
        return true;
    }

    DiscordLog_Printf("[RPC] No valid entries in server code for %016llX\n", titleId);
    return false;
}

bool CustomRPC_HasConfig(void)
{
    return g_tried && g_entry_count > 0 && g_loaded_titleid != 0;
}

bool CustomRPC_TriedForTitle(u64 titleId)
{
    return g_tried && g_loaded_titleid == titleId;
}

void CustomRPC_MarkTried(u64 titleId)
{
    g_tried = true;
    g_loaded_titleid = titleId;
}

void CustomRPC_BuildExtraString(char *extra_out, size_t extra_size)
{
    extra_out[0] = '\0';

    if (!CustomRPC_HasConfig())
        return;

    int pos = 0;
    for (int i = 0; i < g_entry_count && pos < (int)extra_size; i++)
    {
        u32 addr = g_entries[i].address;
        u64 val;
        const char *fmt;

        switch (g_entries[i].type)
        {
        case 'b':
            val = CustomRPC_ReadByte(addr);
            fmt = "%08lX=%02lX";
            break;
        case 'h':
            val = CustomRPC_ReadHalfWord(addr);
            fmt = "%08lX=%04lX";
            break;
        case 'w':
            val = CustomRPC_ReadWord(addr);
            fmt = "%08lX=%08lX";
            break;
        default:
            continue; 
        }

        // Append '&' if not the first entry and there's space for it
        if (pos > 0 && pos + 1 < (int)extra_size)
        {
            extra_out[pos++] = '&';
            extra_out[pos] = '\0';
        }

        int needed = snprintf(extra_out + pos, extra_size - pos,
                              fmt, (unsigned long)addr, (unsigned long)val);
        if (needed < 0 || pos + needed >= (int)extra_size)
            break;
        pos += needed;
    }

    DiscordLog_Printf("[RPC] Extra string (%d bytes): %s\n", (int)strlen(extra_out), extra_out);
}

void CustomRPC_ClearConfig(void)
{
    g_entry_count = 0;
    g_loaded_titleid = 0;
    g_tried = false;
}

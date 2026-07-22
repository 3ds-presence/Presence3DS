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
#include <stdlib.h>
#include <3ds.h>
#include "discord/customRPC/memory_config.h"
#include "discord/customRPC/read_memory.h"
#include "discord/discord_log.h"

// ---------------------------------------------------------------------------
//  Internal structures
// ---------------------------------------------------------------------------

typedef struct {
    u32 address;
    char type; // 'b', 'h', or 'w'
} AddressEntry;

// ---------------------------------------------------------------------------
//  Static state (BSS)
// ---------------------------------------------------------------------------

static AddressEntry g_entries[CUSTOMRPC_MAX_ENTRIES];
static int g_entry_count;
static u64 g_loaded_titleid;    // 0 = no config loaded for current title
static bool g_tried;            // true if we already attempted to load for this titleId
static char g_extra_raw[CUSTOMRPC_EXTRA_SIZE + 1];

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

bool CustomRPC_LoadConfigForTitle(u64 titleId)
{
    // If we already attempted (success or fail) for this title, don't re-read
    if(g_tried && g_loaded_titleid == titleId)
        return g_entry_count > 0;

    // Reset any previous state
    CustomRPC_ClearConfig();

    Result res;
    Handle fileHandle;
    char path[64];
    char buf[CUSTOMRPC_EXTRA_SIZE];

    snprintf(path, sizeof(path), CUSTOMRPC_CONFIG_PATH "/%016llX.txt", titleId);

    DiscordLog_Printf("[RPC] Loading memory config: %s\n", path);

    res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
                                  fsMakePath(PATH_EMPTY, ""),
                                  fsMakePath(PATH_ASCII, path),
                                  FS_OPEN_READ, 0);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[RPC] No config for %016llX (0x%08lX)\n", titleId, (u32)res);
        g_loaded_titleid = titleId;
        g_tried = true;
        return false;
    }

    // Read file content
    u32 bytesRead;
    res = FSFILE_Read(fileHandle, &bytesRead, 0, buf, sizeof(buf) - 1);
    FSFILE_Close(fileHandle);

    if(R_FAILED(res) || bytesRead == 0)
    {
        DiscordLog_Printf("[RPC] Config read failed for %016llX\n", titleId);
        g_loaded_titleid = titleId;
        g_tried = true;
        return false;
    }

    buf[bytesRead] = '\0';

    DiscordLog_Printf("[RPC] Config (%u bytes): %s\n", bytesRead, buf);

    // Parse tokens separated by ','
    char *token = buf;
    while(token && *token && g_entry_count < CUSTOMRPC_MAX_ENTRIES)
    {
        // Find next token boundary
        char *next = strchr(token, ',');
        if(next)
        {
            *next = '\0';
            next++;
        }

        // Skip leading whitespace (shouldn't occur, but be safe)
        while(*token == ' ' || *token == '\t')
            token++;

        if(*token == '\0')
        {
            token = next;
            continue;
        }

        // Format: 0xADDRESSe followed by type letter (b/h/w)
        // Example: 0x004FE6E0b
        size_t len = strlen(token);
        if(len < 3)
        {
            token = next;
            continue;
        }

        char type = token[len - 1];
        if(type != 'b' && type != 'h' && type != 'w')
        {
            token = next;
            continue;
        }

        // Null-terminate before the type char to parse the address
        token[len - 1] = '\0';

        // Parse address (hex format)
        u32 addr = (u32)strtoul(token, NULL, 16);
        if(addr == 0)
        {
            token = next;
            continue;
        }

        // Store valid entry
        g_entries[g_entry_count].address = addr;
        g_entries[g_entry_count].type = type;
        g_entry_count++;

        token = next;
    }

    g_loaded_titleid = titleId;
    g_tried = true;

    if(g_entry_count > 0)
    {
        DiscordLog_Printf("[RPC] Loaded %d address(es) for %016llX\n", g_entry_count, titleId);
        return true;
    }

    DiscordLog_Printf("[RPC] No valid entries in config for %016llX\n", titleId);
    return false;
}

bool CustomRPC_HasConfig(void)
{
    return g_tried && g_entry_count > 0 && g_loaded_titleid != 0;
}

void CustomRPC_BuildExtraString(void)
{
    g_extra_raw[0] = '\0';

    if(!CustomRPC_HasConfig())
        return;

    int pos = 0;
    for(int i = 0; i < g_entry_count && pos < CUSTOMRPC_EXTRA_SIZE; i++)
    {
        u32 addr = g_entries[i].address;
        u64 val = 0;
        const char *val_fmt = "%08lX";

        switch(g_entries[i].type)
        {
            case 'b':
                val = CustomRPC_ReadByte(addr);
                val_fmt = "%02lX";
                break;
            case 'h':
                val = CustomRPC_ReadHalfWord(addr);
                val_fmt = "%04lX";
                break;
            case 'w':
                val = CustomRPC_ReadWord(addr);
                val_fmt = "%08lX";
                break;
        }

        // Format: ADDR=VAL&
        // addr is up to 8 hex digits, val depends on type (2/4/8 hex digits)
        // "ADDR=VAL&" = up to ~20 chars
        int needed = snprintf(g_extra_raw + pos, CUSTOMRPC_EXTRA_SIZE - pos + 1,
                              "%08lX=", (unsigned long)addr);
        if(needed > 0 && pos + needed <= CUSTOMRPC_EXTRA_SIZE)
            pos += needed;
        else
            break;

        needed = snprintf(g_extra_raw + pos, CUSTOMRPC_EXTRA_SIZE - pos + 1,
                          val_fmt, (unsigned long)val);
        if(needed > 0 && pos + needed <= CUSTOMRPC_EXTRA_SIZE)
            pos += needed;
        else
            break;

        if(pos < CUSTOMRPC_EXTRA_SIZE)
        {
            g_extra_raw[pos++] = '&';
            g_extra_raw[pos] = '\0';
        }
    }

    // Remove trailing '&' if any
    if(pos > 0 && g_extra_raw[pos - 1] == '&')
        g_extra_raw[pos - 1] = '\0';

    DiscordLog_Printf("[RPC] Extra string (%d bytes): %s\n", (int)strlen(g_extra_raw), g_extra_raw);
}

const char* CustomRPC_GetRawExtra(void)
{
    return g_extra_raw;
}

void CustomRPC_ClearConfig(void)
{
    g_entry_count = 0;
    g_loaded_titleid = 0;
    g_tried = false;
    g_extra_raw[0] = '\0';
}
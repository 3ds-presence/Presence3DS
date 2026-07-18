/*
 *   This file is part of Luma3DS
 *   Copyright (C) 2016-2021 Aurora Wright, TuxSH
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
#include <arpa/inet.h>
#include "ifile.h"
#include "discord/discord_config.h"
#include "discord/discord_log.h"

char g_uuid[37] = {0};
char g_aes_key_hex[65] = {0};
char g_server_host[256] = {0};
u16 g_server_port = 0;
bool g_config_loaded = false;

// Helper: find value after "KEY=" in line, copy to dst with maxlen
static bool parse_line(const char *line, const char *key, char *dst, u32 maxlen)
{
    u32 keylen = strlen(key);
    if(strncmp(line, key, keylen) == 0 && line[keylen] == '=')
    {
        const char *val = &line[keylen + 1];
        u32 vallen = strlen(val);
        // Strip trailing whitespace/newlines
        while(vallen > 0 && (val[vallen - 1] == '\r' || val[vallen - 1] == '\n' || val[vallen - 1] == ' '))
            vallen--;
        if(vallen > 0 && vallen < maxlen)
        {
            memcpy(dst, val, vallen);
            dst[vallen] = '\0';
            return true;
        }
    }
    return false;
}

Result DiscordConfig_Load(void)
{
    Result res;
    Handle fileHandle;
    char buf[256];
    u64 total;

    // Reset config
    g_uuid[0] = '\0';
    g_aes_key_hex[0] = '\0';
    g_server_host[0] = '\0';
    g_server_port = 0;
    g_config_loaded = false;

    // Open the file directly (pattern from file_loader.c / plugin system)
    res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
                                  fsMakePath(PATH_EMPTY, ""),
                                  fsMakePath(PATH_ASCII, DISCORD_CONFIG_PATH),
                                  FS_OPEN_READ, 0);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[ERR] Can't open " DISCORD_CONFIG_PATH " (0x%08lx)\n", (u32)res);
        DiscordLog_Printf("[INFO] Create file on SD with UUID=..., AES_KEY=..., etc\n");
        return res;
    }

    DiscordLog_Printf("[INFO] File opened successfully\n");

    // Read the whole file
    {
        u32 read;
        res = FSFILE_Read(fileHandle, &read, 0, buf, sizeof(buf) - 1);
        if(R_FAILED(res))
        {
            DiscordLog_Printf("[ERR] Read failed (0x%08lx)\n", (u32)res);
            FSFILE_Close(fileHandle);
            return res;
        }
        buf[read] = '\0';
        total = read;
    }

    FSFILE_Close(fileHandle);

    DiscordLog_Printf("[INFO] Read %llu bytes\n", total);

    // Parse lines
    char *line = buf;
    char *next;
    char tmp_str[64];
    bool has_uuid = false, has_key = false, has_host = false, has_port = false;

    while(line && *line)
    {
        // Find next line
        next = strchr(line, '\n');
        if(next)
        {
            *next = '\0';
            next++;
        }

        // Skip empty lines and comments
        if(line[0] != '\0' && line[0] != '#')
        {
            if(parse_line(line, "UUID", g_uuid, sizeof(g_uuid)))
                has_uuid = true;
            else if(parse_line(line, "AES_KEY", g_aes_key_hex, sizeof(g_aes_key_hex)))
                has_key = true;
            else if(parse_line(line, "SERVER_HOST", g_server_host, sizeof(g_server_host)))
                has_host = true;
            else if(parse_line(line, "SERVER_PORT", tmp_str, sizeof(tmp_str)))
            {
                // Manual port parsing
                u32 port = 0;
                bool port_ok = true;
                for(const char *p = tmp_str; *p; p++)
                {
                    if(*p >= '0' && *p <= '9')
                        port = port * 10 + (*p - '0');
                    else { port_ok = false; break; }
                }
                if(port_ok && port > 0 && port <= 65535)
                {
                    g_server_port = (u16)port;
                    has_port = true;
                }
            }
        }

        line = next;
    }

    if(has_uuid && has_key && has_host && has_port)
    {
        g_config_loaded = true;
        DiscordLog_Printf("[OK] UUID=%s, Server=%s:%u\n",
            g_uuid, g_server_host, g_server_port);
        return 0;
    }
    else
    {
        DiscordLog_Printf("[WARN] Config field missing: uuid=%d key=%d host=%d port=%d\n",
            has_uuid, has_key, has_host, has_port);
        return -1;
    }
}
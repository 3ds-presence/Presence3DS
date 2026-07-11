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
u32 g_server_ip = 0;
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
    IFile file;
    char buf[256];
    u64 total;

    // Reset config
    g_uuid[0] = '\0';
    g_aes_key_hex[0] = '\0';
    g_server_ip = 0;
    g_server_port = 0;
    g_config_loaded = false;

    // Open the config file from SD
    res = IFile_Open(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                     fsMakePath(PATH_ASCII, DISCORD_CONFIG_PATH),
                     FS_OPEN_READ);
    if(R_FAILED(res))
        return res;

    // Read the whole file
    res = IFile_Read(&file, &total, buf, sizeof(buf) - 1);
    if(R_FAILED(res))
    {
        IFile_Close(&file);
        return res;
    }
    buf[total] = '\0';

    IFile_Close(&file);

    // Parse lines
    char *line = buf;
    char *next;
    char tmp_str[64];
    bool has_uuid = false, has_key = false, has_ip = false, has_port = false;

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
            else if(parse_line(line, "SERVER_IP", tmp_str, sizeof(tmp_str)))
            {
                // Convert IP string to u32 (network byte order)
                g_server_ip = inet_addr(tmp_str);
                if(g_server_ip != 0 && g_server_ip != 0xFFFFFFFF)
                    has_ip = true;
            }
            else if(parse_line(line, "SERVER_PORT", tmp_str, sizeof(tmp_str)))
            {
                // Convert port string to u16
                long port = strtol(tmp_str, NULL, 10);
                if(port > 0 && port <= 65535)
                {
                    g_server_port = (u16)port;
                    has_port = true;
                }
            }
        }

        line = next;
    }

    if(has_uuid && has_key && has_ip && has_port)
    {
        g_config_loaded = true;
        DiscordLog_Printf("[OK] UUID=%s, IP=%lu.%lu.%lu.%lu:%u\n",
            g_uuid,
            (g_server_ip >> 0) & 0xFF, (g_server_ip >> 8) & 0xFF,
            (g_server_ip >> 16) & 0xFF, (g_server_ip >> 24) & 0xFF,
            g_server_port);
        return 0;
    }
    else
    {
        DiscordLog_Printf("[WARN] Config incomplete: uuid=%d key=%d ip=%d port=%d\n",
            has_uuid, has_key, has_ip, has_port);
        return -1;
    }
}
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
#include "discord/discord_config.h"
#include "discord/utils/config_reader.h"
#include "discord/discord_log.h"

char g_uuid[37] = {0};
char g_aes_key_hex[65] = {0};
char g_server_host[256] = {0};
u16 g_server_port = 0;
bool g_config_loaded = false;

// Context passed to the config handler
typedef struct {
    bool has_uuid;
    bool has_key;
    bool has_host;
    bool has_port;
    char tmp_str[64];
} ConfigContext;

static bool config_handler(const char *key, const char *value, void *userdata)
{
    ConfigContext *ctx = (ConfigContext *)userdata;

    if(strcmp(key, "UUID") == 0)
    {
        size_t len = strlen(value);
        if(len > 0 && len < sizeof(g_uuid))
        {
            memcpy(g_uuid, value, len);
            g_uuid[len] = '\0';
            ctx->has_uuid = true;
        }
    }
    else if(strcmp(key, "AES_KEY") == 0)
    {
        size_t len = strlen(value);
        if(len > 0 && len < sizeof(g_aes_key_hex))
        {
            memcpy(g_aes_key_hex, value, len);
            g_aes_key_hex[len] = '\0';
            ctx->has_key = true;
        }
    }
    else if(strcmp(key, "SERVER_HOST") == 0)
    {
        size_t len = strlen(value);
        if(len > 0 && len < sizeof(g_server_host))
        {
            memcpy(g_server_host, value, len);
            g_server_host[len] = '\0';
            ctx->has_host = true;
        }
    }
    else if(strcmp(key, "SERVER_PORT") == 0)
    {
        // Manual port parsing
        u32 port = 0;
        bool port_ok = true;
        for(const char *p = value; *p; p++)
        {
            if(*p >= '0' && *p <= '9')
                port = port * 10 + (*p - '0');
            else { port_ok = false; break; }
        }
        if(port_ok && port > 0 && port <= 65535)
        {
            g_server_port = (u16)port;
            ctx->has_port = true;
        }
    }

    return true;
}

Result DiscordConfig_Load(void)
{
    // Reset config
    g_uuid[0] = '\0';
    g_aes_key_hex[0] = '\0';
    g_server_host[0] = '\0';
    g_server_port = 0;
    g_config_loaded = false;

    ConfigContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    Result res = ConfigReader_Parse(DISCORD_CONFIG_PATH, config_handler, &ctx);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[ERR] Can't open " DISCORD_CONFIG_PATH " (0x%08lx)\n", (u32)res);
        DiscordLog_Printf("[INFO] Create file on SD with UUID=..., AES_KEY=..., etc\n");
        return res;
    }

    if(ctx.has_uuid && ctx.has_key && ctx.has_host && ctx.has_port)
    {
        g_config_loaded = true;
        DiscordLog_Printf("[OK] UUID=%s, Server=%s:%u\n",
            g_uuid, g_server_host, g_server_port);
        return 0;
    }
    else
    {
        DiscordLog_Printf("[WARN] Config field missing: uuid=%d key=%d host=%d port=%d\n",
            ctx.has_uuid, ctx.has_key, ctx.has_host, ctx.has_port);
        return -1;
    }
}
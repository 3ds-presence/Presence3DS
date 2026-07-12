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
#include "discord/discord_auth.h"
#include "discord/discord_config.h"
#include "discord/aes256_cbc.h"
#include "discord/discord_http.h"
#include "discord/discord_util.h"
#include "discord/discord_log.h"
#include "discord/sha256.h"

u64 g_counter = 0;
bool active_session = false;

// Global IP string (set by DiscordRPC_Start)
extern char g_ip_str[16];

// ---------------------------------------------------------------------------
//  Internal helpers
// ---------------------------------------------------------------------------

// Decode hex AES key from config into raw bytes
static void decode_aes_key(u8 key[32])
{
    for(u32 i = 0; i < 32; i++)
    {
        char h[3] = {g_aes_key_hex[i * 2], g_aes_key_hex[i * 2 + 1], 0};
        key[i] = (u8)strtoul(h, NULL, 16);
    }
}

// Build an authenticated payload: pack counter (8B BE) + SHA256 of msg into auth_buf,
// AES-CBC-encrypt it, and produce the hex string in auth_hex.
// auth_buf must be at least 48 bytes, auth_hex at least 97 bytes.
static void build_auth(const u8 key[32], const char *msg, u64 counter,
                       char *auth_hex)
{
    u8 hash[32];
    u8 auth_buf[48]; // 8 (counter) + 32 (hash) = 40, padded to 48
    SHA256_CTX sha;

    sha256_init(&sha);
    sha256_update(&sha, (const u8 *)msg, strlen(msg));
    sha256_final(&sha, hash);

    memset(auth_buf, 0, sizeof(auth_buf));
    discord_pack_counter(counter, auth_buf);
    memcpy(&auth_buf[8], hash, 32);

    // IV is always zero for this protocol
    static const u8 zero_iv[16] = {0};
    aes256_cbc_encrypt_to_hex(auth_buf, 40, key, zero_iv, auth_hex, NULL);
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

bool discord_login(void)
{
    char body[128];
    char resp[512];
    char nonce[32];

    snprintf(body, sizeof(body), "uuid=%s", g_uuid);

    int r = discord_http_post(g_ip_str, g_server_port, "/api/login",
                              body, resp, sizeof(resp), 0);
    if(r < 0 || !discord_parse_field(resp, "nonce", nonce, sizeof(nonce)))
    {
        DiscordLog_Printf("[ERR] Login failed\n");
        return false;
    }

    g_counter = strtoull(nonce, NULL, 10);
    DiscordLog_Printf("[AUTH] Nonce=%llu\n", g_counter);
    return true;
}

bool discord_verify(void)
{
    u8 key[32];
    char hex[33];
    char body[256];
    char resp[512];
    char ok[8];

    decode_aes_key(key);

    // Build block: just the counter (8 bytes big-endian).
    // aes256_cbc_encrypt will add PKCS7 padding automatically:
    // 8 → padded to 16 → ciphertext = 16 bytes = 32 hex chars.
    u8 block[8];
    discord_pack_counter(g_counter, block);

    // Encrypt and convert to hex
    static const u8 zero_iv[16] = {0};
    aes256_cbc_encrypt_to_hex(block, 8, key, zero_iv, hex, NULL);

    snprintf(body, sizeof(body), "uuid=%s&cipher_hex=%s", g_uuid, hex);

    int r = discord_http_post(g_ip_str, g_server_port, "/api/login/verify",
                              body, resp, sizeof(resp), 0);
    if(r == 0 && discord_parse_field(resp, "success", ok, sizeof(ok)) &&
       strcmp(ok, "true") == 0)
    {
        active_session = true;
        g_counter++;
        DiscordLog_Printf("[ACTIVE] Connected! counter=%llu\n", g_counter);
        return true;
    }

    DiscordLog_Printf("[ERR] Verify failed\n");
    return false;
}

int discord_activity_update(char* data)
{
    u8 key[32];
    char body[512];
    char resp[512];
    char ok[8];
    char data_enc[64];

    decode_aes_key(key);

    discord_url_encode(data, data_enc, sizeof(data_enc));

    {
        char auth_hex[97];
        build_auth(key, data, g_counter, auth_hex);

        snprintf(body, sizeof(body),
            "uuid=%s&auth_hex=%s&%s",
            g_uuid, auth_hex, data);
    }

    int r = discord_http_post(g_ip_str, g_server_port, "/api/activity",
                              body, resp, sizeof(resp), 0);

    if (r < 0)
    {
        DiscordLog_Printf("[ERR] Activity update failed (r=%d)\n", r);
        return 2;
   }

    if(r == 0 && discord_parse_field(resp, "success", ok, sizeof(ok)) &&
       strcmp(ok, "true") == 0)
    {
        g_counter++;
        DiscordLog_Printf("[ACTIVE] OK counter=%llu\n", g_counter);
        return 0;
    }

    char err[64];
    if(discord_parse_field(resp, "error", err, sizeof(err)))
    {
        DiscordLog_Printf("[WARN] %s\n", err);
        if(strstr(err, "session_expired"))
            return 1; // session expired
    }

    return -1;
}

void discord_logout(void)
{
    u8 key[32];
    char auth_hex[97];
    char body[512];
    char resp[512];
    char ok[8];

    decode_aes_key(key);
    build_auth(key, "logout", g_counter, auth_hex);

    snprintf(body, sizeof(body),
        "uuid=%s&auth_hex=%s",
        g_uuid, auth_hex);

    DiscordLog_Printf("[LOGOUT] POST /api/logout counter=%llu\n", g_counter);

    int r = discord_http_post(g_ip_str, g_server_port, "/api/logout",
                              body, resp, sizeof(resp), 0);
    if(r == 0 && discord_parse_field(resp, "success", ok, sizeof(ok)) &&
       strcmp(ok, "true") == 0)
    {
        DiscordLog_Printf("[LOGOUT] OK\n");
    }
    else
    {
        char err[64];
        if(discord_parse_field(resp, "error", err, sizeof(err)))
            DiscordLog_Printf("[LOGOUT] %s\n", err);
        else
            DiscordLog_Printf("[LOGOUT] Failed (r=%d)\n", r);
    }
}
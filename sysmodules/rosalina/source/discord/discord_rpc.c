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
#include <3ds.h>
#include <stdlib.h>
#include "minisoc.h"
#include "menu.h"
#include "discord/discord_rpc.h"
#include "discord/discord_config.h"
#include "discord/discord_http.h"
#include "discord/discord_crypto.h"
#include "discord/sha256.h"
#include "discord/discord_log.h"

volatile DiscordState g_discord_state = DISCORD_STOPPED;
char g_discord_status[64] = "Stopped";
LightLock g_discord_lock;

static MyThread g_rpcThread;
static u8 CTR_ALIGN(8) g_rpcThreadStack[0x2000];
static Handle g_controlEvent;
static volatile bool g_shouldStop;
static volatile bool g_shouldStart;
static u64 g_counter;
static char g_ip_str[16];

// Helper: parse key=value from HTTP response
static bool parse_response_field(const char *response, const char *field, char *value, u32 maxlen)
{
    const char *p = strstr(response, field);
    if(!p)
        return false;

    p += strlen(field);
    if(*p != '=')
        return false;

    p++;
    u32 i;
    for(i = 0; i < maxlen - 1 && p[i] && p[i] != '&' && p[i] != '\r' && p[i] != '\n'; i++)
        value[i] = p[i];
    value[i] = '\0';

    return i > 0;
}

// Helper: URL encode a string (simple version)
static int url_encode(char *dst, u32 dst_size, const char *src)
{
    u32 di = 0;
    u32 si;
    static const char hex_chars[] = "0123456789ABCDEF";

    for(si = 0; src[si] && di < dst_size - 3; si++)
    {
        u8 c = (u8)src[si];
        if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[di++] = c;
        }
        else if(c == ' ')
        {
            dst[di++] = '+';
        }
        else
        {
            dst[di++] = '%';
            dst[di++] = hex_chars[c >> 4];
            dst[di++] = hex_chars[c & 0x0f];
        }
    }
    dst[di] = '\0';
    return di;
}

// Set state + status in a thread-safe way
static void set_state(DiscordState state, const char *status)
{
    LightLock_Lock(&g_discord_lock);
    g_discord_state = state;
    strncpy(g_discord_status, status, sizeof(g_discord_status) - 1);
    g_discord_status[sizeof(g_discord_status) - 1] = '\0';
    LightLock_Unlock(&g_discord_lock);
}

void DiscordRPC_Init(void)
{
    LightLock_Init(&g_discord_lock);
    svcCreateEvent(&g_controlEvent, RESET_STICKY);
    g_shouldStop = false;
    g_shouldStart = false;
    g_counter = 0;
    g_ip_str[0] = '\0';

    // Convert IP to string
    if(g_config_loaded)
    {
        u8 *ip = (u8 *)&g_server_ip;
        snprintf(g_ip_str, sizeof(g_ip_str), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
    }

    DiscordLog_Printf("[INIT] Discord RPC initialized\n");
}

// Thread function
void DiscordRPC_ThreadMain(void)
{
    DiscordLog_Printf("[THREAD] Started\n");

    while(!g_shouldStop)
    {
        // Wait for start signal or check periodically
        if(g_discord_state == DISCORD_STOPPED)
        {
            if(g_shouldStart && g_config_loaded)
            {
                set_state(DISCORD_STARTING, "Starting...");
                g_shouldStart = false;
            }
            else
            {
                // Sleep 500ms and loop
                svcWaitSynchronization(g_controlEvent, 500 * 1000 * 1000LL);
                svcClearEvent(g_controlEvent);
                continue;
            }
        }

        if(g_shouldStop)
            break;

        // --- STARTING state: init miniSoc ---
        if(g_discord_state == DISCORD_STARTING)
        {
            DiscordLog_Printf("[CONNECT] Initializing network...\n");

            if(R_FAILED(miniSocInit()))
            {
                DiscordLog_Printf("[ERR] miniSocInit failed\n");
                set_state(DISCORD_ERROR, "Network init failed");
                goto error_wait;
            }

            DiscordLog_Printf("[CONNECT] Network OK, sending /login...\n");
            set_state(DISCORD_LOGIN, "Logging in...");
        }

        // --- LOGIN state: POST /login ---
        if(g_discord_state == DISCORD_LOGIN)
        {
            char body[128];
            char response[512];
            char nonce_str[32];
            int res;

            snprintf(body, sizeof(body), "uuid=%s", g_uuid);

            res = discord_http_post(g_ip_str, g_server_port, "/api/login",
                                    body, response, sizeof(response), g_controlEvent);

            if(g_shouldStop)
                goto cleanup;

            if(res < 0)
            {
                DiscordLog_Printf("[ERR] /login HTTP failed\n");
                set_state(DISCORD_ERROR, "Login HTTP failed");
                miniSocExit();
                goto error_wait;
            }

            // Parse nonce from response
            if(!parse_response_field(response, "nonce", nonce_str, sizeof(nonce_str)))
            {
                // Check for error
                char err_str[64];
                if(parse_response_field(response, "error", err_str, sizeof(err_str)))
                {
                    DiscordLog_Printf("[ERR] /login error: %s\n", err_str);
                    set_state(DISCORD_ERROR, "Server rejected");
                }
                else
                {
                    DiscordLog_Printf("[ERR] Cannot parse /login response\n");
                    set_state(DISCORD_ERROR, "Parse error");
                }
                miniSocExit();
                goto error_wait;
            }

            // Convert nonce to u64
            g_counter = strtoull(nonce_str, NULL, 10);
            DiscordLog_Printf("[AUTH] Got nonce: %llu\n", g_counter);
            set_state(DISCORD_VERIFY, "Verifying...");
        }

        // --- VERIFY state: POST /login/verify ---
        if(g_discord_state == DISCORD_VERIFY)
        {
            u8 aes_key[32];
            u8 iv[16] = {0};
            u8 block[16];
            u32 cipher_len;
            char cipher_hex[33]; // 32 hex chars + null
            char body[256];
            char response[512];
            int res;

            // Convert hex key to binary
            u32 key_hex_len = strlen(g_aes_key_hex);
            if(key_hex_len != 64)
            {
                DiscordLog_Printf("[ERR] Invalid AES key length: %u\n", key_hex_len);
                set_state(DISCORD_ERROR, "Invalid key");
                miniSocExit();
                goto error_wait;
            }

            for(u32 i = 0; i < 32; i++)
            {
                char hex_byte[3] = {g_aes_key_hex[i * 2], g_aes_key_hex[i * 2 + 1], 0};
                aes_key[i] = (u8)strtoul(hex_byte, NULL, 16);
            }

            // Build AES block: nonce as 8 bytes big-endian + PKCS7 padding (8 bytes of 0x08)
            memset(block, 0, 16);
            block[0] = (u8)(g_counter >> 56);
            block[1] = (u8)(g_counter >> 48);
            block[2] = (u8)(g_counter >> 40);
            block[3] = (u8)(g_counter >> 32);
            block[4] = (u8)(g_counter >> 24);
            block[5] = (u8)(g_counter >> 16);
            block[6] = (u8)(g_counter >> 8);
            block[7] = (u8)(g_counter);
            memset(&block[8], 0x08, 8); // PKCS7 padding

            // Encrypt
            discord_aes256_cbc_encrypt(aes_key, iv, block, 16, block, &cipher_len);

            // Convert to hex
            bytes_to_hex(block, 16, cipher_hex);
            DiscordLog_Printf("[AUTH] cipher_hex=%s\n", cipher_hex);

            // Send /login/verify
            snprintf(body, sizeof(body), "uuid=%s&cipher_hex=%s", g_uuid, cipher_hex);

            res = discord_http_post(g_ip_str, g_server_port, "/api/login/verify",
                                    body, response, sizeof(response), g_controlEvent);

            if(g_shouldStop)
                goto cleanup;

            if(res < 0)
            {
                DiscordLog_Printf("[ERR] /login/verify HTTP failed\n");
                set_state(DISCORD_ERROR, "Verify HTTP failed");
                miniSocExit();
                goto error_wait;
            }

            // Check for success
            char success_str[8];
            if(parse_response_field(response, "success", success_str, sizeof(success_str)) &&
               strcmp(success_str, "true") == 0)
            {
                DiscordLog_Printf("[AUTH] Login verified successfully\n");
                // counter for first activity = nonce + 1
                g_counter++;
                DiscordLog_Printf("[ACTIVE] Starting activity loop, counter=%llu\n", g_counter);
                set_state(DISCORD_ACTIVE, "Connected to Discord");
                DiscordLog_Printf("[ACTIVE] Connected!\n");
            }
            else
            {
                char err_str[64];
                if(parse_response_field(response, "error", err_str, sizeof(err_str)))
                {
                    DiscordLog_Printf("[ERR] /login/verify error: %s\n", err_str);
                    set_state(DISCORD_ERROR, "Auth failed");
                }
                else
                {
                    DiscordLog_Printf("[ERR] Unexpected verify response\n");
                    set_state(DISCORD_ERROR, "Verify failed");
                }
                miniSocExit();
                goto error_wait;
            }
        }

        // --- ACTIVE state: send /activity every 10 seconds ---
        if(g_discord_state == DISCORD_ACTIVE)
        {
            int activity_retry = 0;
            const int max_retries = 3;

            while(g_discord_state == DISCORD_ACTIVE && !g_shouldStop)
            {
                // Wait 10 seconds between activity updates (with cancel check)
                svcWaitSynchronization(g_controlEvent, 10LL * 1000 * 1000 * 1000);
                svcClearEvent(g_controlEvent);

                if(g_shouldStop)
                    break;

                // Build the activity request
                u8 aes_key[32];
                u8 iv[16] = {0};
                char hash_input[256];
                u8 hash[32];
                u8 auth_input[48]; // 40 bytes + 8 PKCS7 padding
                u32 auth_len;
                char auth_hex[97]; // 96 hex chars + null
                char state_enc[64], details_enc[64];
                char body[512];
                char response[512];
                SHA256_CTX sha_ctx;
                int res;

                // Convert hex key to binary
                u32 key_hex_len = strlen(g_aes_key_hex);
                for(u32 i = 0; i < 32 && i * 2 < key_hex_len; i++)
                {
                    char hex_byte[3] = {g_aes_key_hex[i * 2], g_aes_key_hex[i * 2 + 1], 0};
                    aes_key[i] = (u8)strtoul(hex_byte, NULL, 16);
                }

                // Simple state and details (placeholder)
                const char *state_str = "Playing on 3DS";
                const char *details_str = "Luma3DS Discord RPC";

                // URL encode
                url_encode(state_enc, sizeof(state_enc), state_str);
                url_encode(details_enc, sizeof(details_enc), details_str);

                // SHA256 of hash_input = state + details + activity_type
                snprintf(hash_input, sizeof(hash_input), "%s%s%d", state_str, details_str, 0);
                sha256_init(&sha_ctx);
                sha256_update(&sha_ctx, (const u8 *)hash_input, strlen(hash_input));
                sha256_final(&sha_ctx, hash);

                // Build auth_input: counter (8 bytes BE) || hash (32 bytes) = 40 bytes
                memset(auth_input, 0, sizeof(auth_input));
                auth_input[0] = (u8)(g_counter >> 56);
                auth_input[1] = (u8)(g_counter >> 48);
                auth_input[2] = (u8)(g_counter >> 40);
                auth_input[3] = (u8)(g_counter >> 32);
                auth_input[4] = (u8)(g_counter >> 24);
                auth_input[5] = (u8)(g_counter >> 16);
                auth_input[6] = (u8)(g_counter >> 8);
                auth_input[7] = (u8)(g_counter);
                memcpy(&auth_input[8], hash, 32);

                // PKCS7 padding: add 8 bytes of 0x08
                memset(&auth_input[40], 0x08, 8);

                // AES-256-CBC encrypt 48 bytes = 3 blocks
                discord_aes256_cbc_encrypt(aes_key, iv, auth_input, 48, auth_input, &auth_len);

                // Convert auth to hex
                bytes_to_hex(auth_input, 48, auth_hex);

                // Build POST body
                snprintf(body, sizeof(body),
                    "uuid=%s&counter=%llu&auth_hex=%s&state=%s&details=%s&activity_type=0",
                    g_uuid, g_counter, auth_hex, state_enc, details_enc);

                res = discord_http_post(g_ip_str, g_server_port, "/api/activity",
                                        body, response, sizeof(response), g_controlEvent);

                if(g_shouldStop)
                    break;

                if(res == 0)
                {
                    // Check for success
                    char success_str[8];
                    if(parse_response_field(response, "success", success_str, sizeof(success_str)) &&
                       strcmp(success_str, "true") == 0)
                    {
                        g_counter++;
                        activity_retry = 0;
                        DiscordLog_Printf("[ACTIVE] Activity updated (counter=%llu)\n", g_counter);
                    }
                    else
                    {
                        char err_str[64];
                        if(parse_response_field(response, "error", err_str, sizeof(err_str)))
                        {
                            DiscordLog_Printf("[WARN] Activity error: %s\n", err_str);
                            if(strstr(err_str, "session_expired"))
                            {
                                DiscordLog_Printf("[WARN] Session expired, reconnecting...\n");
                                set_state(DISCORD_STARTING, "Reconnecting...");
                                break;
                            }
                            else if(strstr(err_str, "replay"))
                            {
                                // Counter issue, increment and retry
                                g_counter++;
                            }
                        }
                        activity_retry++;
                    }
                }
                else
                {
                    activity_retry++;
                    DiscordLog_Printf("[WARN] Activity HTTP failed (%d/%d)\n",
                        activity_retry, max_retries);
                }

                // If too many failures, disconnect
                if(activity_retry >= max_retries && g_discord_state == DISCORD_ACTIVE)
                {
                    DiscordLog_Printf("[ERR] Too many failures, disconnecting\n");
                    set_state(DISCORD_ERROR, "Connection lost");
                    break;
                }
            }

            // If we're not stopping and in error state, clean up
            if(g_discord_state != DISCORD_ACTIVE && !g_shouldStop)
            {
                miniSocExit();
                goto error_wait;
            }
        }

        continue;

error_wait:
        // Wait before retrying (don't hammer the server)
        DiscordLog_Printf("[WAIT] Waiting 15s before retry...\n");
        svcWaitSynchronization(g_controlEvent, 15LL * 1000 * 1000 * 1000);
        svcClearEvent(g_controlEvent);
        if(!g_shouldStop)
        {
            set_state(DISCORD_STOPPED, "Waiting to retry");
            DiscordLog_Printf("[STOP] Back to stopped state\n");
        }
        continue;

cleanup:
        // Clean up miniSoc if needed
        if(g_discord_state == DISCORD_ACTIVE || g_discord_state == DISCORD_LOGIN ||
           g_discord_state == DISCORD_VERIFY)
        {
            miniSocExit();
        }
        set_state(DISCORD_STOPPED, "Stopped");
    }

    DiscordLog_Printf("[THREAD] Exiting\n");
}

void DiscordRPC_Start(void)
{
    // Load config if not yet loaded
    if(!g_config_loaded)
    {
        DiscordLog_Printf("[CMD] Loading config before start...\n");
        DiscordConfig_Load();
    }

    if(g_discord_state == DISCORD_STOPPED && !g_shouldStart && g_config_loaded)
    {
        g_shouldStart = true;
        svcSignalEvent(g_controlEvent);
        DiscordLog_Printf("[CMD] Start requested\n");
    }
    else if(!g_config_loaded)
    {
        DiscordLog_Printf("[CMD] Cannot start: config not loaded\n");
    }
}

void DiscordRPC_Stop(void)
{
    if(g_discord_state != DISCORD_STOPPED)
    {
        DiscordLog_Printf("[CMD] Stop requested\n");
        set_state(DISCORD_STOPPED, "Stopped");
        g_shouldStop = true;
        svcSignalEvent(g_controlEvent);
    }
}

MyThread *DiscordRPC_CreateThread(void)
{
    DiscordRPC_Init();

    if(R_FAILED(MyThread_Create(&g_rpcThread, DiscordRPC_ThreadMain,
                                g_rpcThreadStack, sizeof(g_rpcThreadStack),
                                53, CORE_SYSTEM)))
    {
        DiscordLog_Printf("[ERR] Failed to create RPC thread\n");
        return NULL;
    }

    DiscordLog_Printf("[INIT] RPC thread created\n");
    return &g_rpcThread;
}
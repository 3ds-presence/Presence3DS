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
 *
 *   Thread lifecycle & state management for Discord RPC.
 *   Protocol logic (login, verify, activity, logout) lives in discord_session.c
 */

#include <string.h>
#include "discord/utils/printf.h"
#include <3ds.h>
#include "minisoc.h"
#include "MyThread.h"
#include "menu.h"
#include "discord/discord_rpc_main.h"
#include "discord/discord_config.h"
#include "discord/discord_update.h"
#include "discord/user_prefs.h"
#include "discord/discord_session.h"
#include "discord/discord_log.h"
#include "discord/discord_activity.h"
#include "discord/utils/mii_utils.h"
#include "discord/utils/sha256.h"
#include "discord/customRPC/read_memory.h"
#include "discord/customRPC/memory_config.h"
#include "pmdbgext.h"

volatile DiscordState g_discord_state = DISCORD_STOPPED;
char g_discord_status[64] = "Stopped";
LightLock g_discord_lock;

static MyThread g_rpcThread;
static u8 CTR_ALIGN(8) g_rpcThreadStack[0x4000];
static volatile bool g_shouldStop;
static volatile bool g_rpcStopping;
static Handle g_rpcStartedEvent;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static void set_state(DiscordState s, const char *st)
{
    LightLock_Lock(&g_discord_lock);
    g_discord_state = s;
    strncpy(g_discord_status, st, sizeof(g_discord_status) - 1);
    g_discord_status[sizeof(g_discord_status) - 1] = '\0';
    LightLock_Unlock(&g_discord_lock);
}

// Initialize network and verify socket creation works.
// Returns true on success.
static bool network_init(void)
{
    if(R_FAILED(miniSocInit()))
    {
        DiscordLog_Printf("[ERR] miniSocInit failed\n");
        set_state(DISCORD_ERROR, "Network init failed");
        return false;
    }

    u32 tries = 15;
    int sock = socSocket(AF_INET, SOCK_STREAM, 0);
    while(sock == -1 && --tries > 0)
    {
        svcSleepThread(100 * 1000 * 1000LL);
        sock = socSocket(AF_INET, SOCK_STREAM, 0);
    }

    if(sock < 0)
    {
        DiscordLog_Printf("[ERR] Socket creation failed\n");
        set_state(DISCORD_ERROR, "Socket failed");
        return false;
    }
    socClose(sock);

    return true;
}

// ---------------------------------------------------------------------------
//  Thread main
// ---------------------------------------------------------------------------

void DiscordRPC_ThreadMain(void)
{
    active_session = false;
    DiscordLog_Printf("[THREAD] Started\n");

    if(!network_init())
    {
        svcSignalEvent(g_rpcStartedEvent);
        miniSocExit();
        return;
    }

    svcSignalEvent(g_rpcStartedEvent);
    DiscordLog_Printf("[THREAD] Network OK, starting login...\n");

    char data_mii[MII_OUT_SIZE + 16] = "\0";
    if(!g_pref_values[PREFS_HIDE_MII])
    {
        char mii[MII_OUT_SIZE];
        mii_get_raw_hex(mii, sizeof(mii));
        snprintf(data_mii, sizeof(data_mii), "mii=%s", mii);
    }

    bool reconnecting = false;
    for(;;)
    {
        // --- Login + Verify ---
        // After a network error (reconnecting == true), retry a few times.
        int max_attempts = reconnecting ? 3 : 1;
        bool session_ok = false;
        set_state(DISCORD_LOGIN, reconnecting ? "Reconnecting..." : "Logging in...");
        for (int attempt = 1; attempt <= max_attempts && !g_shouldStop; attempt++)
        {
            int login_res = discord_login();
            if(login_res == 1)
            {
                // Server refused the login (success=false): retrying won't help.
                DiscordLog_Printf("[THREAD] Login refused, stopping session\n");
                set_state(DISCORD_ERROR, "Login refused");
                goto stop;
            }
            if(login_res != 0)
            {
                // Network error: worth retrying when reconnecting.
                DiscordLog_Printf("[ERR] Login failed (attempt %d/%d)\n", attempt, max_attempts);
                if(attempt == max_attempts)
                {
                    set_state(DISCORD_ERROR, "Login failed");
                    break;
                }
            }
            else
            {
                set_state(DISCORD_VERIFY, "Verifying...");
                if(discord_verify(data_mii))
                {
                    session_ok = true;
                    break;
                }
                DiscordLog_Printf("[ERR] Verify failed (attempt %d/%d)\n", attempt, max_attempts);
                if(attempt == max_attempts)
                {
                    set_state(DISCORD_ERROR, "Verify failed");
                    break;
                }
            }
            set_state(DISCORD_LOGIN, "Reconnecting...");
            svcSleepThread(3 * 1000 * 1000 * 1000LL); // Wait 3s before retrying
        }
        if(!session_ok)
        {
            DiscordLog_Printf("[THREAD] Error occurred, stopping session\n");
            goto stop;
        }
        reconnecting = false;

        // --- Activity loop ---
        set_state(DISCORD_ACTIVE, "Connected to Discord");
        u8 prev_hash[32];
        memset(prev_hash, 0, 32);
        bool network_lost = false;
        while(!g_shouldStop)
        {
            char data[5500];
            int ret = -1;

            if(discord_activity_tick(data, sizeof(data)) != 0)
            {
                DiscordLog_Printf("[THREAD] Activity build failed, reconnecting\n");
                ret = 2;
            }
            else
            {
                // Compute SHA-256 hash of the activity data for change detection
                u8 current_hash[32];
                SHA256_CTX sha;
                sha256_init(&sha);
                sha256_update(&sha, (const u8 *)data, strlen(data));
                sha256_final(&sha, current_hash);

                if (memcmp(current_hash, prev_hash, 32) != 0)
                {
                    memcpy(prev_hash, current_hash, 32);
                    DiscordLog_Printf("[THREAD] Activity changed: %s\n", data);
                    // If HIDE_HOME is enabled and we're on Home Menu
                    if(g_pref_values[PREFS_HIDE_HOME])
                    {
                        // Check if title ID is all zeros (Home Menu)
                        const char *tid_field = strstr(data, "titleid=0000000000000000");
                        if(tid_field)
                        {
                            data[0] = '\0'; // Clear activity data to hide it
                        }
                    }
                    ret = discord_activity_update(data);
                } else {
                    // No change in activity, just send a heartbeat
                    ret = discord_activity_heartbeat();
                }
            }

            switch(ret) {
                case 0:
                    // All good, continue
                    break;
                case 1:
                    set_state(DISCORD_LOGIN, "Session expired");
                    DiscordLog_Printf("[WARN] Session expired\n");
                    break;
                case 2:
                    set_state(DISCORD_ERROR, "Network error");
                    DiscordLog_Printf("[ERR] Network error\n");
                    break;
                default:
                    set_state(DISCORD_ERROR, "Activity update failed");
                    DiscordLog_Printf("[ERR] Activity update failed (ret=%d)\n", ret);
                    break;
            }

            if (ret != 0)
            {

                network_lost = true;
                active_session = false;
                break;
            }
            for (int i = 0; i < 100 && !g_shouldStop; i++) {
                svcSleepThread(100 * 1000 * 1000); // Sleep 100ms, check for stop signal every 100ms
                // Every one secondes :
                if (i % 10 == 0) {
                    FS_ProgramInfo programInfo;
                    u32 pid;
                    u32 launchFlags;

                    if(R_FAILED(PMDBG_GetCurrentAppInfo(&programInfo, &pid, &launchFlags)))
                    {
                        CustomRPC_UnmapPage();
                        CustomRPC_ClearConfig();
                    }
                }
            }
        }

        if(network_lost)
        {
            DiscordLog_Printf("[THREAD] Disconnected from server: attempting to reconnect\n");
            CustomRPC_UnmapPage();
            CustomRPC_ClearConfig();
            reconnecting = true;
            continue;
        }
    }

stop:
    CustomRPC_UnmapPage();
    CustomRPC_ClearConfig();
    if(active_session) discord_logout();
    set_state(DISCORD_STOPPED, "Stopped");
    miniSocExit();
    DiscordLog_Printf("[THREAD] Exited\n");
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void DiscordRPC_Start(void)
{
    if(!g_config_loaded)
    {
        DiscordLog_Printf("[CMD] Loading config...\n");
        DiscordConfig_Load();
    }

    // Load user preferences
    if(!g_prefs_loaded)
    {
        DiscordLog_Printf("[CMD] Loading user preferences...\n");
        UserPrefs_Load();
    }

    if(g_discord_state != DISCORD_STOPPED || !g_config_loaded)
    {
        if(!g_config_loaded) DiscordLog_Printf("[CMD] No config\n");
        return;
    }

    g_shouldStop = false;
    g_rpcStopping = false;

    if(R_FAILED(svcCreateEvent(&g_rpcStartedEvent, RESET_STICKY)))
    {
        DiscordLog_Printf("[CMD] Event creation failed\n");
        return;
    }

    DiscordLog_Printf("[CMD] Creating thread (prio 0x20)...\n");
    if(R_FAILED(MyThread_Create(&g_rpcThread, DiscordRPC_ThreadMain,
                                g_rpcThreadStack, sizeof(g_rpcThreadStack),
                                0x20, CORE_SYSTEM)))
    {
        DiscordLog_Printf("[CMD] Thread creation failed\n");
        svcCloseHandle(g_rpcStartedEvent);
        return;
    }

    DiscordLog_Printf("[CMD] Waiting for thread init...\n");
    svcWaitSynchronization(g_rpcStartedEvent, 10LL * 1000 * 1000 * 1000);
    svcCloseHandle(g_rpcStartedEvent);
    DiscordLog_Printf("[CMD] Thread initialized\n");
}

void DiscordRPC_Stop(void)
{
    // Guard against concurrent calls 
    if(g_rpcStopping)
        return;
    g_rpcStopping = true;

    DiscordLog_Printf("[CMD] Stopping...\n");
    g_shouldStop = true;

    // Wait 5 seconds for the thread to notice g_shouldStop and exit
    Result res = MyThread_Join(&g_rpcThread, 5LL * 1000 * 1000 * 1000);

    if(R_FAILED(res))
    {
        // Thread is stuck in a blocking soc:U IPC (no network).
        // Abort soc:U handle to unblock it, then wait indefinitely.
        DiscordLog_Printf("[CMD] Thread timeout, aborting soc:U...\n");
        miniSocAbort();
        MyThread_Join(&g_rpcThread, -1LL);
    }

    set_state(DISCORD_STOPPED, "Stopped");
    DiscordLog_Printf("[CMD] Stopped\n");
}

void DiscordRPC_Init(void)
{
    LightLock_Init(&g_discord_lock);
    g_shouldStop = false;
    g_rpcStopping = false;
    g_counter = 0;
    CustomRPC_Init();
    // Check /presence3ds/.upd: clears the marker if the update was installed,
    // keeps the "Upd avail" flag otherwise
    DiscordUpdate_Init();
    DiscordLog_Printf("[INIT] Discord RPC ready\n");
}
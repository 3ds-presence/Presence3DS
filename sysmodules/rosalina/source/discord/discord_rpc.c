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
 *
 *   Thread lifecycle & state management for Discord RPC.
 *   Protocol logic (login, verify, activity, logout) lives in discord_auth.c
 */

#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include "minisoc.h"
#include "MyThread.h"
#include "menu.h"
#include "discord/discord_rpc.h"
#include "discord/discord_config.h"
#include "discord/discord_auth.h"
#include "discord/discord_log.h"
#include "discord/discord_activity.h"

volatile DiscordState g_discord_state = DISCORD_STOPPED;
char g_discord_status[64] = "Stopped";
LightLock g_discord_lock;

static MyThread g_rpcThread;
static u8 CTR_ALIGN(8) g_rpcThreadStack[0x4000];
static volatile bool g_shouldStop;
char g_ip_str[16];
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

    // --- Login ---
    set_state(DISCORD_LOGIN, "Logging in...");
    if(!discord_login())
    {
        set_state(DISCORD_ERROR, "Login failed");
        goto stop;
    }

    // --- Verify ---
    set_state(DISCORD_VERIFY, "Verifying...");
    if(!discord_verify())
    {
        set_state(DISCORD_ERROR, "Verify failed");
        goto stop;
    }

    // --- Activity loop ---
    set_state(DISCORD_ACTIVE, "Connected to Discord");
    char prev_data[256] = {0};
    while(!g_shouldStop)
    {
        char data[256];
        create_activity_string(data, sizeof(data));

        if (strcmp(data, prev_data) != 0)
        {
            DiscordLog_Printf("[THREAD] Activity changed: %s\n", data);
            strncpy(prev_data, data, sizeof(prev_data) - 1);
            prev_data[sizeof(prev_data) - 1] = '\0';
            int ret = discord_activity_update(data);
        } else {
            // No change in activity, just send a heartbeat
            int ret = discord_activity_heartbeat();
        }

        if(ret == 1) // session expired
        {
            set_state(DISCORD_LOGIN, "Session expired");
            DiscordLog_Printf("[WARN] Session expired\n");
        } 
        else if (ret == 2) // network error
        {
            set_state(DISCORD_ERROR, "Network error");
            DiscordLog_Printf("[ERR] Network error\n");
        }
        
        if (ret != 0) 
        {
            DiscordLog_Printf("[THREAD] Error occurred, stopping session\n");
            active_session = false;
            g_shouldStop = true;
        }
        for (int i = 0; i < 100 && !g_shouldStop; i++) {
            svcSleepThread(100 * 1000 * 1000); // Sleep 100ms, check for stop signal every 100ms
        }
    }

stop:
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
    if(g_discord_state != DISCORD_STOPPED || !g_config_loaded)
    {
        if(!g_config_loaded) DiscordLog_Printf("[CMD] No config\n");
        return;
    }

    // Prepare IP string
    u8 *ip = (u8 *)&g_server_ip;
    snprintf(g_ip_str, sizeof(g_ip_str), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    g_shouldStop = false;

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
    g_counter = 0;
    g_ip_str[0] = '\0';
    DiscordLog_Printf("[INIT] Discord RPC ready\n");
}
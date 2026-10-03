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
#include "discord/utils/soc_utils.h"
#include "MyThread.h"
#include "menu.h"
#include "discord/discord_rpc_main.h"
#include "discord/discord_config.h"
#include "discord/discord_update.h"
#include "discord/user_prefs.h"
#include "discord/discord_session.h"
#include "discord/discord_log.h"
#include "discord/discord_activity.h"
#include "discord/ndm_yield.h"
#include "discord/net_watch.h"
#include "discord/utils/mii_utils.h"
#include "discord/utils/sha256.h"
#include "discord/customRPC/read_memory.h"
#include "discord/customRPC/memory_config.h"
#include "pmdbgext.h"

volatile DiscordState g_discord_state = DISCORD_STOPPED;
char g_discord_status[64] = "Stopped";
LightLock g_discord_lock;
Handle g_rpc_should_stop_event;

static bool is_initialized = false;
static MyThread g_rpcThread;
static u8 CTR_ALIGN(8) g_rpcThreadStack[0x4000];
static volatile bool g_shouldStop;
static volatile bool g_stopSleep;
static volatile bool g_rpcStopping;
static Handle g_rpcStartedEvent;
static u32 s_rpcYieldGen;

// True when the radio was voluntarily released for a game
static bool yieldWantsExit(void)
{
    return ndmYieldRpcShouldExit(&s_rpcYieldGen);
}

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

// Initialize network and verify the soc stack accepts sockets.
// Returns true on success.
static bool network_init(void)
{
    Result init_res = miniSocInit();
    if(R_FAILED(init_res))
    {
        DiscordLog_Printf("[ERR] miniSocInit failed (0x%08lx)\n", (u32)init_res);
        set_state(DISCORD_ERROR, "Network init failed");
        return false;
    }

    // Sanity check: can the stack hand out a socket?
    // (real connectivity is proven by the login POST right after)
    int sock = socSocket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        DiscordLog_Printf("[ERR] Socket probe failed\n");
        set_state(DISCORD_ERROR, "Socket failed");
        return false;
    }
    socClose(sock);

    return true;
}

#define NET_WAIT_FALLBACK_NS (60LL * 1000 * 1000 * 1000)

// Wait until the WiFi is connected
static bool wait_network_ready(void)
{
    if(g_shouldStop || yieldWantsExit())
        return false;

    if(netWatchIsOnline())
        return true;
    else if (!yieldWantsExit())
    {
        DiscordLog_Printf("[NET] Kicking AC connection...\n");
        if(netWatchKick(g_rpc_should_stop_event))
            return true;
        DiscordLog_Printf("[NET] Kick failed, waiting for WiFi...\n");
    }

    set_state(DISCORD_LOGIN, "Waiting WiFi...");

    Handle netEvt = netWatchGetEvent();
    Handle stopEvt = g_rpc_should_stop_event;

    for(;;)
    {
        if(netEvt != 0)
        {
            Handle hds[2];
            s32 nHds = 0;
            hds[nHds++] = netEvt;
            if(stopEvt != 0)
                hds[nHds++] = stopEvt;

            s32 idx = -1;
            Result res = svcWaitSynchronizationN(&idx, hds, nHds, false,
                                                 NET_WAIT_FALLBACK_NS);
            if(R_SUCCEEDED(res))
            {
                if(idx == 1)
                {
                    // stop requested
                    DiscordLog_Printf("[THREAD] Network wait aborted\n");
                    return false;
                }
                // idx == 0: AC reported a connection state change
                svcClearEvent(netEvt);
            }
        }
        else
        {
            // Degraded mode (no event available): timed wait, still abortable
            if(stopEvt != 0)
            {
                if(R_SUCCEEDED(svcWaitSynchronization(stopEvt, NET_WAIT_FALLBACK_NS)))
                {
                    DiscordLog_Printf("[THREAD] Network wait aborted\n");
                    return false;
                }
            }
            else
                svcSleepThread(NET_WAIT_FALLBACK_NS);
        }

        if(g_shouldStop || yieldWantsExit())
        {
            DiscordLog_Printf("[THREAD] Network wait aborted\n");
            return false;
        }

        if(netWatchIsOnline())
            return true;
    }
}

// ---------------------------------------------------------------------------
//  Thread main
// ---------------------------------------------------------------------------

// Result of a login+verify phase (see login_session()).
typedef enum {
    SESSION_OK,       // logged in and verified
    SESSION_REFUSED,  // server refused the login (success=false): retrying won't help
    SESSION_FAILED,   // retries exhausted (login or verify network error)
    SESSION_STOPPED,  // g_shouldStop was requested during login
} SessionResult;

// Login + verify phase. When `reconnecting` is set (after a network error),
// up to 3 attempts are made instead of just 1.
static SessionResult login_session(const char *mii_data, bool reconnecting)
{
    int max_attempts = reconnecting ? 3 : 1;
    set_state(DISCORD_LOGIN, reconnecting ? "Reconnecting..." : "Logging in...");

    for(int attempt = 1; attempt <= max_attempts && !g_shouldStop; attempt++)
    {
        int login_res = discord_login();
        if(login_res == 1)
        {
            // Server refused the login (success=false): retrying won't help.
            DiscordLog_Printf("[THREAD] Login refused, stopping session\n");
            set_state(DISCORD_ERROR, "Login refused");
            return SESSION_REFUSED;
        }
        if(login_res != 0)
        {
            // Network error: worth retrying when reconnecting.
            DiscordLog_Printf("[ERR] Login failed (attempt %d/%d)\n", attempt, max_attempts);
            if(attempt == max_attempts)
            {
                set_state(DISCORD_ERROR, "Login failed");
                return SESSION_FAILED;
            }
        }
        else
        {
            set_state(DISCORD_VERIFY, "Verifying...");
            if(discord_verify(mii_data))
                return SESSION_OK;
            DiscordLog_Printf("[ERR] Verify failed (attempt %d/%d)\n", attempt, max_attempts);
            if(attempt == max_attempts)
            {
                set_state(DISCORD_ERROR, "Verify failed");
                return SESSION_FAILED;
            }
        }
        set_state(DISCORD_LOGIN, "Reconnecting...");
        svcSleepThread(3 * 1000 * 1000 * 1000LL); // Wait 3s before retrying
    }

    // The loop ended because g_shouldStop was set.
    return SESSION_STOPPED;
}

// Result of the activity loop (see run_activity_loop()).
typedef enum {
    ACTIVITY_OK,           // exited because g_shouldStop was requested
    ACTIVITY_YIELD_EXIT,   // radio released for a game (ndm yield)
    ACTIVITY_NETWORK_LOST, // server connection lost: a new login is needed
} ActivityResult;

// Activity loop: pushes activity updates/heartbeats, polls the running app
// (PMDBG) once per second, and watches the yield/stop flags. Returns the
// reason the loop ended.
static ActivityResult run_activity_loop(void)
{
    set_state(DISCORD_ACTIVE, "Connected to Discord");
    u8 prev_hash[32];
    memset(prev_hash, 0, sizeof(prev_hash));

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
            }
            else
            {
                // No change in activity, just send a heartbeat
                ret = discord_activity_heartbeat();
            }
        }

        switch(ret)
        {
            case 0:
                // All good, continue
                break;
            case 1:
                // Server closed the session itself: no logout needed later.
                set_state(DISCORD_ERROR, "Session expired");
                DiscordLog_Printf("[WARN] Session expired\n");
                active_session = false;
                break;
            case 2:
                // Network error (incl. cancelled request): the server-side
                // session state is unknown and most likely still alive,
                // so keep active_session set -> the stop path will attempt
                // a proper logout.
                set_state(DISCORD_ERROR, "Network error");
                DiscordLog_Printf("[ERR] Network error\n");
                break;
            default:
                set_state(DISCORD_ERROR, "Activity update failed");
                DiscordLog_Printf("[ERR] Activity update failed (ret=%d)\n", ret);
                break;
        }

        if (ret != 0)
            return ACTIVITY_NETWORK_LOST;

        for (int i = 0; i < 100 && !g_shouldStop; i++)
        {
            svcSleepThread(100 * 1000 * 1000); // Sleep 100ms, check for stop signal every 100ms
            if(yieldWantsExit())
                return ACTIVITY_YIELD_EXIT;
            // Every second: make sure the app we read from is still alive
            if (i % 10 == 0)
            {
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

    // g_shouldStop was requested while polling the activity
    return ACTIVITY_OK;
}

void DiscordRPC_ThreadMain(void)
{
    active_session = false;
    g_stopSleep = false;
    // Sample the yield generation: any release after this point means this
    // thread must exit as soon as it notices (see yieldWantsExit()).
    s_rpcYieldGen = ndmYieldGetGeneration();
    DiscordLog_Printf("[THREAD] Started\n");
    svcSignalEvent(g_rpcStartedEvent);

    if(!wait_network_ready())
    {
        // Stop (or radio lent to a game) before any network use
        set_state(DISCORD_STOPPED, "Stopped");
        DiscordLog_Printf("[THREAD] Exited\n");
        return;
    }

    if(!network_init())
    {
        ndmYieldSafeSocExit();
        return;
    }

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
        // Before (re)connecting, make sure the WiFi is actually up.
        if(!wait_network_ready())
        {
            DiscordLog_Printf("[THREAD] Network wait aborted, exiting...\n");
            active_session = false; // no network: skip the logout below
            break;
        }

        // --- Login + Verify ---
        SessionResult sres = login_session(data_mii, reconnecting);
        if(sres != SESSION_OK)
        {
            if(sres == SESSION_FAILED)
            {
                // Network error: wait for the WiFi to come back
                DiscordLog_Printf("[THREAD] Network error, waiting for reconnect signal\n");
                CustomRPC_UnmapPage();
                CustomRPC_ClearConfig();
                reconnecting = true;
                continue;
            }
            // SESSION_REFUSED: retrying won't help.
            // SESSION_STOPPED: stop was requested. Voluntary stops are silent.
            break;
        }
        reconnecting = false;

        // --- Activity loop ---
        ActivityResult ares = run_activity_loop();
        if(ares == ACTIVITY_YIELD_EXIT)
        {
            DiscordLog_Printf("[THREAD] Network released for a game, exiting...\n");
            active_session = false; // no point logging out on a dying network
            break;
        }
        if(ares == ACTIVITY_NETWORK_LOST)
        {
            if(yieldWantsExit())
            {
                DiscordLog_Printf("[THREAD] Network released for a game, exiting...\n");
                active_session = false; // no network: skip the logout below
                break;
            }
            DiscordLog_Printf("[THREAD] Disconnected from server: attempting to reconnect\n");
            CustomRPC_UnmapPage();
            CustomRPC_ClearConfig();
            reconnecting = true;
            continue;
        }
        // ACTIVITY_OK: g_shouldStop was requested while active
        break;
    }

    // --- Cleanup (stop path) ---
    CustomRPC_UnmapPage();
    CustomRPC_ClearConfig();
    if(active_session && !ndmYieldIsActive())
        discord_logout(g_stopSleep);
    set_state(DISCORD_STOPPED, "Stopped");
    ndmYieldSafeSocExit();
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
    if(g_rpc_should_stop_event != 0)
        svcClearEvent(g_rpc_should_stop_event);

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

void DiscordRPC_Stop(bool sleep)
{
    // Guard against concurrent calls 
    if(g_rpcStopping)
        return;
    g_rpcStopping = true;

    DiscordLog_Printf("[CMD] Stopping...\n");
    g_shouldStop = true;
    g_stopSleep = sleep;
    if(g_rpc_should_stop_event != 0)
        svcSignalEvent(g_rpc_should_stop_event);

    Result res = MyThread_Join(&g_rpcThread, 10LL * 1000 * 1000 * 1000);
    if(R_FAILED(res))
        DiscordLog_Printf("[ERR] RPC thread did not exit within 10 s\n");

    set_state(DISCORD_STOPPED, "Stopped");
    DiscordLog_Printf("[CMD] Stopped\n");
}

void DiscordRPC_Init(void)
{
    if (is_initialized)
        return;
    is_initialized = true;
    LightLock_Init(&g_discord_lock);
    if(R_FAILED(svcCreateEvent(&g_rpc_should_stop_event, RESET_STICKY)))
    {
        DiscordLog_Printf("[ERR] Cannot create cancel event -> Stop latency degraded (not fatal)\n");
        g_rpc_should_stop_event = 0;
    }
    g_shouldStop = false;
    g_rpcStopping = false;
    g_counter = 0;
    CustomRPC_Init();
    // Check /presence3ds/.upd: clears the marker if the update was installed,
    // keeps the "Upd avail" flag otherwise
    DiscordUpdate_Init();
    DiscordLog_Printf("[INIT] Discord RPC ready\n");
}
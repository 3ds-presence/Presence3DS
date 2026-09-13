/*
*   This file is part of Presence3DS.
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

// Games ask ndm:u for an exclusive state (Download Play, StreetPass)
// While Rosalina's network is active, the kext blocks those requests.
// This module yields: it drops OUR ndm state and clears the kext's 0x10
// block bit so the game gets the state for real; it takes it back on Leave.
//
// Handshake with the kext (GetSystemInfo 0x10005 / KernelSetState 0x10008):
//   bit0: game asks, bit1: we acked (stays set), bit2: real game Leave.
//   subcommands: 0 ack, 1 reset, 2 refuse.
// Leave -> wait 3 s, probe ndm, restore when free; watchdog covers games
// that vanish without a Leave.

#include "discord/ndm_yield.h"
#include <3ds.h>
#include <3ds/services/ndm.h>
#include "MyThread.h"
#include "menu.h"
#include "minisoc.h"
#include "input_redirection.h"
#include "gdb/server.h"
#include "discord/discord_log.h"
#include "discord/discord_rpc_main.h"

extern GDBServer gdbServer;

#define YIELD_WATCHDOG_MS       (10 * 1000)     // probe ndm after this much inactivity
#define YIELD_START_GRACE_MS    (5 * 1000)      // the game legitimately holds its state at first
#define LEAVE_GRACE_MS          (3 * 1000)      // wait after a Leave before restoring

// ndmuYieldState bits
#define KBIT_ASK        1u
#define KBIT_ACK        2u
#define KBIT_LEAVE      4u

#define THREAD_STACK_SIZE   0x1000

// ms -> cpu ticks
static u64 ticksMs(u64 ms)
{
    return ms * (SYSCLOCK_ARM11 / 1000u);
}

// Probe delay while a session runs.
typedef enum
{
    YM_NONE = 0,        // nothing lent
    YM_RUNNING,         // no Leave: long delay (15 s)
    YM_LEAVE_GRACE,     // Leave seen: short delay (3 s)
} YieldMode;

static bool s_yieldActive;            // an ndm state is currently lent to a game
static u32 s_yieldGeneration;         // increment on release: tells RPC threads to stop
static YieldMode s_yieldMode;         // probe delay: 3 s (Leave) or 15 s (no Leave)
static u64 s_yieldLastEventTick;      // tick of last release/Leave/re-arm
static bool s_bitCleared;             // the 0x10 block was cleared by us
static bool s_wasActive;              // restart RPC when the session ends

static LightLock s_lock;
static MyThread s_thread;
static u8 s_threadStack[THREAD_STACK_SIZE];
static Handle s_wakeEvent;            // "session ended" -> restart Discord presence

// Restart Discord RPC after a session (dedicated thread)
static void ndmYieldThreadMain(void)
{
    for(;;)
    {
        if(R_FAILED(svcWaitSynchronization(s_wakeEvent, -1LL)))
            continue;
        svcClearEvent(s_wakeEvent);
        DiscordRPC_StartWithNetRetry(20, 3LL * 1000 * 1000 * 1000);
    }
}

// true if the kext still blocks ndm:u calls (kernel bit 0x10)
static bool kernelBitIsSet(void)
{
    s64 out = 0;
    svcGetSystemInfo(&out, 0x10000, 0x10);
    return (out & 0x10) != 0;
}

// gdb / input redirection need the wifi: refuse
static bool networkOwnedByOthers(void)
{
    if(inputRedirectionEnabled)
        return true;
    if(gdbServer.referenceCount != 0 || gdbServer.super.running)
        return true;

    return false;
}

// Drop our ndm state, clear the 0x10 block, ack the game. Call with the lock held.
static void doRelease(void)
{
    // Lifts exactly what miniSocInit got; no-op if the RPC already released it.
    miniSocUnlockState(false);

    if(!s_bitCleared && kernelBitIsSet())
    {
        svcKernelSetState(0x10000, 0x10); // 0x10 is a toggle: clear only if set
        s_bitCleared = true;
    }

    // bump: tells RPC threads to stop
    s_yieldGeneration++;

    svcKernelSetState(0x10008, 0); // ack: let the game's request pass
}

// Ask ndm if the game is done.
static bool ndmProbe(ndmExclusiveState *out)
{
    if(R_FAILED(ndmuInit()))
        return false;
    *out = NDM_EXCLUSIVE_STATE_LOCAL_COMMUNICATIONS;
    Result rc = NDMU_GetExclusiveState(out);
    ndmuExit();
    return R_SUCCEEDED(rc);
}

// true if ndm is None (= free)
static bool ndmProbeFree(void)
{
    ndmExclusiveState st;
    return ndmProbe(&st) && st == NDM_EXCLUSIVE_STATE_NONE;
}

// wait (max 5 s) for the game to drop its ndm state
static void ndmWaitGameStateGone(void)
{
    for(int i = 0; i < 50; i++)
    {
        ndmExclusiveState st;
        if(!ndmProbe(&st) || st == NDM_EXCLUSIVE_STATE_NONE)
            return;
        svcSleepThread(100 * 1000 * 1000LL);
    }
}

// full: re-lock our ndm state, restore the 0x10 block, reset the handshake.
// full=false (app exit): reset only (the system cleans the dead process).
static void doRestore(bool full)
{
    s_yieldActive = false;
    s_yieldMode = YM_NONE;

    if(full)
    {
        ndmWaitGameStateGone();
        miniSocLockState();
    }

    if(s_bitCleared)
    {
        // restore the 0x10 block only if the wifi is still up
        if(full && miniSocEnabled && !kernelBitIsSet())
            svcKernelSetState(0x10000, 0x10);
        s_bitCleared = false;
    }
    svcKernelSetState(0x10008, 1); // reset handshake: all bits back to 0
}

void ndmYieldProcess(void)
{
    s64 out = 0;
    svcGetSystemInfo(&out, 0x10005, 0);
    u32 state = (u32)out;

    LightLock_Lock(&s_lock);

    bool ended = false;

    if((state & KBIT_ASK) && !(state & KBIT_ACK) && !s_yieldActive)
    {
        if(networkOwnedByOthers())
        {
            // Refuse: gdb / input redirection keep the wifi (kext answers fake success).
            svcKernelSetState(0x10008, 2);
        }
        else
        {
            s_wasActive = g_discord_state != DISCORD_STOPPED;
            s_yieldActive = true;
            s_yieldMode = YM_RUNNING;
            s_yieldLastEventTick = svcGetSystemTick();
            doRelease();
        }
    }
    else if(s_yieldActive)
    {
        // Leave seen: probe every 3 s. No Leave: watchdog probe every 15 s.
        if((state & KBIT_LEAVE) && s_yieldMode != YM_LEAVE_GRACE)
        {
            s_yieldMode = YM_LEAVE_GRACE;
            s_yieldLastEventTick = svcGetSystemTick();
        }

        u64 intervalMs = (s_yieldMode == YM_LEAVE_GRACE)
                             ? LEAVE_GRACE_MS
                             : (YIELD_START_GRACE_MS + YIELD_WATCHDOG_MS);
        if((svcGetSystemTick() - s_yieldLastEventTick) >= ticksMs(intervalMs))
        {
            if(ndmProbeFree())
            {
                DiscordLog_Printf("[YIELD] %s: game gone, restoring network\n",
                                  s_yieldMode == YM_LEAVE_GRACE ? "leave confirmed" : "watchdog");
                doRestore(true);
                ended = true;
            }
            else
                s_yieldLastEventTick = svcGetSystemTick(); // still busy: re-arm
        }
    }

    LightLock_Unlock(&s_lock);

    if(ended && s_wasActive)
    {
        s_wasActive = false;
        svcSignalEvent(s_wakeEvent);
    }
}

void ndmYieldOnAppExit(void)
{
    LightLock_Lock(&s_lock);
    bool active = s_yieldActive;
    if(active)
    {
        doRestore(false);
        DiscordLog_Printf("[YIELD] app exited during session, handshake dropped\n");
    }
    LightLock_Unlock(&s_lock);
}

bool ndmYieldIsActive(void)
{
    LightLock_Lock(&s_lock);
    bool active = s_yieldActive;
    LightLock_Unlock(&s_lock);
    return active;
}

u32 ndmYieldGetGeneration(void)
{
    LightLock_Lock(&s_lock);
    u32 gen = s_yieldGeneration;
    LightLock_Unlock(&s_lock);
    return gen;
}

bool ndmYieldRpcShouldExit(u32 *gen)
{
    LightLock_Lock(&s_lock);
    bool exit = s_yieldActive || (s_yieldGeneration != *gen);
    *gen = s_yieldGeneration;
    LightLock_Unlock(&s_lock);
    return exit;
}

void ndmYieldSafeSocExit(void)
{
    // miniSocExit re-toggles the 0x10 block ON; re-clear it if we had it off.
    LightLock_Lock(&s_lock);

    bool yielding = s_yieldActive;
    bool bitWasOff = yielding && !kernelBitIsSet();

    miniSocExit();

    if(bitWasOff && kernelBitIsSet())
    {
        svcKernelSetState(0x10000, 0x10);
        DiscordLog_Printf("[YIELD] soc released, 0x10 toggle neutralized\n");
    }

    LightLock_Unlock(&s_lock);
}

void ndmYieldInit(void)
{
    LightLock_Init(&s_lock);
    if(R_FAILED(svcCreateEvent(&s_wakeEvent, RESET_STICKY)))
        svcBreak(USERBREAK_PANIC);

    if(R_FAILED(MyThread_Create(&s_thread, ndmYieldThreadMain, s_threadStack,
                                THREAD_STACK_SIZE, 50, CORE_SYSTEM)))
        svcBreak(USERBREAK_PANIC);
}

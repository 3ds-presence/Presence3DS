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

#include <3ds.h>
#include "discord/net_watch.h"
#include "discord/discord_log.h"

static Handle s_notifEvent = 0;

bool netWatchIsOnline(void)
{
    if(R_FAILED(acInit()))
        return false;

    // ACU_GetWifiStatus: bitmask of the AP type we are connected to (0 = none)
    u32 wifiStatus = 0;
    bool online = R_SUCCEEDED(ACU_GetWifiStatus(&wifiStatus)) && wifiStatus != 0;

    if(online)
    {
        // ACU_GetStatus: 1 = not connected, 3 = connected
        u32 status = 0;
        online = R_SUCCEEDED(ACU_GetStatus(&status)) && status != 1;
    }

    acExit();
    return online;
}

Handle netWatchGetEvent(void)
{
    if(s_notifEvent == 0)
    {
        Handle evt = 0;
        if(R_FAILED(svcCreateEvent(&evt, RESET_STICKY)))
        {
            DiscordLog_Printf("[NET] Cannot create AC watch event\n");
            return 0;
        }

        s_notifEvent = evt;
        __dmb();

        DiscordLog_Printf("[NET] Watching AC connection state\n");
    }

    return s_notifEvent;
}

void netWatchNotify(void)
{
    if(s_notifEvent != 0)
        svcSignalEvent(s_notifEvent);
}

bool netWatchKick(Handle cancelEvt)
{
    // Fast path: already connected
    if (netWatchIsOnline())
        return true;

    if (R_FAILED(acInit()))
        return false;

    acuConfig config = {0};
    ACU_CreateDefaultConfig(&config);
    ACU_SetNetworkArea(&config, 2);
    ACU_SetAllowApType(&config, 0xFF);
    ACU_SetRequestEulaVersion(&config);

    Handle connectEvent = 0;
    svcCreateEvent(&connectEvent, RESET_ONESHOT);

    bool connected = false;
    if (R_SUCCEEDED(ACU_ConnectAsync(&config, connectEvent)))
    {
        Handle hds[2];
        s32 nHds = 0;
        hds[nHds++] = connectEvent;
        if (cancelEvt != 0)
            hds[nHds++] = cancelEvt;

        s32 idx = -1;
        if (R_SUCCEEDED(svcWaitSynchronizationN(&idx, hds, nHds, false,
                                                15LL * 1000 * 1000 * 1000)) &&
            idx == 0)
            connected = netWatchIsOnline();
    }
    svcCloseHandle(connectEvent);
    acExit();
    return connected;
}

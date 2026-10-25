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

#include "discord/presence_ipc.h"
#include "discord/discord_rpc_main.h" // PRESENCE3DS_VERSION

static void replyError(u32 *cmdbuf, Result res)
{
    cmdbuf[0] = IPC_MakeHeader(0, 1, 0);
    cmdbuf[1] = res;
}

static void parseVersion(u32 *major, u32 *minor, u32 *patch)
{
    const char *p = PRESENCE3DS_VERSION;
    u32 *dst[3] = { major, minor, patch };

    for(u32 i = 0; i < 3; i++)
    {
        u32 value = 0;

        while(*p != '\0' && (*p < '0' || *p > '9'))
            p++;

        while(*p >= '0' && *p <= '9')
            value = value * 10 + (u32)(*p++ - '0');

        *dst[i] = value;
    }
}

void PresenceIPC__HandleCommands(void *_ctx)
{
    (void)_ctx;

    u32 *cmdbuf = getThreadCommandBuffer();

    switch(cmdbuf[0] >> 16)
    {
        case 0x1:
        {
            if(cmdbuf[0] != IPC_MakeHeader(0x1, 0, 0))
            {
                replyError(cmdbuf, 0xD9001830);
                break;
            }

            u32 major, minor, patch;
            parseVersion(&major, &minor, &patch);

            cmdbuf[0] = IPC_MakeHeader(0x1, 2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = (major << 24) | (minor << 16) | (patch << 8);
            break;
        }

        default:
            replyError(cmdbuf, 0xD900182F);
            break;
    }
}

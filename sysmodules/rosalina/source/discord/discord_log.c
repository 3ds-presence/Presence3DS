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
#include <stdarg.h>
#include <3ds/synchronization.h>
#include "discord/discord_log.h"
#include "fmt.h"

static char g_logBuffer[DISCORD_LOG_SIZE];
static int g_logPos;
static LightLock g_logLock;

void DiscordLog_Init(void)
{
    LightLock_Init(&g_logLock);
    g_logPos = 0;
    g_logBuffer[0] = '\0';
}

void DiscordLog_Printf(const char *fmt, ...)
{
    va_list args;
    char tmp[256];
    int len;

    va_start(args, fmt);
    len = vsprintf(tmp, fmt, args);
    va_end(args);

    if(len <= 0)
        return;

    LightLock_Lock(&g_logLock);

    // Check if we need to make room (+1 for null terminator)
    if(g_logPos + len + 1 > DISCORD_LOG_SIZE)
    {
        int overflow = (g_logPos + len + 1) - DISCORD_LOG_SIZE;
        if(overflow >= g_logPos)
        {
            // Everything must be discarded
            g_logPos = 0;
        }
        else
        {
            // Shift existing content to make room
            memmove(g_logBuffer, g_logBuffer + overflow, g_logPos - overflow);
            g_logPos -= overflow;
        }
    }

    // Append new message
    memcpy(&g_logBuffer[g_logPos], tmp, len);
    g_logPos += len;
    g_logBuffer[g_logPos] = '\0';

    LightLock_Unlock(&g_logLock);
}

char *DiscordLog_GetBuffer(void)
{
    return g_logBuffer;
}
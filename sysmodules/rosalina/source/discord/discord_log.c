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

// Max characters per visual line on the bottom screen (x=10, SPACING_X=6, width=320)
// (320 - 10) / 6 ≈ 51, use 50 to have a safe margin
#define MAX_CHARS_PER_LOG_LINE 50

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

    // Insert \n every MAX_CHARS_PER_LOG_LINE characters to prevent lines
    // from wrapping invisibly when displayed on the bottom screen.
    // Existing \n are preserved and reset the column counter.
    char wrapped[512];
    char *src = tmp;
    char *dst = wrapped;
    int col = 0;

    while(*src && (dst - wrapped) < (int)sizeof(wrapped) - 2)
    {
        if(*src == '\n')
        {
            *dst++ = *src++;
            col = 0;
        }
        else if(col >= MAX_CHARS_PER_LOG_LINE)
        {
            // Insert a line break before the character that would overflow
            *dst++ = '\n';
            col = 0;
        }
        else
        {
            *dst++ = *src++;
            col++;
        }
    }
    *dst = '\0';

    int wrappedLen = (int)(dst - wrapped);

    LightLock_Lock(&g_logLock);

    // Check if we need to make room (+1 for null terminator)
    if(g_logPos + wrappedLen + 1 > DISCORD_LOG_SIZE)
    {
        int overflow = (g_logPos + wrappedLen + 1) - DISCORD_LOG_SIZE;
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

    // Append new message (with \n wrapping already applied)
    memcpy(&g_logBuffer[g_logPos], wrapped, wrappedLen);
    g_logPos += wrappedLen;
    g_logBuffer[g_logPos] = '\0';

    LightLock_Unlock(&g_logLock);
}

char *DiscordLog_GetBuffer(void)
{
    return g_logBuffer;
}
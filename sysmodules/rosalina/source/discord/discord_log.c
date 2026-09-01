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

#include <stdarg.h>
#include <3ds/synchronization.h>
#include "discord/utils/printf.h"
#include "discord/discord_log.h"
#include "fmt.h"

// Max characters per visual line on the bottom screen (x=10, SPACING_X=6, width=320)
// (320 - 10) / 6 ~ 51, use 50 to have a safe margin
#define MAX_CHARS_PER_LOG_LINE 50

// Circular log buffer. g_logHead writes, g_logTail marks the oldest kept
// character, g_logCount counts valid characters. Capacity is capped at
// DISCORD_LOG_SIZE - 1 so one byte always remains free for the terminator
// DiscordLog_GetBuffer writes after linearizing. When full, the oldest
// character is overwritten (tail advances) - no shifting needed.
static char g_logBuffer[DISCORD_LOG_SIZE];
static int g_logHead;
static int g_logTail;
static int g_logCount;
static LightLock g_logLock;

// Append one character to the ring, overwriting the oldest character when full.
static void ring_put(char c)
{
    g_logBuffer[g_logHead] = c;
    g_logHead = (g_logHead + 1) % DISCORD_LOG_SIZE;

    if(g_logCount < DISCORD_LOG_SIZE - 1)
    {
        g_logCount++;
    }
    else
    {
        g_logTail = (g_logTail + 1) % DISCORD_LOG_SIZE;
    }
}

// vfctprintf callback to handle line wrapping
static void log_out_cb(char c, void *extra)
{
    int *col = (int *)extra;

    if(c == '\n')
    {
        ring_put('\n');
        *col = 0;
    }
    else if(*col >= MAX_CHARS_PER_LOG_LINE)
    {
        ring_put('\n');
        ring_put(c);
        *col = 1;
    }
    else
    {
        ring_put(c);
        (*col)++;
    }
}

void DiscordLog_Clear(void)
{
    g_logHead = 0;
    g_logTail = 0;
    g_logCount = 0;
    g_logBuffer[0] = '\0';
}

void DiscordLog_Init(void)
{
    LightLock_Init(&g_logLock);
    DiscordLog_Clear();
}

void DiscordLog_Printf(const char *fmt, ...)
{
    va_list args;
    int col = 0;

    LightLock_Lock(&g_logLock);

    va_start(args, fmt);
    vfctprintf(log_out_cb, &col, fmt, args);
    va_end(args);

    LightLock_Unlock(&g_logLock);
}

// Reverse g_logBuffer[lo..hi] in place.
static void reverse_range(int lo, int hi)
{
    while(lo < hi)
    {
        char t = g_logBuffer[lo];
        g_logBuffer[lo] = g_logBuffer[hi];
        g_logBuffer[hi] = t;
        lo++;
        hi--;
    }
}

char *DiscordLog_GetBuffer(void)
{
    LightLock_Lock(&g_logLock);

    if(g_logCount == 0)
    {
        g_logHead = 0;
        g_logTail = 0;
    }
    else if(g_logTail != 0)
    {
        // Rotate the ring in place (three reversals) to linearize it
        reverse_range(0, DISCORD_LOG_SIZE - 1);
        reverse_range(0, DISCORD_LOG_SIZE - 1 - g_logTail);
        reverse_range(DISCORD_LOG_SIZE - g_logTail, DISCORD_LOG_SIZE - 1);
        g_logTail = 0;
        g_logHead = g_logCount;
    }

    g_logBuffer[g_logCount] = '\0';

    LightLock_Unlock(&g_logLock);

    return g_logBuffer;
}

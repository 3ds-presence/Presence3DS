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

#include <3ds.h>
#include "fmt.h"
#include "menu.h"
#include "draw.h"
#include "discord/discord_menu.h"
#include "discord/discord_rpc.h"
#include "discord/discord_log.h"
#include "discord/discord_config.h"

static bool discordMenuIsStopped(void)
{
    return g_discord_state == DISCORD_STOPPED;
}

static bool discordMenuIsNotStopped(void)
{
    return g_discord_state != DISCORD_STOPPED;
}

Menu discordMenu = {
    "Discord RPC",
    {
        { "Status/Info", METHOD, .method = &DiscordMenu_ShowAction },
        { "Start Discord RPC", METHOD, .method = &DiscordMenu_Start, .visibility = &discordMenuIsStopped },
        { "Stop Discord RPC", METHOD, .method = &DiscordMenu_Stop, .visibility = &discordMenuIsNotStopped },
        { "View Log", METHOD, .method = &DiscordMenu_ViewLog },
        { "Reload Config", METHOD, .method = &DiscordMenu_ReloadConfig },
        {},
    }
};

void DiscordMenu_Start(void)
{
    DiscordRPC_Start();
}

void DiscordMenu_Stop(void)
{
    DiscordRPC_Stop();
}

void DiscordMenu_ReloadConfig(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    Result res = DiscordConfig_Load();

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Discord RPC -- Reload Config");
        if(R_SUCCEEDED(res))
        {
            Draw_DrawString(10, 30, COLOR_WHITE, "Configuration reloaded.");
            Draw_DrawFormattedString(10, 50, COLOR_WHITE, "IP: %lu.%lu.%lu.%lu:%u",
                (g_server_ip >> 0) & 0xFF, (g_server_ip >> 8) & 0xFF,
                (g_server_ip >> 16) & 0xFF, (g_server_ip >> 24) & 0xFF,
                g_server_port);
            if(g_uuid[0])
                Draw_DrawFormattedString(10, 70, COLOR_WHITE, "UUID: %s", g_uuid);
        }
        else
        {
            Draw_DrawString(10, 30, COLOR_RED, "Failed to load config!");
            Draw_DrawString(10, 50, COLOR_WHITE, "Place /luma/discord_rpc.txt on SD.");
        }
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void DiscordMenu_ViewLog(void)
{
    char *logBuf = DiscordLog_GetBuffer();
    u32 scrollPos = 0;
    u32 lineCount = 0;
    u32 displayLines = 20; // How many lines fit on screen

    // Count lines roughly
    char *p = logBuf;
    while(*p)
    {
        if(*p == '\n')
            lineCount++;
        p++;
    }

    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Discord RPC -- Logs (B to exit)");

        // Display log lines starting from scrollPos
        u32 line = 0;
        u32 displayed = 0;
        u32 posY = 30;
        char *lineStart = logBuf;

        while(*lineStart && displayed < displayLines)
        {
            // Find end of line
            char *lineEnd = strchr(lineStart, '\n');
            if(!lineEnd)
                lineEnd = lineStart + strlen(lineStart);

            char saved = *lineEnd;
            *lineEnd = '\0';

            if(line >= scrollPos)
            {
                Draw_DrawString(10, posY, COLOR_WHITE, lineStart);
                posY += SPACING_Y;
                displayed++;
            }

            *lineEnd = saved;
            lineStart = lineEnd;
            if(*lineStart == '\n')
                lineStart++;
            line++;
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);
        if(pressed & KEY_DOWN && scrollPos + displayLines < lineCount)
            scrollPos++;
        else if(pressed & KEY_UP && scrollPos > 0)
            scrollPos--;
        else if(pressed & KEY_B)
            break;
    }
    while(!menuShouldExit);
}

void DiscordMenu_ShowAction(void)
{
    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Discord RPC -- Status");

        u32 posY = 30;

        // Display state
        const char *stateStr;
        u32 stateColor;
        switch(g_discord_state)
        {
            case DISCORD_STOPPED:  stateStr = "Stopped";   stateColor = COLOR_WHITE; break;
            case DISCORD_LOGIN:    stateStr = "Logging in"; stateColor = RGB565(0x1F, 0x1F, 0x00); break;
            case DISCORD_VERIFY:   stateStr = "Verifying"; stateColor = RGB565(0x1F, 0x1F, 0x00); break;
            case DISCORD_ACTIVE:   stateStr = "Connected"; stateColor = COLOR_GREEN; break;
            case DISCORD_ERROR:    stateStr = "Error";     stateColor = COLOR_RED; break;
            default:               stateStr = "Unknown";   stateColor = COLOR_WHITE; break;
        }

        posY = Draw_DrawFormattedString(10, posY, stateColor, "State: %s\n", stateStr);

        LightLock_Lock(&g_discord_lock);
        posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE, "Status: %s\n", g_discord_status);
        LightLock_Unlock(&g_discord_lock);

        if(g_uuid[0])
        {
            posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE, "UUID: %s\n", g_uuid);
        }

        if(g_config_loaded)
        {
            posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE,
                "Server: %lu.%lu.%lu.%lu:%u\n",
                (g_server_ip >> 0) & 0xFF, (g_server_ip >> 8) & 0xFF,
                (g_server_ip >> 16) & 0xFF, (g_server_ip >> 24) & 0xFF,
                g_server_port);
        }
        else
        {
            posY = Draw_DrawString(10, posY, COLOR_RED, "No config loaded!\n");
        }

        posY = Draw_DrawString(10, posY, COLOR_WHITE, "\nPress B to go back.");

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}
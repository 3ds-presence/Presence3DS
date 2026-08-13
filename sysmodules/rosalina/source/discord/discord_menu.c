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

#include <string.h>
#include <3ds.h>
#include "fmt.h"
#include "menu.h"
#include "draw.h"
#include "discord/discord_menu.h"
#include "discord/discord_rpc_main.h"
#include "discord/discord_log.h"
#include "discord/discord_config.h"
#include "discord/user_prefs.h"

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
        { "User Preferences", METHOD, .method = &DiscordMenu_EditPrefs },
        {},
    }
};

// Returns: 0 = cancel, 1 = launch, 2 = launch + never ask again
static int DiscordMenu_ShowUnsafeWarning(void)
{
    int choice = 0; // 0 = no choice yet, 1 = launch, 2 = launch + never ask
    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Discord RPC -- Security Warning");

        u32 y = 40;
        y = Draw_DrawString(10, y, COLOR_RED,
            "WARNING: You are using a private server!");
        y += SPACING_Y;
        y = Draw_DrawString(10, y, COLOR_WHITE,
            "Only use it if you trust the server owner.");
        y += SPACING_Y;
        y = Draw_DrawFormattedString(10, y, COLOR_WHITE,
            "Current Host: %s:%u", g_server_host, g_server_port);
        y += SPACING_Y * 2;

        y = Draw_DrawString(10, y, COLOR_WHITE,
            "A - Launch Discord RPC");
        y += SPACING_Y;
        y = Draw_DrawString(10, y, COLOR_WHITE,
            "B - Cancel");
        y += SPACING_Y;
        y = Draw_DrawString(10, y, COLOR_WHITE,
            "Y - Launch and don't show this again");

        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE,
            "Presence3DS %s", PRESENCE3DS_VERSION);
        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInput();
        if(pressed & KEY_A)
        {
            choice = 1;
            break;
        }
        else if(pressed & KEY_B)
        {
            choice = 0;
            break;
        }
        else if(pressed & KEY_Y)
        {
            choice = 2;
            break;
        }
    }
    while(!menuShouldExit);

    return choice;
}

void DiscordMenu_Start(void)
{
    // Load config if not already loaded
    if(!g_config_loaded)
    {
        DiscordLog_Printf("[CMD] Loading config...\n");
        DiscordConfig_Load();
    }

    // Load user preferences if not already loaded
    if(!g_prefs_loaded)
    {
        DiscordLog_Printf("[CMD] Loading user preferences...\n");
        UserPrefs_Load();
    }

    // If config is loaded and host is not the official one, check warning
    if(g_config_loaded && strcmp(g_server_host, "3ds-presence.top") != 0)
    {
        if(!g_pref_values[PREFS_ALLOW_UNSAFE])
        {
            int choice = DiscordMenu_ShowUnsafeWarning();
            if(choice == 0)
                return; // Cancelled by user
            else if(choice == 2)
            {
                g_pref_values[PREFS_ALLOW_UNSAFE] = true;
                UserPrefs_Save(); // Persist the choice
            }
        }
    }

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
            Draw_DrawFormattedString(10, 50, COLOR_WHITE, "Server: %s:%u",
                g_server_host, g_server_port);
            if(g_uuid[0])
                Draw_DrawFormattedString(10, 70, COLOR_WHITE, "UUID: %s", g_uuid);
        }
        else
        {
            Draw_DrawString(10, 30, COLOR_RED, "Failed to load config!");
            Draw_DrawString(10, 50, COLOR_WHITE, "Place /luma/discord_rpc.txt on SD.");
        }
        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE, "Presence3DS %s", PRESENCE3DS_VERSION);
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

void DiscordMenu_EditPrefs(void)
{
    // Load current prefs from SD
    UserPrefs_Load();

    // Start on the first visible preference (label non-empty)
    int selection = 0;
    while(g_user_prefs[selection].label[0] == '\0')
        selection = (selection + 1) % PREFS_COUNT;

    do
    {
        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "User Preferences");
        Draw_DrawString(10, 22, COLOR_WHITE, "A: toggle   B: save & exit");

        u32 y = 40;
        for(u32 i = 0; i < PREFS_COUNT; i++)
        {
            // Skip hidden preferences (empty label)
            if(g_user_prefs[i].label[0] == '\0')
                continue;

            u32 color = (int)i == selection ? RGB565(0x1F, 0x3F, 0x00) : COLOR_WHITE; // highlight selected

            // Draw checkbox
            if(g_pref_values[i])
                Draw_DrawString(10, y, color, "[*]");
            else
                Draw_DrawString(10, y, color, "[ ]");

            Draw_DrawFormattedString(40, y, color, g_user_prefs[i].label);
            y += 20;
        }

        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE, "Presence3DS %s", PRESENCE3DS_VERSION);
        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInput();
        if(pressed & KEY_DOWN)
        {
            do { selection = (selection + 1) % PREFS_COUNT; }
            while(g_user_prefs[selection].label[0] == '\0');
        }
        else if(pressed & KEY_UP)
        {
            do { selection = (selection - 1 + PREFS_COUNT) % PREFS_COUNT; }
            while(g_user_prefs[selection].label[0] == '\0');
        }
        else if(pressed & KEY_A)
            g_pref_values[selection] = !g_pref_values[selection];
        else if(pressed & KEY_B)
        {
            // Save and exit
            UserPrefs_Save();
            break;
        }
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
                "Server: %s:%u\n",
                g_server_host, g_server_port);
        }
        else
        {
            posY = Draw_DrawString(10, posY, COLOR_RED, "No config loaded!\n");
        }

        // Preferences
        posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE,
            "\nHide Mii: %s  Hide Home: %s\n",
            g_pref_values[PREFS_HIDE_MII] ? "ON" : "OFF",
            g_pref_values[PREFS_HIDE_HOME] ? "ON" : "OFF");
        posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE,
            "Auto-Start: %s\n",
            g_pref_values[PREFS_AUTO_START] ? "ON" : "OFF");

        posY = Draw_DrawString(10, posY, COLOR_WHITE, "\nPress B to go back.");

        Draw_DrawFormattedString(10, SCREEN_BOT_HEIGHT - 20, COLOR_TITLE, "Presence3DS %s", PRESENCE3DS_VERSION);
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

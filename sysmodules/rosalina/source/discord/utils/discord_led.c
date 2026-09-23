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
#include "discord/utils/discord_led.h"
#include "discord/discord_log.h"

// Discord blurple: rgb(88, 101, 242)
// Using Gamma 2.2 correction rgb(25, 33, 227) + normalization rgb(28, 37, 255)
#define DISCORD_LED_R 28
#define DISCORD_LED_G 37
#define DISCORD_LED_B 255

#define DISCORD_LED_BLINK_SECS 3

void DiscordUpdate_BlinkLed(void)
{
    InfoLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));

    // delay = total animation time in 1/16 s (seconds * 0x10).
    // loopDelay = 0xFF: play the pattern only once, then stop.
    pattern.delay      = DISCORD_LED_BLINK_SECS * 0x10;
    pattern.smoothing  = 0x5F;
    pattern.loopDelay  = 0xFF;
    pattern.blinkSpeed = 0x00;

    // 5 LEDs on, 5 LEDs off, repeated 3 times (total 30 LEDs)
    for(int i = 1; i < 31; i += 10)
    {
        for(int j = 0; j < 5; j++)
        {
            pattern.redPattern[i + j]   = DISCORD_LED_R;
            pattern.greenPattern[i + j] = DISCORD_LED_G;
            pattern.bluePattern[i + j]  = DISCORD_LED_B;
        }
    }

    Result res = mcuHwcInit();
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[LED] mcuHwcInit failed (0x%08lx)\n", (u32)res);
        return;
    }

    res = MCUHWC_SetInfoLedPattern(&pattern);
    mcuHwcExit();

    if(R_FAILED(res))
        DiscordLog_Printf("[LED] SetInfoLedPattern failed (0x%08lx)\n", (u32)res);
}

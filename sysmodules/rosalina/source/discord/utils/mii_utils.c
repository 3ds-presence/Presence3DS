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
#include <stdio.h>
#include <3ds.h>
#include "discord/utils/mii_utils.h"

/*
 * This function retrieves the console's main Mii via FRD_GetMyMii()
 * and returns the CFLStoreData without the CRC16 (0x62 bytes - 2 bytes = 96 bytes) as a
 * lowercase hexadecimal string.
 *
 * This hex string can be saved to a binary file on PC and used with:
 *   python mii2studio.py mii.bin output.mnms 3ds
 *
 * Format reference: https://www.3dbrew.org/wiki/Mii#Mii_format
 */

char *mii_get_raw_hex(char *out, size_t out_size)
{
    CFLStoreData mii;
    Result res;

    if (out == NULL || out_size < MII_OUT_SIZE)
        return NULL;

    // Initialize friend services (force user service, needed for sysmodule context)
    res = frdInit(true);
    if (R_FAILED(res))
        return NULL;

    // Get the console's main Mii data
    // FRD_GetMyMii returns the current user's Mii as a CFLStoreData (FriendMii)
    res = FRD_GetMyMii(&mii);
    if (R_FAILED(res))
    {
        frdExit();
        return NULL;
    }

    frdExit();

    // Convert all raw bytes (excluding crc16) to hex string
    for (u32 i = 0; i < CFLSTORE_SIZE - 2; i++)
    {
        sprintf(out + (i * 2), "%02x", ((u8 *)&mii)[i]);
    }

    out[(CFLSTORE_SIZE - 2) * 2] = '\0';

    return out;
}
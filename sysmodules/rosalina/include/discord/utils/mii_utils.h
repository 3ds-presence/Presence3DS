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

#pragma once

#include <stddef.h>

#define CFLSTORE_SIZE 0x62 // CFLStoreData = MiiData(0x5E) + pad[2] + crc16[2] = 98 bytes

// Hex output excluding the last 2 bytes (crc16): (0x62 - 2) * 2 + null = 189
#define MII_OUT_SIZE  ((CFLSTORE_SIZE - 2) * 2 + 1)

/**
 * @brief Retrieves the console's main Mii (CFLStoreData) as a lowercase hex string.
 *
 * The hex string represents the full CFLStoreData structure (0x62 bytes = 98 bytes).
 *
 * @param out      Output buffer to write the hex string into.
 * @param out_size Size of the output buffer. Must be at least 189 (188 hex chars + null terminator).
 * @return         Pointer to `out` on success, NULL on failure.
 */
char *mii_get_raw_hex(char *out, size_t out_size);
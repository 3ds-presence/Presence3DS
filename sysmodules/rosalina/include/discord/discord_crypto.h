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

#pragma once

#include <3ds/types.h>

// AES-256-CBC encrypt with PKCS7 padding (in-place capable)
// input_len: length of input data
// output: buffer of at least input_len + 16 bytes
// output_len: [out] final padded length (multiple of 16)
void discord_aes256_cbc_encrypt(const u8 key[32], const u8 iv[16],
                                const u8 *input, u32 input_len,
                                u8 *output, u32 *output_len);

// Convert binary to lowercase hex string
// hex must be at least len * 2 + 1 bytes
void bytes_to_hex(const u8 *bytes, u32 len, char *hex);
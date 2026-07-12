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
 *
 *   AES-256-CBC software implementation (FIPS 197).
 *   Only encryption is needed for this protocol.
 */

#include <string.h>
#include "discord/aes256_cbc.h"
#include "discord/discord_util.h"

// AES S-box
static const u8 sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

// Round constants
static const u8 Rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

// Multiply by 2 in GF(2^8)
static u8 xtime(u8 a)
{
    return (u8)((a << 1) ^ (((a >> 7) & 1) * 0x1b));
}

// GF(2^8) multiplication of a by b
static u8 gmult(u8 a, u8 b)
{
    u8 result = 0;
    u8 i;
    for(i = 0; i < 8; i++)
    {
        if(b & 1)
            result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

// SubBytes step
static void sub_bytes(u8 *state)
{
    u32 i;
    for(i = 0; i < 16; i++)
        state[i] = sbox[state[i]];
}

// ShiftRows step
static void shift_rows(u8 *state)
{
    u8 tmp;

    // Row 1: shift left by 1
    tmp = state[1];
    state[1]  = state[5];
    state[5]  = state[9];
    state[9]  = state[13];
    state[13] = tmp;

    // Row 2: shift left by 2
    tmp = state[2];
    state[2]  = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6]  = state[14];
    state[14] = tmp;

    // Row 3: shift left by 3 (right by 1)
    tmp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7]  = state[3];
    state[3]  = tmp;
}

// MixColumns step
static void mix_columns(u8 *state)
{
    u8 i, a[4];
    for(i = 0; i < 4; i++)
    {
        u8 *col = &state[i * 4];
        a[0] = col[0];
        a[1] = col[1];
        a[2] = col[2];
        a[3] = col[3];
        col[0] = gmult(a[0], 2) ^ gmult(a[1], 3) ^ a[2] ^ a[3];
        col[1] = a[0] ^ gmult(a[1], 2) ^ gmult(a[2], 3) ^ a[3];
        col[2] = a[0] ^ a[1] ^ gmult(a[2], 2) ^ gmult(a[3], 3);
        col[3] = gmult(a[0], 3) ^ a[1] ^ a[2] ^ gmult(a[3], 2);
    }
}

// AddRoundKey step
static void add_round_key(u8 *state, const u32 *rk)
{
    u32 i;
    for(i = 0; i < 4; i++)
    {
        u32 w = rk[i];
        state[i * 4]     ^= (u8)(w >> 24);
        state[i * 4 + 1] ^= (u8)(w >> 16);
        state[i * 4 + 2] ^= (u8)(w >> 8);
        state[i * 4 + 3] ^= (u8)(w);
    }
}

// AES-256 key expansion (14 rounds, 60 words)
// rk must be 60 u32 elements
static void key_expansion(const u8 key[32], u32 rk[60])
{
    u32 i;
    u32 temp;

    // First 8 words from the key
    for(i = 0; i < 8; i++)
    {
        rk[i] = ((u32)key[4 * i] << 24) | ((u32)key[4 * i + 1] << 16) |
                ((u32)key[4 * i + 2] << 8)  | (u32)key[4 * i + 3];
    }

    for(i = 8; i < 60; i++)
    {
        temp = rk[i - 1];
        if(i % 8 == 0)
        {
            // RotWord
            temp = (temp << 8) | (temp >> 24);
            // SubWord
            temp = ((u32)sbox[(temp >> 24) & 0xff] << 24) |
                   ((u32)sbox[(temp >> 16) & 0xff] << 16) |
                   ((u32)sbox[(temp >> 8) & 0xff] << 8)   |
                   ((u32)sbox[temp & 0xff]);
            // XOR with Rcon
            temp ^= (u32)Rcon[i / 8] << 24;
        }
        else if(i % 8 == 4)
        {
            // SubWord only
            temp = ((u32)sbox[(temp >> 24) & 0xff] << 24) |
                   ((u32)sbox[(temp >> 16) & 0xff] << 16) |
                   ((u32)sbox[(temp >> 8) & 0xff] << 8)   |
                   ((u32)sbox[temp & 0xff]);
        }
        rk[i] = rk[i - 8] ^ temp;
    }
}

// Encrypt a single 16-byte block
static void aes256_encrypt_block(const u32 rk[60], u8 block[16])
{
    u32 i;

    // Initial round
    add_round_key(block, rk);

    // 14 rounds
    for(i = 1; i < 14; i++)
    {
        sub_bytes(block);
        shift_rows(block);
        mix_columns(block);
        add_round_key(block, &rk[i * 4]);
    }

    // Final round (no MixColumns)
    sub_bytes(block);
    shift_rows(block);
    add_round_key(block, &rk[14 * 4]);
}

void aes256_cbc_encrypt(const u8 key[32], const u8 iv[16],
                        const u8 *input, u32 input_len,
                        u8 *output, u32 *output_len)
{
    u32 i, j;
    u32 rk[60];
    u8 buffer[16];
    u8 chain[16];
    u32 num_blocks;
    u32 padded_len;

    // PKCS7 padding: add (16 - input_len % 16) bytes, each = (16 - input_len % 16)
    u8 padval = (u8)(16 - (input_len % 16));
    padded_len = input_len + padval;

    // Key expansion
    key_expansion(key, rk);

    // Copy IV to chain
    memcpy(chain, iv, 16);

    // Process full blocks
    num_blocks = padded_len / 16;
    for(i = 0; i < num_blocks; i++)
    {
        // Copy input or padding to buffer and XOR with chain
        for(j = 0; j < 16; j++)
        {
            if(i * 16 + j < input_len)
                buffer[j] = input[i * 16 + j] ^ chain[j];
            else
                buffer[j] = padval ^ chain[j];
        }

        // Encrypt
        aes256_encrypt_block(rk, buffer);

        // Output and update chain
        memcpy(&output[i * 16], buffer, 16);
        memcpy(chain, buffer, 16);
    }

    *output_len = padded_len;
}

void bytes_to_hex(const u8 *bytes, u32 len, char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    u32 i;
    for(i = 0; i < len; i++)
    {
        hex[i * 2]     = hex_chars[bytes[i] >> 4];
        hex[i * 2 + 1] = hex_chars[bytes[i] & 0x0f];
    }
    hex[len * 2] = '\0';
}

void aes256_cbc_encrypt_to_hex(const u8 *input, u32 input_len,
                               const u8 key[32], const u8 iv[16],
                               char *hex_out, u32 *output_len)
{
    u8 buf[64]; // 64 is enough for maximum padded size used in this protocol
    u32 alen;

    aes256_cbc_encrypt(key, iv, input, input_len, buf, &alen);
    bytes_to_hex(buf, alen, hex_out);

    if(output_len) *output_len = alen;
}
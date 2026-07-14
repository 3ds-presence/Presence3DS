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
#include "discord/utils/discord_util.h"

void discord_url_encode(const char *src, char *dst, u32 dst_size)
{
    u32 si, di = 0;
    for(si = 0; src[si] && di < dst_size - 3; si++)
    {
        u8 c = (u8)src[si];
        if(c == ' ')
        {
            dst[di++] = '+';
        }
        else if((c >= '0' && c <= '9') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[di++] = c;
        }
        else
        {
            static const char hex[] = "0123456789ABCDEF";
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0xf];
        }
    }
    dst[di] = '\0';
}

bool discord_parse_field(const char *resp, const char *field, char *value, u32 max_len)
{
    const char *p = strstr(resp, field);
    if(!p) return false;
    p += strlen(field);
    if(*p != '=') return false;
    p++;
    u32 i;
    for(i = 0; i < max_len - 1 && p[i] && p[i] != '&' && p[i] != '\r' && p[i] != '\n'; i++)
        value[i] = p[i];
    value[i] = '\0';
    return i > 0;
}

void discord_pack_counter(u64 counter, u8 buf[8])
{
    buf[0] = (u8)(counter >> 56);
    buf[1] = (u8)(counter >> 48);
    buf[2] = (u8)(counter >> 40);
    buf[3] = (u8)(counter >> 32);
    buf[4] = (u8)(counter >> 24);
    buf[5] = (u8)(counter >> 16);
    buf[6] = (u8)(counter >> 8);
    buf[7] = (u8)(counter);
}
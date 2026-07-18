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
#include <3ds.h>
#include <arpa/inet.h>
#include "discord/utils/soc_utils.h"
#include "discord/discord_log.h"

#define DNS_OUTBUF_SIZE 0x1A88

int resolve_host(const char *host, u32 *ip_out)
{
    if(host == NULL || ip_out == NULL || host[0] == '\0')
        return -1;

    // Try to parse as an IP address first
    u32 ip = inet_addr(host);
    if(ip != INADDR_NONE)
    {
        *ip_out = ip;
        return 0;
    }

    // The input is a domain name, resolve via soc:U IPC
    // Uses the same IPC command 0xD (SOCU_GetHostByName) as libctru's gethostbyname
    Handle socHandle = 0;
    Result res = srvGetServiceHandle(&socHandle, "soc:U");
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[ERR] Failed to get soc:U handle (0x%08lx)\n", (u32)res);
        return -1;
    }

    u32 *cmdbuf = getThreadCommandBuffer();
    u32 *staticbufs = getThreadStaticBuffers();
    u32 saved_static[2];
    u32 name_len = strlen(host) + 1;
    static u8 outbuf[DNS_OUTBUF_SIZE];

    // Build IPC command for SOCU_GetHostByName (cmd 0xD)
    // Parameters: name_len, outbuf_size
    // Static buffers: name (input), outbuf (output)
    cmdbuf[0] = IPC_MakeHeader(0xD, 2, 2); // 0xD0082
    cmdbuf[1] = name_len;
    cmdbuf[2] = sizeof(outbuf);
    cmdbuf[3] = ((name_len) << 14) | 0xC02; // Static buffer descriptor for input name
    cmdbuf[4] = (u32)host;

    // Save and set output static buffer
    saved_static[0] = staticbufs[0];
    saved_static[1] = staticbufs[1];
    staticbufs[0] = IPC_Desc_StaticBuffer(sizeof(outbuf), 0);
    staticbufs[1] = (u32)outbuf;

    res = svcSendSyncRequest(socHandle);

    // Restore static buffers
    staticbufs[0] = saved_static[0];
    staticbufs[1] = saved_static[1];

    svcCloseHandle(socHandle);

    if(R_FAILED(res))
    {
        DiscordLog_Printf("[ERR] SOCU_GetHostByName IPC failed (0x%08lx)\n", (u32)res);
        return -1;
    }

    if(cmdbuf[1] != 0)
    {
        DiscordLog_Printf("[ERR] SOCU_GetHostByName failed for '%s': %d\n", host, (int)cmdbuf[1]);
        return -1;
    }

    // Parse the output buffer to extract the first resolved IPv4 address
    // Buffer layout (from libctru):
    //   offset 4: u32 num_results
    //   offset 8: host name (null-terminated)
    //   offset 0x1908: array of IP addresses (each 16 bytes)
    //   IP address format at 0x1908: first 4 bytes are the IPv4 address in network byte order
    u32 num_results;
    memcpy(&num_results, outbuf + 4, sizeof(num_results));
    if(num_results == 0)
    {
        DiscordLog_Printf("[ERR] No results for '%s'\n", host);
        return -1;
    }

    // Copy the first IPv4 address (network byte order, at offset 0x1908)
    *ip_out = *(u32 *)(outbuf + 0x1908);

    DiscordLog_Printf("[DNS] Resolved '%s' to %lu.%lu.%lu.%lu\n",
        host,
        (*ip_out >> 0) & 0xFF,
        (*ip_out >> 8) & 0xFF,
        (*ip_out >> 16) & 0xFF,
        (*ip_out >> 24) & 0xFF);

    return 0;
}

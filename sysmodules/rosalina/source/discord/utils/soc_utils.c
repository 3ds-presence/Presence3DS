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
#include <errno.h>
#include <3ds.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "minisoc.h"
#include "discord/utils/soc_utils.h"
#include "discord/discord_log.h"

#define DNS_OUTBUF_SIZE 0x1A88

// SOCU fcntl constants (see libctru source/services/soc/soc_fcntl.c)
#define SOCU_F_SETFL    4
#define O_NONBLOCK_3DS  0x4
#define CONNECT_POLL_SLICE_MS 250

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

    int dns_ret = (int)cmdbuf[1];
    if(dns_ret == 0)
        dns_ret = _net_convert_error((int)cmdbuf[2]);

    if(dns_ret != 0)
    {
        DiscordLog_Printf("[ERR] DNS failed for '%s': %d\n", host, dns_ret);
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

// Open a dedicated soc:U session. SOCU identifies clients by PID, so a
// separate session operates on the same socket table as the session opened
// by miniSocInit() (same pattern as resolve_host above).
static Result socu_open_handle(Handle *out)
{
    return srvGetServiceHandle(out, "soc:U");
}

// SOCU fcntl (cmd 0x13), restricted to F_SETFL (the only use we need).
// arg_3ds is the raw 3DS flag bitmask (O_NONBLOCK_3DS | ...).
// Returns 0 on success, -1 on failure.
static int socu_fcntl_setfl(Handle socHandle, int sockfd, int arg_3ds)
{
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x13, 3, 2); // 0x1300C2
    cmdbuf[1] = (u32)sockfd;
    cmdbuf[2] = (u32)SOCU_F_SETFL;
    cmdbuf[3] = (u32)arg_3ds;
    cmdbuf[4] = IPC_Desc_CurProcessId();

    Result res = svcSendSyncRequest(socHandle);
    if(R_FAILED(res))
        return -1;

    int ret = (int)cmdbuf[1];
    if(ret == 0)
        ret = _net_convert_error((int)cmdbuf[2]);

    return (ret < 0) ? -1 : 0;
}

// SOCU connect (cmd 0x6), same IPC as minisoc's socConnect but without
// collapsing the POSIX error code, so EINPROGRESS can be detected.
// Returns: 0 = connected, 1 = in progress (non-blocking), -1 = error.
static int socu_connect(Handle socHandle, int sockfd, const struct sockaddr *addr,
                        socklen_t addrlen, int *out_err)
{
    u8 tmpaddr[0x1c];
    socklen_t tmp_addrlen = (addr->sa_family == AF_INET) ? 8 : 0x1c;
    if(addrlen < tmp_addrlen)
        return -1;

    memset(tmpaddr, 0, sizeof(tmpaddr));
    tmpaddr[0] = (u8)tmp_addrlen;
    tmpaddr[1] = addr->sa_family;
    memcpy(&tmpaddr[2], addr->sa_data, tmp_addrlen - 2);

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x6, 2, 4); // 0x60084
    cmdbuf[1] = (u32)sockfd;
    cmdbuf[2] = (u32)addrlen;
    cmdbuf[3] = IPC_Desc_CurProcessId();
    cmdbuf[5] = IPC_Desc_StaticBuffer(tmp_addrlen, 0);
    cmdbuf[6] = (u32)tmpaddr;

    Result res = svcSendSyncRequest(socHandle);
    if(R_FAILED(res))
        return -1;

    int ret = (int)cmdbuf[1];
    if(ret == 0)
        ret = _net_convert_error((int)cmdbuf[2]);

    if(ret == 0)
        return 0;

    // "Not finished yet": the non-blocking connect reports EINPROGRESS.
    if(ret == EINPROGRESS || ret == -EINPROGRESS)
        return 1;

    if(out_err != NULL)
        *out_err = (ret < 0) ? -ret : ret;
    return -1;
}

// Retrieve pending error on a socket (SO_ERROR), for diagnostics.
// Returns 0 on success, -1 on failure; *out_err = raw SOCU error value.
static int socu_get_so_error(Handle socHandle, int sockfd, int *out_err)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    u32 *staticbufs = getThreadStaticBuffers();
    u32 saved_static[2];
    int err = 0;
    socklen_t len = sizeof(err);

    cmdbuf[0] = IPC_MakeHeader(0x11, 4, 2); // 0x110102
    cmdbuf[1] = (u32)sockfd;
    cmdbuf[2] = (u32)SOL_SOCKET;
    cmdbuf[3] = (u32)SO_ERROR;
    cmdbuf[4] = (u32)len;
    cmdbuf[5] = IPC_Desc_CurProcessId();

    saved_static[0] = staticbufs[0];
    saved_static[1] = staticbufs[1];
    staticbufs[0] = IPC_Desc_StaticBuffer(len, 0);
    staticbufs[1] = (u32)&err;

    Result res = svcSendSyncRequest(socHandle);

    staticbufs[0] = saved_static[0];
    staticbufs[1] = saved_static[1];

    if(R_FAILED(res))
        return -1;

    int ret = (int)cmdbuf[1];
    if(ret == 0)
        ret = _net_convert_error((int)cmdbuf[2]);
    if(ret < 0)
        return -1;

    *out_err = err;
    return 0;
}

// Set or clear O_NONBLOCK on a socket created with socSocket().
// Returns 0 on success, -1 on failure.
static int soc_set_nonblocking(int sockfd, int nonblock)
{
    if(sockfd < 0)
        return -1;

    Handle socHandle;
    if(R_FAILED(socu_open_handle(&socHandle)))
        return -1;

    int r = socu_fcntl_setfl(socHandle, sockfd, nonblock ? O_NONBLOCK_3DS : 0);
    svcCloseHandle(socHandle);
    return r;
}

// Connect attempt + wait loop
// Returns 0 on success, -1 on failure/timeout/cancellation.
static int socu_connect_wait(Handle socHandle, int sockfd, const struct sockaddr *addr,
                             socklen_t addrlen, Handle cancel_event, u64 timeout_ns)
{
    int err = 0;
    int r = socu_connect(socHandle, sockfd, addr, addrlen, &err);
    if(r == 0)
        return 0;
    if(r < 0)
    {
        DiscordLog_Printf("[ERR] Connect error: %d\n", err);
        return -1;
    }

    // In progress: poll for writability in slices, checking cancellation
    // each slice so we never block more than CONNECT_POLL_SLICE_MS.
    const u64 slice_ns = CONNECT_POLL_SLICE_MS * 1000LL * 1000;
    u64 max_slices = (timeout_ns + slice_ns - 1) / slice_ns;
    if(max_slices == 0)
        max_slices = 1;

    for(u64 i = 0; i < max_slices; i++)
    {
        if(cancel_event != 0 && svcWaitSynchronization(cancel_event, 0) == 0)
        {
            DiscordLog_Printf("[WARN] Connect cancelled\n");
            return -1;
        }

        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int n = socPoll(&pfd, 1, CONNECT_POLL_SLICE_MS);
        if(n < 0)
        {
            DiscordLog_Printf("[ERR] Connect poll failed: %d\n", n);
            return -1;
        }
        if(n == 0)
            continue; // slice timeout, keep waiting

        if(pfd.revents & POLLOUT)
            return 0;

        // POLLERR / POLLHUP: connection refused or failed.
        int soerr = 0;
        if(socu_get_so_error(socHandle, sockfd, &soerr) == 0)
            DiscordLog_Printf("[ERR] Connect failed (SO_ERROR=%d)\n", soerr);
        return -1;
    }
    DiscordLog_Printf("[ERR] Connect timeout after %lu ms\n",
                      (u32)(timeout_ns / 1000000));
    return -1;
}

int soc_connect_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen,
                        Handle cancel_event, u64 timeout_ns)
{
    if(sockfd < 0 || addr == NULL || addrlen == 0)
        return -1;

    Handle socHandle;
    if(R_FAILED(socu_open_handle(&socHandle)))
    {
        DiscordLog_Printf("[ERR] soc_connect_timeout: cannot open soc:U\n");
        return -1;
    }

    int nb_set = (soc_set_nonblocking(sockfd, 1) == 0);
    int ret = socu_connect_wait(socHandle, sockfd, addr, addrlen, cancel_event, timeout_ns);

    if(nb_set)
        soc_set_nonblocking(sockfd, 0);
    svcCloseHandle(socHandle);
    return ret;
}

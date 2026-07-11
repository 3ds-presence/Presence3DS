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
#include <stdio.h>
#include <3ds.h>
#include <arpa/inet.h>
#include "minisoc.h"
#include "discord/discord_http.h"
#include "discord/discord_log.h"

// HTTP connection timeout: 5 seconds, recv timeout: 3 seconds
#define CONNECT_TIMEOUT_NS (5LL * 1000 * 1000 * 1000)
#define RECV_TIMEOUT_NS    (3LL * 1000 * 1000 * 1000)

int discord_http_post(const char *host, u16 port, const char *path,
                      const char *body, char *response, u32 resp_size,
                      Handle timeout_event)
{
    int sockfd;
    struct sockaddr_in addr;
    ssize_t sent, received;
    char req[512];
    int req_len;
    int ret = -1;
    struct linger linger_opt;

    // Build HTTP request
    req_len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, host, port, strlen(body), body);

    if(req_len <= 0 || (u32)req_len >= sizeof(req))
    {
        DiscordLog_Printf("[ERR] Request too long\n");
        return -1;
    }

    // Create socket
    sockfd = socSocket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
    {
        DiscordLog_Printf("[ERR] Socket creation failed: %d\n", sockfd);
        return -1;
    }

    // Set up server address
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    // Connect with timeout check using svcWaitSynchronization
    while(true)
    {
        // Check for cancellation
        if(timeout_event != 0 && svcWaitSynchronization(timeout_event, 0) == 0)
        {
            DiscordLog_Printf("[WARN] HTTP cancelled before connect\n");
            goto cleanup;
        }

        int res = socConnect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
        if(res == 0)
            break;

        // Connection failed - check if it's a transient error
        if(res < -10000)
        {
            DiscordLog_Printf("[ERR] Socket service broken: %d\n", res);
            goto cleanup;
        }

        // Retry with timeout check
        DiscordLog_Printf("[WARN] Connect failed: %d, retrying...\n", res);
        if(timeout_event != 0 && svcWaitSynchronization(timeout_event, 500 * 1000 * 1000LL) == 0)
            goto cleanup;
    }

    // Send HTTP request
    sent = socSend(sockfd, req, req_len, 0);
    if(sent != req_len)
    {
        DiscordLog_Printf("[ERR] Send failed: %d\n", (int)sent);
        goto cleanup;
    }

    // Receive response (with timeout via socPoll or checking event)
    {
        u32 total_received = 0;
        s32 n;

        while(total_received < resp_size - 1)
        {
            // Check for cancellation
            if(timeout_event != 0 && svcWaitSynchronization(timeout_event, 0) == 0)
            {
                DiscordLog_Printf("[WARN] HTTP cancelled during recv\n");
                goto cleanup;
            }

            // Poll with 500ms timeout for readability
            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            n = socPoll(&pfd, 1, 500);
            if(n < 0)
            {
                DiscordLog_Printf("[ERR] Poll failed: %d\n", n);
                goto cleanup;
            }
            else if(n == 0)
            {
                // Timeout on poll, check cancel and retry
                continue;
            }

            if(!(pfd.revents & POLLIN))
                break;

            received = socRecv(sockfd, response + total_received,
                               resp_size - 1 - total_received, 0);
            if(received <= 0)
                break;

            total_received += (u32)received;
        }

        if(total_received > 0)
        {
            response[total_received] = '\0';
            ret = 0; // Success, response is in buffer
        }
        else
        {
            DiscordLog_Printf("[ERR] No response received\n");
            ret = -1;
        }
    }

cleanup:
    // Force close with linger to avoid TIME_WAIT
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = 0;
    socSetsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));
    socClose(sockfd);

    return ret;
}
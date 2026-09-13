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

#ifndef SOC_UTILS_H
#define SOC_UTILS_H

#include <3ds.h>
#include <sys/socket.h>

// Resolve a host string to an IPv4 address in network byte order.
// Supports both IP addresses ("192.168.1.100") and domain names ("example.com").
// Returns 0 on success, -1 on failure.
int resolve_host(const char *host, u32 *ip_out);

// Connect with a timeout, so the call never blocks longer than timeout_ns,
// and can be cancelled early by signalling cancel_event (0 = no cancellation).
// IMPORTANT: after a failed or timed-out attempt the TCP attempt keeps running
// in the background: the caller MUST close the socket (socClose) afterwards.
// Returns 0 on success, -1 on failure/timeout/cancellation.
int soc_connect_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen,
                        Handle cancel_event, u64 timeout_ns);

// Short human-readable description of a POSIX errno
const char *soc_errno_str(int err);

// Extended version of socSocket(): same socket creation, but the reason of
// a failure
int soc_socket_ex(int domain, int type, int protocol,
                  int *out_errno, int *out_raw, u32 *out_svcres);

#endif
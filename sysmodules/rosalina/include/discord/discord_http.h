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

#include <3ds/types.h>

// HTTP POST to the presence server
// host: IP string (e.g. "192.168.1.42")
// port: port number
// path: URL path (e.g. "/login")
// body: form-url-encoded body
// response: buffer to receive response (must be at least resp_size)
// resp_size: size of response buffer
// timeout_event: Handle to check for cancellation (signal = abort), or 0
// Returns 0 on success, negative on error
int discord_http_post(const char *host, u16 port, const char *path,
                      const char *body, char *response, u32 resp_size,
                      Handle timeout_event);
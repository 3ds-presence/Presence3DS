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
#include <3ds/result.h>
#include <stdbool.h>

// Callback invoked for each parsed KEY=VALUE line.
// Return false to stop parsing early, true to continue.
typedef bool (*ConfigReader_Handler)(const char *key, const char *value, void *userdata);

// Parse a key=value config file from SD.
// Lines starting with '#' are comments (ignored), empty lines are ignored.
// Trailing whitespace/CR/LF are stripped from values.
// The handler is called for every successfully parsed KEY=VALUE pair.
Result ConfigReader_Parse(const char *path, ConfigReader_Handler handler, void *userdata);
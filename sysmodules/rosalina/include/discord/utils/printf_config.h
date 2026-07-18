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

/**
 * Printf configuration for the Luma3DS Discord RPC module.
 *
 * Only %s, %u, %llX/%016llX, %llu format specifiers are used.
 * All floating-point/exponential/writeback features are disabled to reduce code size.
 */

#ifndef PRINTF_CONFIG_H_
#define PRINTF_CONFIG_H_

/* Enable soft aliases so that snprintf() in source code maps to snprintf_() */
#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_SOFT 1

/* Disable floating-point specifiers %f, %F, %e, %E, %g, %G, %a, %A */
#define PRINTF_SUPPORT_DECIMAL_SPECIFIERS      0
#define PRINTF_SUPPORT_EXPONENTIAL_SPECIFIERS  0

/* Disable the %n writeback specifier (not used) */
#define PRINTF_SUPPORT_WRITEBACK_SPECIFIER     0

/* No floating-point is used, so we do not need double internally */
#define PRINTF_USE_DOUBLE_INTERNALLY          0

/* long long is needed for %016llX and %llu */
#define PRINTF_SUPPORT_LONG_LONG              1

#endif /* PRINTF_CONFIG_H_ */
/*
*   Presence3DS addition for Luma3DS: ndm:u radio yield handshake.
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

#include "types.h"

// Meaning of each bit of the handshake state:
//   NDMU_BIT_ASK:    a game requested an exclusive state while Rosalina owns the radio
//   NDMU_BIT_ACK:    Rosalina acknowledged -> radio released for the whole yield session
//   NDMU_BIT_LEAVE:  the game's real LeaveExclusiveState went through (grace starts)

// Lifecycle of the bits:
//   - a game EnterExclusiveState on a radio-exclusive mode sets
//     NDMU_BIT_ASK; Rosalina releases the radio then sets NDMU_BIT_ACK
//     (or withdraws NDMU_BIT_ASK on refusal, e.g. gdb/input redirection);
//   - NDMU_BIT_ACK stays set for the whole session so further Enters and
//     the real LeaveExclusiveState pass through untouched;
//   - that Leave sets NDMU_BIT_LEAVE -> Rosalina restores the radio and
//     resets the whole state.

typedef enum
{
    NDMU_BIT_ASK   = 1u,
    NDMU_BIT_ACK   = 2u,
    NDMU_BIT_LEAVE = 4u,
} NdmuYieldBit;

void ndmuYieldStateSetBits(u32 bits);
void ndmuYieldStateClearBits(u32 bits);
void ndmuYieldStateReset(void);
u32 ndmuYieldStateGet(void);

// True if acked, false if refused or timeout. The caller must have
// set NDMU_BIT_ASK before calling this.
bool ndmuWaitForAck(void);

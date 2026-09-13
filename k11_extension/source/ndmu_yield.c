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

#include "ndmu_yield.h"
#include "synchronization.h"
#include "globals.h"

static vu32 ndmuYieldState;

void ndmuYieldStateSetBits(u32 bits)
{
    u32 old;
    do
    {
        old = __ldrex((s32 *)&ndmuYieldState);
    }
    while(__strex((s32 *)&ndmuYieldState, (s32)(old | bits)));
    __dmb();
}

void ndmuYieldStateClearBits(u32 bits)
{
    u32 old;
    do
    {
        old = __ldrex((s32 *)&ndmuYieldState);
    }
    while(__strex((s32 *)&ndmuYieldState, (s32)(old & ~bits)));
    __dmb();
}

void ndmuYieldStateReset(void)
{
    u32 old;
    do
    {
        old = __ldrex((s32 *)&ndmuYieldState);
        (void)old;
    }
    while(__strex((s32 *)&ndmuYieldState, 0));
    __dmb();
}

u32 ndmuYieldStateGet(void)
{
    __dmb();
    return ndmuYieldState;
}

bool ndmuWaitForAck(void)
{
    u32 iters = 0;
    for(;;)
    {
        u32 state = ndmuYieldStateGet();
        if(state & NDMU_BIT_ACK)
            return true; // acked: the radio was released for the session
        if(!(state & NDMU_BIT_ASK) || iters >= 100u)
            return false; // the pending ASK was cleared (Rosalina refused)
                          // or ~1 s timeout elapsed
        SleepThread(10 * 1000 * 1000LL);
        iters++;
    }
}
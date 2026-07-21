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

#include <3ds.h>
#include "discord/customRPC/read_memory.h"
#include "csvc.h"

#define DEST_ADDR 0x00100000

static Handle g_mappedProcessHandle = 0;
static u32 g_mappedPid = 0;
static u32 g_mappedPageStart = 0;
static u32 g_mappedSize = 0;
static bool g_isMapped = false;

void CustomRPC_Init(void)
{
    g_mappedPid = 0;
    g_isMapped = false;
}

bool CustomRPC_MapPage(u32 pid)
{
    if(g_isMapped)
        CustomRPC_UnmapPage();

    if(R_FAILED(svcOpenProcess(&g_mappedProcessHandle, pid)))
        return false;

    // Get full code region info (same as MemoryViewer in process_list.c)
    s64 textSize, rodataSize, dataSize, startAddr;
    svcGetProcessInfo(&textSize, g_mappedProcessHandle, 0x10002);
    svcGetProcessInfo(&rodataSize, g_mappedProcessHandle, 0x10003);
    svcGetProcessInfo(&dataSize, g_mappedProcessHandle, 0x10004);
    svcGetProcessInfo(&startAddr, g_mappedProcessHandle, 0x10005);

    u32 codeStartAddress = (u32)startAddr;
    u32 codeTotalSize = (u32)(textSize + rodataSize + dataSize);

    Result res = svcMapProcessMemoryEx(CUR_PROCESS_HANDLE, DEST_ADDR,
                                        g_mappedProcessHandle, codeStartAddress,
                                        codeTotalSize, 0);
    if(R_SUCCEEDED(res))
    {
        g_mappedPid = pid;
        g_mappedPageStart = codeStartAddress;
        g_mappedSize = codeTotalSize;
        g_isMapped = true;
        return true;
    }

    svcCloseHandle(g_mappedProcessHandle);
    return false;
}

void CustomRPC_UnmapPage(void)
{
    if(!g_isMapped)
        return;

    svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE, DEST_ADDR, g_mappedSize);
    svcCloseHandle(g_mappedProcessHandle);
    g_mappedPid = 0;
    g_isMapped = false;
}

u8 CustomRPC_ReadByte(u32 address)
{
    if(!g_isMapped)
        return 0;

    u32 offset = address - g_mappedPageStart;
    if(offset >= g_mappedSize)
        return 0;

    return *(volatile u8*)(DEST_ADDR + offset);
}

u16 CustomRPC_ReadHalfWord(u32 address)
{
    if(!g_isMapped)
        return 0;

    u32 offset = address - g_mappedPageStart;
    if(offset + sizeof(u16) > g_mappedSize)
        return 0;

    return *(volatile u16*)(DEST_ADDR + offset);
}

u32 CustomRPC_ReadWord(u32 address)
{
    if(!g_isMapped)
        return 0;

    u32 offset = address - g_mappedPageStart;
    if(offset + sizeof(u32) > g_mappedSize)
        return 0;

    return *(volatile u32*)(DEST_ADDR + offset);
}


u32 CustomRPC_GetMappedPid(void)
{
    return g_mappedPid;
}
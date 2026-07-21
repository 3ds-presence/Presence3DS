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
#include <string.h>
#include "discord/utils/printf.h"
#include "discord/utils/discord_util.h"

#include "discord/discord_activity.h"
#include "discord/discord_log.h"
#include "discord/customRPC/read_memory.h"
#include "pmdbgext.h"

#define SMDH_READ_SIZE  0x36C0
#define SMDH_NUM_TITLES 0x10
#define SMDH_TITLES_OFFSET 0x0008
#define SMDH_TITLE_ENTRY_SIZE 0x200
#define SMDH_SHORT_DESC_OFFSET 0x00
#define SMDH_LONG_DESC_OFFSET 0x80
#define SMDH_PUBLISHER_OFFSET 0x180

// Static buffer for SMDH data to avoid stack overflow (RPC thread stack is only 16KB)
static u8 smdh_buffer[SMDH_READ_SIZE] __attribute__((aligned(32)));

// SMDH language index for each CFG_Language value
static const u8 smdh_lang_for_cfg[] = {
    0, // CFG_LANGUAGE_JP -> Japanese (SMDH index 0)
    1, // CFG_LANGUAGE_EN -> English (SMDH index 1)
    2, // CFG_LANGUAGE_FR -> French (SMDH index 2)
    3, // CFG_LANGUAGE_DE -> German (SMDH index 3)
    4, // CFG_LANGUAGE_IT -> Italian (SMDH index 4)
    5, // CFG_LANGUAGE_ES -> Spanish (SMDH index 5)
    6, // CFG_LANGUAGE_ZH -> Chinese (SMDH index 6)
    7, // CFG_LANGUAGE_KO -> Korean (SMDH index 7)
    1, // CFG_LANGUAGE_NL -> Dutch -> English fallback
    1, // CFG_LANGUAGE_PT -> Portuguese -> English fallback
    1, // CFG_LANGUAGE_RU -> Russian -> English fallback
    6, // CFG_LANGUAGE_TW -> Traditional Chinese -> Chinese
};

// Fallback language order (after system language, checked one by one)
static const u8 smdh_lang_fallback[] = {
    1, // English
    2, // French
    3, // German
    4, // Italian
    5, // Spanish
    0, // Japanese
    6, // Chinese
    7, // Korean
};

// Read the SMDH (icon section) from a title's ExeFS
// Uses the same approach as FBI: ARCHIVE_SAVEDATA_AND_CONTENT with binary paths
static Result read_smdh(u64 titleId, FS_MediaType mediaType, u8 *smdh_out)
{
    Result res = 0;
    Handle fileHandle = 0;

    // Binary archive path: [low32(titleId), high32(titleId), mediaType, 0]
    u32 archivePath[4] = {
        (u32)(titleId & 0xFFFFFFFF),
        (u32)((titleId >> 32) & 0xFFFFFFFF),
        mediaType,
        0x00000000
    };

    FS_Path archivePathBin;
    archivePathBin.type = PATH_BINARY;
    archivePathBin.size = sizeof(archivePath);
    archivePathBin.data = archivePath;

    // Binary file path for ExeFS section "icon":
    // [offset_low, offset_high, section_type=ExeFS(2), "icon", padding]
    static const u32 filePath[5] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};

    FS_Path filePathBin;
    filePathBin.type = PATH_BINARY;
    filePathBin.size = sizeof(filePath);
    filePathBin.data = filePath;

    res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SAVEDATA_AND_CONTENT,
                                  archivePathBin, filePathBin, FS_OPEN_READ, 0);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[DBG] Failed to open SMDH for %016llX (m=%d): %08lX\n",
                          titleId, (int)mediaType, res);
        return res;
    }

    u32 bytesRead = 0;
    res = FSFILE_Read(fileHandle, &bytesRead, 0, smdh_out, SMDH_READ_SIZE);
    (void)bytesRead;
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[DBG] Failed to read SMDH for %016llX: %08lX\n", titleId, res);
    }

    FSFILE_Close(fileHandle);
    return res;
}

// Check if an SMDH language index has a non-empty long description
static bool smdh_lang_has_name(const u8 *smdh, u8 langIndex)
{
    const u16 *desc = (const u16 *)(smdh + SMDH_TITLES_OFFSET + langIndex * SMDH_TITLE_ENTRY_SIZE + SMDH_LONG_DESC_OFFSET);
    return desc[0] != 0;
}

// Copy strings from an SMDH language entry to output buffers
static void smdh_copy_lang(const u8 *smdh, u8 langIndex,
                            char *name_out, size_t name_size,
                            char *publisher_out, size_t publisher_size)
{
    u32 entryOffset = SMDH_TITLES_OFFSET + langIndex * SMDH_TITLE_ENTRY_SIZE;
    const u16 *longDesc = (const u16 *)(smdh + entryOffset + SMDH_LONG_DESC_OFFSET);
    const u16 *publisher = (const u16 *)(smdh + entryOffset + SMDH_PUBLISHER_OFFSET);

    utf16_to_utf8((uint8_t *)name_out, longDesc, name_size - 1);
    name_out[name_size - 1] = '\0';
    utf16_to_utf8((uint8_t *)publisher_out, publisher, publisher_size - 1);
    publisher_out[publisher_size - 1] = '\0';
}

// Extract the short description (name) and publisher from SMDH data
// Priority: system language -> English -> other languages
static void extract_smdh_strings(const u8 *smdh,
                                  char *name_out, size_t name_size,
                                  char *publisher_out, size_t publisher_size)
{
    name_out[0] = '\0';
    publisher_out[0] = '\0';

    // Debug: dump short desc of all 8 SMDH languages (first 8 UTF-16 chars each)
    for(u32 lang = 0; lang < 8; lang++)
    {
        const u16 *desc = (const u16 *)(smdh + SMDH_TITLES_OFFSET + lang * SMDH_TITLE_ENTRY_SIZE + SMDH_SHORT_DESC_OFFSET);
        DiscordLog_Printf("[DBG] L%d:%04X%04X%04X%04X%04X%04X%04X%04X\n",
            lang, desc[0], desc[1], desc[2], desc[3], desc[4], desc[5], desc[6], desc[7]);
    }

    // 1. Try system language first
    u8 cfgLang = CFG_LANGUAGE_EN;
    u8 smdhLang = 1; // English default
    if(R_SUCCEEDED(cfguInit()))
    {
        CFGU_GetSystemLanguage(&cfgLang);
        cfguExit();
        if(cfgLang < sizeof(smdh_lang_for_cfg))
            smdhLang = smdh_lang_for_cfg[cfgLang];
    }

    DiscordLog_Printf("[DBG] System language=%d -> SMDH index=%d\n", cfgLang, smdhLang);

    if(smdh_lang_has_name(smdh, smdhLang))
    {
        smdh_copy_lang(smdh, smdhLang, name_out, name_size, publisher_out, publisher_size);
        DiscordLog_Printf("[DBG] Using system lang %d: name=%s pub=%s\n", smdhLang, name_out, publisher_out);
        return;
    }

    // 2. Try English if different from system language
    if(smdhLang != 1 && smdh_lang_has_name(smdh, 1))
    {
        smdh_copy_lang(smdh, 1, name_out, name_size, publisher_out, publisher_size);
        DiscordLog_Printf("[DBG] Using English: name=%s pub=%s\n", name_out, publisher_out);
        return;
    }

    // 3. Try all other languages in fallback order
    for(u32 i = 0; i < sizeof(smdh_lang_fallback); i++)
    {
        u8 lang = smdh_lang_fallback[i];
        if(lang == smdhLang || lang == 1) continue; // already tried
        if(smdh_lang_has_name(smdh, lang))
        {
            smdh_copy_lang(smdh, lang, name_out, name_size, publisher_out, publisher_size);
            DiscordLog_Printf("[DBG] Using fallback lang %d: name=%s pub=%s\n", lang, name_out, publisher_out);
            return;
        }
    }
}

// Get the current title ID, media type and PID
static u64 get_current_app_info(FS_MediaType *outMediaType, u32 *outPid)
{
    FS_ProgramInfo programInfo;
    u32 pid;
    u32 launchFlags;

    if(R_FAILED(PMDBG_GetCurrentAppInfo(&programInfo, &pid, &launchFlags)))
    {
        if(outMediaType) *outMediaType = MEDIATYPE_SD;
        if(outPid) *outPid = 0;
        return 0;
    }

    if(outMediaType) *outMediaType = programInfo.mediaType;
    if(outPid) *outPid = pid;
    u64 titleId = programInfo.programId;
    DiscordLog_Printf("[DBG] Current title ID: %016llX, PID: %lu, mediaType: %d\n", titleId, pid, (int)programInfo.mediaType);
    return titleId;
}

void create_activity_string(char* buffer, size_t buffer_size) {
    char titleid[17];
    char name[512] = "";
    char publisher[256] = "";
    char name_enc[1536] = "";
    char pub_enc[768] = "";
    FS_MediaType mediaType = MEDIATYPE_SD;
    u32 currentPid = 0;

    u64 tid = get_current_app_info(&mediaType, &currentPid);
    if(tid != 0)
    {
        snprintf(titleid, sizeof(titleid), "%016llX", tid);

        // Read SMDH to get name and publisher
        if(R_SUCCEEDED(read_smdh(tid, mediaType, smdh_buffer)))
        {
            extract_smdh_strings(smdh_buffer, name, sizeof(name), publisher, sizeof(publisher));

            DiscordLog_Printf("[DBG] Title name: %s, Publisher: %s\n", name, publisher);
        }
        else
        {
            DiscordLog_Printf("[DBG] Could not read SMDH for %016llX\n", tid);
        }

        // CustomRPC: manage page mapping based on PID
        if(CustomRPC_GetMappedPid() != currentPid)
        {
            CustomRPC_UnmapPage();
            CustomRPC_MapPage(currentPid);
        }
        u8 val = CustomRPC_ReadByte(0x004FE6E0);
        DiscordLog_Printf("[DBG] CustomRPC read from 0x004FE6E0: 0x%02X (%u)\n", val, val);
    }
    else
    {
        snprintf(titleid, sizeof(titleid), "0000000000000000");
        snprintf(name, sizeof(name), "Home Menu");
        snprintf(publisher, sizeof(publisher), "Nintendo");
        CustomRPC_UnmapPage();
    }

    // URL-encode name and publisher to prevent '&', '=' etc. from breaking the query string
    discord_url_encode(name, name_enc, sizeof(name_enc));
    discord_url_encode(publisher, pub_enc, sizeof(pub_enc));

    snprintf(buffer, buffer_size, "titleid=%s&name=%s&publisher=%s", titleid, name_enc, pub_enc);
}

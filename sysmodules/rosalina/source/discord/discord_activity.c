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
#include "discord/discord_session.h"
#include "discord/customRPC/read_memory.h"
#include "discord/customRPC/memory_config.h"
#include "discord/user_prefs.h"
#include "pmdbgext.h"

#define SMDH_READ_SIZE  0x36C0
#define SMDH_NUM_TITLES 0x10
#define SMDH_NUM_LANGS  12
#define SMDH_TITLES_OFFSET 0x0008
#define SMDH_TITLE_ENTRY_SIZE 0x200
#define SMDH_SHORT_DESC_OFFSET 0x00
#define SMDH_LONG_DESC_OFFSET 0x80
#define SMDH_PUBLISHER_OFFSET 0x180
#define SMDH_REGION_LOCKOUT_OFFSET 0x2018
#define SMDH_REGION_BITS_MASK 0x7F

// Static buffer for SMDH data to avoid stack overflow (RPC thread stack is only 16KB)
static u8 smdh_buffer[SMDH_READ_SIZE] __attribute__((aligned(32)));

// Fallback language order (after system language, checked one by one)
static const u8 smdh_lang_fallback[] = {
    1,  // English
    2,  // French
    3,  // German
    4,  // Italian
    5,  // Spanish
    0,  // Japanese
    6,  // Chinese
    7,  // Korean
    8,  // Dutch
    9,  // Portuguese
    10, // Russian
    11, // Traditional Chinese
};
_Static_assert(sizeof(smdh_lang_fallback) == SMDH_NUM_LANGS, "Fallback array size mismatch");
_Static_assert(RPC_LANGUAGE_MAX == SMDH_NUM_LANGS, "RPC language count mismatch");

// Read the SMDH (icon section) from a title's ExeFS
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

static bool smdh_is_region_free(const u8 *smdh)
{
    u32 regionLockout = *(const u32 *)(smdh + SMDH_REGION_LOCKOUT_OFFSET);
    return (regionLockout & SMDH_REGION_BITS_MASK) == SMDH_REGION_BITS_MASK;
}

// Region free (homebrew): short, region-locked: long description. 
static u32 smdh_name_offset(const u8 *smdh)
{
    return smdh_is_region_free(smdh) ? SMDH_SHORT_DESC_OFFSET : SMDH_LONG_DESC_OFFSET;
}

// Check if an SMDH language index has a non-empty name
static bool smdh_lang_has_name(const u8 *smdh, u8 langIndex)
{
    const u16 *desc = (const u16 *)(smdh + SMDH_TITLES_OFFSET + langIndex * SMDH_TITLE_ENTRY_SIZE + smdh_name_offset(smdh));
    return desc[0] != 0;
}

// Copy strings from an SMDH language entry to output buffers
static void smdh_copy_lang(const u8 *smdh, u8 langIndex,
                            char *name_out, size_t name_size,
                            char *publisher_out, size_t publisher_size)
{
    u32 entryOffset = SMDH_TITLES_OFFSET + langIndex * SMDH_TITLE_ENTRY_SIZE;
    const u16 *name = (const u16 *)(smdh + entryOffset + smdh_name_offset(smdh));
    const u16 *publisher = (const u16 *)(smdh + entryOffset + SMDH_PUBLISHER_OFFSET);

    utf16_to_utf8((uint8_t *)name_out, name, name_size - 1);
    name_out[name_size - 1] = '\0';
    utf16_to_utf8((uint8_t *)publisher_out, publisher, publisher_size - 1);
    publisher_out[publisher_size - 1] = '\0';
}

// Extract the name (short or long description) and publisher from SMDH data
// Priority: RPC language preference -> system language -> fallback list
static void extract_smdh_strings(const u8 *smdh,
                                  char *name_out, size_t name_size,
                                  char *publisher_out, size_t publisher_size)
{
    name_out[0] = '\0';
    publisher_out[0] = '\0';

    u8 smdhLang = CFG_LANGUAGE_EN; // English default
    u8 prefLang = g_pref_values[PREFS_RPC_LANGUAGE];

    if(prefLang > RPC_LANGUAGE_AUTO && prefLang <= RPC_LANGUAGE_MAX)
        smdhLang = prefLang - 1;
    else if(R_SUCCEEDED(cfguInit()))
    {
        u8 cfgLang = 0;
        CFGU_GetSystemLanguage(&cfgLang);
        cfguExit();
        if(cfgLang < SMDH_NUM_LANGS)
            smdhLang = cfgLang;
    }

    DiscordLog_Printf("[DBG] SMDH index=%d, region free=%d\n", smdhLang, smdh_is_region_free(smdh));

    if(smdh_lang_has_name(smdh, smdhLang))
    {
        smdh_copy_lang(smdh, smdhLang, name_out, name_size, publisher_out, publisher_size);
        DiscordLog_Printf("[DBG] Using lang %d: name=%s pub=%s\n", smdhLang, name_out, publisher_out);
        return;
    }

    // Fallback on fail
    for(u32 i = 0; i < sizeof(smdh_lang_fallback); i++)
    {
        u8 lang = smdh_lang_fallback[i];
        if(lang == smdhLang) continue; // already tried
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

// Fetch CustomRPC script for a title if not already fetched. 
// "-": no script to fetch
// Returns 0 on success, non-zero on error.
static int fetch_script_for_title(u64 tid)
{
    if(tid == 0)
    {
        CustomRPC_UnmapPage();
        CustomRPC_ClearConfig();
        return 0;
    }

    if(!CustomRPC_TriedForTitle(tid))
    {
        CustomRPC_ClearConfig();

        char code[CUSTOMRPC_EXTRA_SIZE];
        int script_res = discord_get_script(tid, code, sizeof(code));
        if(script_res != 0)
        {
            CustomRPC_ClearConfig();
            DiscordLog_Printf("[RPC] Script fetch failed for %016llX (r=%d)\n",
                              tid, script_res);
            return -1;
        }

        // "-" (or empty) = the server has no script for this title: nothing
        // to load, but remember it so we don't refetch on every iteration.
        if(code[0] == '-' || code[0] == '\0')
        {
            DiscordLog_Printf("[RPC] No code for %016llX\n", tid);
            CustomRPC_MarkTried(tid);
            return 0;
        }

        CustomRPC_LoadConfigFromString(tid, code);
    }

    return 0;
}

// One activity loop iteration: query the current title once, fetch the
// server-side script for it if needed, then build the activity string.
// Returns 0 on success, non-zero when the script fetch failed so the
// activity loop can fall back to reconnection.
int discord_activity_tick(char *buffer, size_t buffer_size)
{
    FS_MediaType mediaType;
    u32 pid;
    u64 tid = get_current_app_info(&mediaType, &pid);

    if (!g_pref_values[PREFS_DISABLE_CUSTOMRPC]) {
        int fetch_res = fetch_script_for_title(tid);
        if(fetch_res != 0)
            return fetch_res;
    } else {
        CustomRPC_UnmapPage();
        CustomRPC_ClearConfig();
    }
    
    create_activity_string(buffer, buffer_size, tid, mediaType, pid);
    return 0;
}

// Builds the activity string from the given title (SMDH name/publisher) and
// the CustomRPC memory values. Does no network I/O.
void create_activity_string(char* buffer, size_t buffer_size, u64 tid, FS_MediaType mediaType, u32 currentPid) {
    char titleid[17] = "";
    char name[512] = "";
    char publisher[256] = "";
    char name_enc[1536] = "";
    char pub_enc[768] = "";
    char extra_buf[CUSTOMRPC_EXTRA_SIZE + 1] = "";
    char extra_enc[CUSTOMRPC_EXTRA_SIZE * 3] = "";

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

        if(CustomRPC_GetMappedPid() != currentPid)
        {
            CustomRPC_UnmapPage();

            if(CustomRPC_HasConfig())
                CustomRPC_MapPage(currentPid);
        }

        // Refresh memory values and build extra string if config is loaded
        if(CustomRPC_HasConfig())
        {
            CustomRPC_BuildExtraString(extra_buf, sizeof(extra_buf));
        }
    }
    else
    {
        snprintf(titleid, sizeof(titleid), "0000000000000000");
        snprintf(name, sizeof(name), "Home Menu");
        snprintf(publisher, sizeof(publisher), "Nintendo");
        CustomRPC_UnmapPage();
        CustomRPC_ClearConfig();
    }

    // URL-encode name and publisher to prevent '&', '=' etc. from breaking the query string
    discord_url_encode(name, name_enc, sizeof(name_enc));
    discord_url_encode(publisher, pub_enc, sizeof(pub_enc));

    snprintf(buffer, buffer_size, "titleid=%s&name=%s&publisher=%s",
             titleid, name_enc, pub_enc);

    // Append CustomRPC extra data if present
    if(extra_buf[0])
    {
        discord_url_encode(extra_buf, extra_enc, sizeof(extra_enc));
        snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer),
                 "&extra=%s", extra_enc);
    }
}
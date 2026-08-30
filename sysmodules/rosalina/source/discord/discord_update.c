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
 *
 *   Update detection: the server may send a "ver" field in the login
 *   response. When it advertises a version newer than the running build,
 *   the new version is persisted in /presence3ds/.upd so the "Upd avail"
 *   banner survives reboots. The file is removed at startup once the
 *   running version catches up (update installed).
 */

#include <string.h>
#include <3ds.h>
#include "discord/discord_update.h"
#include "discord/discord_rpc_main.h" // PRESENCE3DS_VERSION
#include "discord/discord_log.h"

static volatile bool g_upd_available = false;

// ---------------------------------------------------------------------------
//  Version parsing / comparison
// ---------------------------------------------------------------------------

// Parse up to 3 numeric components of a "vX.Y.Z"-style string.
// Missing components default to 0 ("v1.2" == "1.2.0").
static void parse_ver_components(const char *s, u32 out[3])
{
    out[0] = out[1] = out[2] = 0;

    // Skip leading non-digits (e.g. the 'v' prefix)
    while(*s != '\0' && (*s < '0' || *s > '9'))
        s++;

    for(int i = 0; i < 3 && *s != '\0'; i++)
    {
        u32 v = 0;
        while(*s >= '0' && *s <= '9')
        {
            v = v * 10 + (u32)(*s - '0');
            s++;
        }
        out[i] = v;

        if(*s == '.')
            s++;
        else
            break;
    }
}

int DiscordUpdate_CompareVersions(const char *a, const char *b)
{
    u32 va[3], vb[3];

    parse_ver_components(a, va);
    parse_ver_components(b, vb);

    for(int i = 0; i < 3; i++)
    {
        if(va[i] != vb[i])
            return va[i] < vb[i] ? -1 : 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
//  .upd file handling
// ---------------------------------------------------------------------------

// Read the version stored in DISCORD_UPD_PATH. Returns false if the file
// doesn't exist or can't be read.
static bool upd_read_file(char *ver, u32 max_len)
{
    Handle h;
    Result res = FSUSER_OpenFileDirectly(&h, ARCHIVE_SDMC,
        fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, DISCORD_UPD_PATH),
        FS_OPEN_READ, 0);
    if(R_FAILED(res))
        return false;

    u32 read = 0;
    res = FSFILE_Read(h, &read, 0, ver, max_len - 1);
    FSFILE_Close(h);
    if(R_FAILED(res))
        return false;

    ver[read] = '\0';

    // Strip trailing whitespace / CR / LF
    size_t len = strlen(ver);
    while(len > 0 && (ver[len - 1] == '\r' || ver[len - 1] == '\n' ||
                      ver[len - 1] == ' ' || ver[len - 1] == '\t'))
        ver[--len] = '\0';

    return len > 0;
}

static void upd_write_file(const char *ver)
{
    Handle h;
    u32 len = (u32)strlen(ver);

    Result res = FSUSER_OpenFileDirectly(&h, ARCHIVE_SDMC,
        fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, DISCORD_UPD_PATH),
        FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[UPD] Can't write " DISCORD_UPD_PATH " (0x%08lx)\n", (u32)res);
        return;
    }

    u32 written = 0;
    FSFILE_SetSize(h, len);
    FSFILE_Write(h, &written, 0, ver, len, FS_WRITE_FLUSH);
    FSFILE_Close(h);

    DiscordLog_Printf("[UPD] New version %s available (marker saved)\n", ver);
}

static void upd_delete_file(void)
{
    FS_Archive sd;
    Result res = FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[UPD] Can't open SDMC to remove marker (0x%08lx)\n", (u32)res);
        return;
    }

    res = FSUSER_DeleteFile(sd, fsMakePath(PATH_ASCII, DISCORD_UPD_PATH));
    FSUSER_CloseArchive(sd);

    if(R_SUCCEEDED(res))
        DiscordLog_Printf("[UPD] Marker removed (update installed)\n");
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void DiscordUpdate_Init(void)
{
    char ver[32];

    g_upd_available = false;

    if(!upd_read_file(ver, sizeof(ver)))
        return; // No pending update marker on SD

    if(DiscordUpdate_CompareVersions(ver, PRESENCE3DS_VERSION) > 0)
    {
        // Running build is still older than the stored version:
        // keep the marker so the banner stays visible.
        g_upd_available = true;
        DiscordLog_Printf("[UPD] Update to %s pending (running %s)\n",
            ver, PRESENCE3DS_VERSION);
    }
    else
    {
        // Running build caught up with (or passed) the stored version:
        // the update has been applied, remove the marker.
        upd_delete_file();
    }
}

bool DiscordUpdate_CheckRemote(const char *ver)
{
    if(ver == NULL || ver[0] == '\0')
        return g_upd_available;

    if(DiscordUpdate_CompareVersions(ver, PRESENCE3DS_VERSION) > 0)
    {
        g_upd_available = true;
        upd_write_file(ver);
    }
    else
    {
        // Server no longer advertises a newer version: clear any stale marker
        if(g_upd_available)
            upd_delete_file();
        g_upd_available = false;
    }

    return g_upd_available;
}

bool DiscordUpdate_Available(void)
{
    return g_upd_available;
}
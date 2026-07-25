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

#include <string.h>
#include <3ds.h>
#include "discord/utils/config_reader.h"

Result ConfigReader_Parse(const char *path, ConfigReader_Handler handler, void *userdata)
{
    Result res;
    Handle fileHandle;
    char buf[256];
    u64 total;

    res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
                                  fsMakePath(PATH_EMPTY, ""),
                                  fsMakePath(PATH_ASCII, path),
                                  FS_OPEN_READ, 0);
    if(R_FAILED(res))
        return res;

    // Read the whole file
    {
        u32 read;
        res = FSFILE_Read(fileHandle, &read, 0, buf, sizeof(buf) - 1);
        if(R_FAILED(res))
        {
            FSFILE_Close(fileHandle);
            return res;
        }
        buf[read] = '\0';
        total = read;
    }

    FSFILE_Close(fileHandle);

    (void)total;

    // Parse lines
    char *line = buf;
    char *next;

    while(line && *line)
    {
        // Find next line
        next = strchr(line, '\n');
        if(next)
        {
            *next = '\0';
            next++;
        }

        // Skip empty lines and comments
        if(line[0] != '\0' && line[0] != '#')
        {
            // Find '='
            char *eq = strchr(line, '=');
            if(eq)
            {
                *eq = '\0';
                const char *key = line;
                const char *val = eq + 1;

                // Strip trailing whitespace from key
                char *pk = (char *)key + strlen(key) - 1;
                while(pk >= key && (*pk == ' ' || *pk == '\t'))
                    *(pk--) = '\0';

                // Strip trailing whitespace/CR/LF from value
                size_t vallen = strlen(val);
                while(vallen > 0 && (val[vallen - 1] == '\r' || val[vallen - 1] == '\n' || val[vallen - 1] == ' ' || val[vallen - 1] == '\t'))
                    vallen--;
                // We'll work with a temporary null-terminated area
                // Since buf is mutable, we can just write the null terminator
                // But val points into buf, and we already null-terminated at eq.
                // We need a temporary copy or just write into buf.
                // Let's just pass the val as-is and let the callback handle trimming if needed.
                // Actually the values from the original code trim trailing whitespace.
                // Since val points into buf, we can safely write null.
                ((char *)val)[vallen] = '\0';

                if(!handler(key, val, userdata))
                    break;
            }
        }

        line = next;
    }

    return 0;
}
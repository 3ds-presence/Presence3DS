#include <3ds.h>
#include <stdio.h>

#include "discord/discord_activity.h"
#include "discord/discord_log.h"
#include "pmdbgext.h"

static u64 get_current_title_id(void)
{
    FS_ProgramInfo programInfo;
    u32 pid;
    u32 launchFlags;

    if(R_FAILED(PMDBG_GetCurrentAppInfo(&programInfo, &pid, &launchFlags)))
        return 0;

    u64 titleId = programInfo.programId;
    DiscordLog_Printf("[DBG] Current title ID: %016llX\n", titleId);
    return titleId;
}


void create_activity_string(char* buffer, size_t buffer_size) {
    char titleid[17];
    
    {
        u64 tid = get_current_title_id();
        if(tid != 0)
            snprintf(titleid, sizeof(titleid), "%016llX", tid);
        else
            snprintf(titleid, sizeof(titleid), "0000000000000000");
    }

    snprintf(buffer, buffer_size, "titleid=%s", titleid);
}
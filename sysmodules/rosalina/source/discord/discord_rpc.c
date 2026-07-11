/*
 *   All RPC operations run in their own MyThread (like InputRedirection).
 *   miniSocInit/Exit, sockets, crypto — all in the same thread.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include "minisoc.h"
#include "menu.h"
#include "discord/discord_rpc.h"
#include "discord/discord_config.h"
#include "discord/discord_http.h"
#include "discord/discord_crypto.h"
#include "discord/sha256.h"
#include "discord/discord_log.h"

volatile DiscordState g_discord_state = DISCORD_STOPPED;
char g_discord_status[64] = "Stopped";
LightLock g_discord_lock;

static MyThread g_rpcThread;
static u8 CTR_ALIGN(8) g_rpcThreadStack[0x4000];
static volatile bool g_shouldStop;
static u64 g_counter;
static char g_ip_str[16];

// Event for signaling thread startup completion
static Handle g_rpcStartedEvent;

static bool parse_field(const char *r, const char *f, char *v, u32 m)
{
    const char *p = strstr(r, f);
    if(!p) return false;
    p += strlen(f);
    if(*p != '=') return false;
    p++;
    u32 i;
    for(i = 0; i < m-1 && p[i] && p[i] != '&' && p[i] != '\r' && p[i] != '\n'; i++) v[i] = p[i];
    v[i] = '\0';
    return i > 0;
}

static void set_state(DiscordState s, const char *st)
{
    LightLock_Lock(&g_discord_lock);
    g_discord_state = s;
    strncpy(g_discord_status, st, sizeof(g_discord_status)-1);
    g_discord_status[sizeof(g_discord_status)-1] = '\0';
    LightLock_Unlock(&g_discord_lock);
}

void DiscordRPC_ThreadMain(void)
{
    DiscordLog_Printf("[THREAD] Started\n");

    // Initialize network INSIDE the thread (like InputRedirection does)
    Result res = miniSocInit();
    if(R_FAILED(res))
    {
        DiscordLog_Printf("[ERR] miniSocInit failed (0x%08lx)\n", (u32)res);
        set_state(DISCORD_ERROR, "Network init failed");
        svcSignalEvent(g_rpcStartedEvent);
        return;
    }

    // Retry loop for socket creation (like InputRedirection)
    u32 tries = 15;
    int sock = socSocket(AF_INET, SOCK_STREAM, 0);
    while(sock == -1 && --tries > 0)
    {
        svcSleepThread(100 * 1000 * 1000LL);
        sock = socSocket(AF_INET, SOCK_STREAM, 0);
    }

    if(sock < 0)
    {
        DiscordLog_Printf("[ERR] Socket creation failed\n");
        set_state(DISCORD_ERROR, "Socket failed");
        miniSocExit();
        svcSignalEvent(g_rpcStartedEvent);
        return;
    }
    socClose(sock);

    // Signal that we're ready (like InputRedirection)
    svcSignalEvent(g_rpcStartedEvent);

    DiscordLog_Printf("[THREAD] Network OK, starting login...\n");

    // --- LOGIN ---
    {
        char body[128], resp[512], nonce[32];
        snprintf(body, sizeof(body), "uuid=%s", g_uuid);
        set_state(DISCORD_LOGIN, "Logging in...");

        int r = discord_http_post(g_ip_str, g_server_port, "/api/login",
                                   body, resp, sizeof(resp), 0);
        if(g_shouldStop) goto stop;

        if(r < 0 || !parse_field(resp, "nonce", nonce, sizeof(nonce)))
        {
            DiscordLog_Printf("[ERR] Login failed\n");
            set_state(DISCORD_ERROR, "Login failed");
            goto stop;
        }
        g_counter = strtoull(nonce, NULL, 10);
        DiscordLog_Printf("[AUTH] Nonce=%llu\n", g_counter);
    }

    // --- VERIFY ---
    {
        u8 key[32], iv[16]={0}, block[16];
        u32 clen;
        char hex[33], body[256], resp[512], ok[8];

        for(u32 i = 0; i < 32; i++)
        {
            char h[3] = {g_aes_key_hex[i*2], g_aes_key_hex[i*2+1], 0};
            key[i] = (u8)strtoul(h, NULL, 16);
        }

        memset(block, 0, 16);
        block[7]=(u8)g_counter; block[6]=(u8)(g_counter>>8);
        block[5]=(u8)(g_counter>>16); block[4]=(u8)(g_counter>>24);
        block[3]=(u8)(g_counter>>32); block[2]=(u8)(g_counter>>40);
        block[1]=(u8)(g_counter>>48); block[0]=(u8)(g_counter>>56);
        memset(&block[8], 0x08, 8);

        discord_aes256_cbc_encrypt(key, iv, block, 16, block, &clen);
        bytes_to_hex(block, 16, hex);
        snprintf(body, sizeof(body), "uuid=%s&cipher_hex=%s", g_uuid, hex);

        int r = discord_http_post(g_ip_str, g_server_port, "/api/login/verify",
                                   body, resp, sizeof(resp), 0);
        if(g_shouldStop) goto stop;

        if(r == 0 && parse_field(resp, "success", ok, sizeof(ok)) && strcmp(ok, "true") == 0)
        {
            g_counter++;
            DiscordLog_Printf("[ACTIVE] Connected! counter=%llu\n", g_counter);
            set_state(DISCORD_ACTIVE, "Connected to Discord");
        }
        else
        {
            DiscordLog_Printf("[ERR] Verify failed\n");
            set_state(DISCORD_ERROR, "Verify failed");
            goto stop;
        }
    }

    // --- ACTIVITY LOOP ---
    while(!g_shouldStop)
    {
        svcSleepThread(10LL * 1000 * 1000 * 1000);
        if(g_shouldStop) break;

        u8 key[32], iv[16]={0}, hash[32], auth[48];
        u32 alen;
        char hin[256], ah[97], se[64], de[64], body[512], resp[512], ok[8];
        SHA256_CTX sha;

        for(u32 i = 0; i < 32; i++)
        {
            char h[3] = {g_aes_key_hex[i*2], g_aes_key_hex[i*2+1], 0};
            key[i] = (u8)strtoul(h, NULL, 16);
        }

        static const char *hstr = "Playing on 3DS";
        static const char *dstr = "Luma3DS Discord RPC";

        // URL encode
        u32 si, di = 0;
        for(si = 0; hstr[si] && di < sizeof(se)-3; si++) {
            u8 c = (u8)hstr[si];
            if(c == ' ') se[di++] = '+';
            else if((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_'||c=='.'||c=='~') se[di++]=c;
            else { se[di++]='%'; se[di++]= "0123456789ABCDEF"[c>>4]; se[di++]="0123456789ABCDEF"[c&0xf]; }
        }
        se[di]='\0';
        di=0;
        for(si = 0; dstr[si] && di < sizeof(de)-3; si++) {
            u8 c = (u8)dstr[si];
            if(c == ' ') de[di++] = '+';
            else if((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_'||c=='.'||c=='~') de[di++]=c;
            else { de[di++]='%'; de[di++]="0123456789ABCDEF"[c>>4]; de[di++]="0123456789ABCDEF"[c&0xf]; }
        }
        de[di]='\0';

        snprintf(hin, sizeof(hin), "%s%s%d", hstr, dstr, 0);
        sha256_init(&sha);
        sha256_update(&sha, (const u8*)hin, strlen(hin));
        sha256_final(&sha, hash);

        memset(auth,0,sizeof(auth));
        auth[0]=(u8)(g_counter>>56); auth[1]=(u8)(g_counter>>48);
        auth[2]=(u8)(g_counter>>40); auth[3]=(u8)(g_counter>>32);
        auth[4]=(u8)(g_counter>>24); auth[5]=(u8)(g_counter>>16);
        auth[6]=(u8)(g_counter>>8);  auth[7]=(u8)g_counter;
        memcpy(&auth[8], hash, 32);
        memset(&auth[40], 0x08, 8);

        discord_aes256_cbc_encrypt(key, iv, auth, 48, auth, &alen);
        bytes_to_hex(auth, 48, ah);

        snprintf(body, sizeof(body),
            "uuid=%s&counter=%llu&auth_hex=%s&state=%s&details=%s&activity_type=0",
            g_uuid, g_counter, ah, se, de);

        int r = discord_http_post(g_ip_str, g_server_port, "/api/activity",
                                   body, resp, sizeof(resp), 0);
        if(g_shouldStop) break;

        if(r == 0 && parse_field(resp, "success", ok, sizeof(ok)) && strcmp(ok, "true") == 0)
        {
            g_counter++;
            DiscordLog_Printf("[ACTIVE] OK counter=%llu\n", g_counter);
        }
        else
        {
            char err[64];
            if(parse_field(resp, "error", err, sizeof(err)))
            {
                DiscordLog_Printf("[WARN] %s\n", err);
                if(strstr(err, "session_expired"))
                {
                    set_state(DISCORD_LOGIN, "Session expired");
                    DiscordLog_Printf("[WARN] Session expired\n");
                    break;
                }
            }
        }
    }

stop:
    set_state(DISCORD_STOPPED, "Stopped");
    miniSocExit();
    DiscordLog_Printf("[THREAD] Exited\n");
}

void DiscordRPC_Start(void)
{
    if(!g_config_loaded)
    {
        DiscordLog_Printf("[CMD] Loading config...\n");
        DiscordConfig_Load();
    }
    if(g_discord_state != DISCORD_STOPPED || !g_config_loaded)
    {
        if(!g_config_loaded) DiscordLog_Printf("[CMD] No config\n");
        return;
    }

    // Prepare IP string
    u8 *ip = (u8 *)&g_server_ip;
    snprintf(g_ip_str, sizeof(g_ip_str), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    g_shouldStop = false;

    // Create startup event (like InputRedirection)
    if(R_FAILED(svcCreateEvent(&g_rpcStartedEvent, RESET_STICKY)))
    {
        DiscordLog_Printf("[CMD] Event creation failed\n");
        return;
    }

    DiscordLog_Printf("[CMD] Creating thread (prio 0x20)...\n");
    if(R_FAILED(MyThread_Create(&g_rpcThread, DiscordRPC_ThreadMain,
                                g_rpcThreadStack, sizeof(g_rpcThreadStack),
                                0x20, CORE_SYSTEM)))
    {
        DiscordLog_Printf("[CMD] Thread creation failed\n");
        svcCloseHandle(g_rpcStartedEvent);
        return;
    }

    // Wait for thread to initialize (like InputRedirection, 10s timeout)
    DiscordLog_Printf("[CMD] Waiting for thread init...\n");
    svcWaitSynchronization(g_rpcStartedEvent, 10LL * 1000 * 1000 * 1000);
    svcCloseHandle(g_rpcStartedEvent);
    DiscordLog_Printf("[CMD] Thread initialized\n");
}

void DiscordRPC_Stop(void)
{
    DiscordLog_Printf("[CMD] Stopping...\n");
    g_shouldStop = true;
    MyThread_Join(&g_rpcThread, -1LL);
    set_state(DISCORD_STOPPED, "Stopped");
    DiscordLog_Printf("[CMD] Stopped\n");
}

void DiscordRPC_Init(void)
{
    LightLock_Init(&g_discord_lock);
    g_shouldStop = false;
    g_counter = 0;
    g_ip_str[0] = '\0';
    DiscordLog_Printf("[INIT] Discord RPC ready\n");
}
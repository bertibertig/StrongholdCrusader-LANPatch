/*
 * dptest - DirectPlay LAN test that drives DirectPlay the same way
 * Stronghold Crusader does (COM IDirectPlay4A + IDirectPlayLobby3A compound
 * address with the TCP/IP SP and an empty INet address => broadcast search).
 *
 *   dptest host <seconds>          host a session, print joins, exit 0 if someone joined
 *   dptest join <seconds> [ip]     search (broadcast, or unicast to ip), join, send, exit 0 on success
 *
 * Output lines start with "DPTEST:" so test scripts can grep them.
 */
#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <dplay.h>
#include <dplobby.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* random but fixed application GUID for the test */
DEFINE_GUID(GUID_SCLanTest, 0x5c1a7e57, 0x1234, 0x4abc, 0x9a, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77);

static LPDIRECTPLAY4A dp;
static int found;
static DPSESSIONDESC2 found_desc;

static FILE *logf;
#define LOG(...) do { printf("DPTEST: " __VA_ARGS__); printf("\n"); fflush(stdout); \
                      if (logf) { fprintf(logf, "DPTEST: " __VA_ARGS__); fprintf(logf, "\n"); fflush(logf); } } while (0)

static BOOL FAR PASCAL enum_cb(LPCDPSESSIONDESC2 d, LPDWORD timeout, DWORD flags, LPVOID ctx)
{
    (void)timeout; (void)ctx;
    if (flags & DPESC_TIMEDOUT)
        return FALSE;
    if (!found) {
        found = 1;
        found_desc = *d;
        LOG("found session '%s' players=%lu/%lu", d->lpszSessionNameA ? d->lpszSessionNameA : "?",
            d->dwCurrentPlayers, d->dwMaxPlayers);
    }
    return TRUE;
}

static int init_connection(const char *ip)
{
    LPDIRECTPLAYLOBBY3A lobby = NULL;
    DPCOMPOUNDADDRESSELEMENT el[2];
    char addr[1024];
    DWORD size = sizeof addr;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectPlay, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectPlay4A, (void **)&dp);
    if (FAILED(hr)) { LOG("CoCreateInstance(DirectPlay) failed 0x%08lx", hr); return 0; }
    hr = CoCreateInstance(&CLSID_DirectPlayLobby, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectPlayLobby3A, (void **)&lobby);
    if (FAILED(hr)) { LOG("CoCreateInstance(Lobby) failed 0x%08lx", hr); return 0; }

    el[0].guidDataType = DPAID_ServiceProvider;
    el[0].dwDataSize = sizeof(GUID);
    el[0].lpData = (LPVOID)&DPSPGUID_TCPIP;
    el[1].guidDataType = DPAID_INet;
    el[1].dwDataSize = strlen(ip) + 1;
    el[1].lpData = (LPVOID)ip;
    hr = IDirectPlayLobby_CreateCompoundAddress(lobby, el, 2, addr, &size);
    if (FAILED(hr)) { LOG("CreateCompoundAddress failed 0x%08lx", hr); return 0; }
    hr = IDirectPlayX_InitializeConnection(dp, addr, 0);
    if (FAILED(hr)) { LOG("InitializeConnection failed 0x%08lx", hr); return 0; }
    IDirectPlayLobby_Release(lobby);
    return 1;
}

static void pump(DPID me, DWORD ms, int *joins)
{
    DWORD end = GetTickCount() + ms;
    char buf[4096];
    while (GetTickCount() < end) {
        DPID from = 0, to = 0;
        DWORD size = sizeof buf;
        HRESULT hr = IDirectPlayX_Receive(dp, &from, &to, DPRECEIVE_ALL, buf, &size);
        if (hr == DPERR_NOMESSAGES) { Sleep(20); continue; }
        if (FAILED(hr)) { Sleep(20); continue; }
        if (from == DPID_SYSMSG) {
            DPMSG_GENERIC *g = (DPMSG_GENERIC *)buf;
            if (g->dwType == DPSYS_CREATEPLAYERORGROUP) {
                LOG("player joined");
                if (joins) (*joins)++;
            } else if (g->dwType == DPSYS_DESTROYPLAYERORGROUP) {
                LOG("player left");
            } else if (g->dwType == DPSYS_SESSIONLOST) {
                LOG("session lost");
            }
        } else {
            LOG("message from %lu: %.*s", from, (int)size, buf);
        }
    }
    (void)me;
}

int main(int argc, char **argv)
{
    int secs;
    DPID me = 0;
    DPNAME name;
    HRESULT hr;

    if (argc < 3) {
        fprintf(stderr, "usage: dptest host|join <seconds> [ip]\n");
        return 2;
    }
    secs = atoi(argv[2]);
    logf = fopen("dptest.log", "w");
    /* make sure dplayx.dll (the proxy, if present) is loaded like the game does */
    DirectPlayEnumerateA(NULL, NULL);
    CoInitialize(NULL);

    if (!init_connection(argc > 3 ? argv[3] : ""))
        return 1;

    memset(&name, 0, sizeof name);
    name.dwSize = sizeof name;

    if (!strcmp(argv[1], "host")) {
        DPSESSIONDESC2 d;
        int joins = 0;
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        /* same flags as the game: 0xa044 */
        d.dwFlags = DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST | DPSESSION_DIRECTPLAYPROTOCOL | DPSESSION_OPTIMIZELATENCY;
        d.guidApplication = GUID_SCLanTest;
        d.dwMaxPlayers = 8;
        d.lpszSessionNameA = "sclantest";
        hr = IDirectPlayX_Open(dp, &d, DPOPEN_CREATE);
        if (FAILED(hr)) { LOG("Open(CREATE) failed 0x%08lx", hr); return 1; }
        name.lpszShortNameA = "host";
        hr = IDirectPlayX_CreatePlayer(dp, &me, &name, NULL, NULL, 0, 0);
        LOG("hosting, CreatePlayer 0x%08lx", hr);
        pump(me, secs * 1000, &joins);
        LOG("host done, joins=%d", joins);
        IDirectPlayX_Close(dp);
        return joins ? 0 : 3;
    } else {
        DPSESSIONDESC2 d;
        DWORD end = GetTickCount() + secs * 1000, t0 = GetTickCount();
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        d.guidApplication = GUID_SCLanTest;
        while (!found && GetTickCount() < end) {
            hr = IDirectPlayX_EnumSessions(dp, &d, 0, enum_cb, NULL,
                                           DPENUMSESSIONS_AVAILABLE | DPENUMSESSIONS_ASYNC);
            if (FAILED(hr) && hr != DPERR_CONNECTING)
                LOG("EnumSessions 0x%08lx", hr);
            Sleep(250);
        }
        if (!found) { LOG("RESULT: no session found"); return 3; }
        LOG("found after %lu ms", GetTickCount() - t0);
        hr = IDirectPlayX_Open(dp, &found_desc, DPOPEN_JOIN);
        if (FAILED(hr)) { LOG("RESULT: Open(JOIN) failed 0x%08lx", hr); return 4; }
        name.lpszShortNameA = "client";
        hr = IDirectPlayX_CreatePlayer(dp, &me, &name, NULL, NULL, 0, 0);
        if (FAILED(hr)) { LOG("RESULT: CreatePlayer failed 0x%08lx", hr); return 5; }
        hr = IDirectPlayX_Send(dp, me, DPID_ALLPLAYERS, DPSEND_GUARANTEED, "hello", 5);
        LOG("send 0x%08lx", hr);
        pump(me, 2000, NULL);
        LOG("RESULT: joined");
        IDirectPlayX_Close(dp);
        return 0;
    }
}

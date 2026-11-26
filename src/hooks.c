/*
 * Import-table hooks.
 *
 * The game creates DirectPlay through COM (CoCreateInstance(CLSID_DirectPlay)),
 * which loads the *system* dplayx.dll. That one loads the TCP/IP service
 * provider dpwsockx.dll, which does all the socket work. We patch the import
 * tables of:
 *
 *   game exe   : CoCreateInstance (to notice DirectPlay), gethostbyname
 *   dplayx.dll : LoadLibrary*     (to notice dpwsockx.dll being loaded)
 *   dpwsockx   : sendto/WSASendTo (broadcast fan-out + unicast to peers),
 *                socket/accept    (TCP_NODELAY / keepalive),
 *                gethostbyname    (LAN address first)
 *
 * A watcher thread re-applies the patches periodically, because the service
 * provider may be unloaded and loaded again between games.
 */
#include "sclan.h"
#include <stdio.h>
#include <string.h>

typedef int     (WSAAPI *sendto_fn)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int     (WSAAPI *wsasendto_fn)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const struct sockaddr *,
                                       int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef SOCKET  (WSAAPI *socket_fn)(int, int, int);
typedef SOCKET  (WSAAPI *accept_fn)(SOCKET, struct sockaddr *, int *);
typedef struct hostent *(WSAAPI *ghbn_fn)(const char *);
typedef HMODULE (WINAPI *lla_fn)(LPCSTR);
typedef HMODULE (WINAPI *llw_fn)(LPCWSTR);
typedef HMODULE (WINAPI *llexa_fn)(LPCSTR, HANDLE, DWORD);
typedef HMODULE (WINAPI *llexw_fn)(LPCWSTR, HANDLE, DWORD);
typedef HRESULT (WINAPI *cci_fn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);

static sendto_fn    real_sendto;
static wsasendto_fn real_WSASendTo;
static socket_fn    real_socket;
static accept_fn    real_accept;
static ghbn_fn      real_gethostbyname;
static lla_fn       real_LoadLibraryA;
static llw_fn       real_LoadLibraryW;
static llexa_fn     real_LoadLibraryExA;
static llexw_fn     real_LoadLibraryExW;
static cci_fn       real_CoCreateInstance;

static volatile LONG g_watch_started;

/* last enumeration request seen - re-sent to peers discovered later */
static CRITICAL_SECTION g_enumcs;
static char   g_enum_pkt[1500];
static int    g_enum_len;
static SOCKET g_enum_sock = INVALID_SOCKET;
static DWORD  g_enum_time;

/* ------------------------------------------------------------------ IAT */

int sclan_patch_iat(HMODULE mod, const void *target, const void *repl)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    int patched = 0;

    if (!mod || !target || target == repl || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress || !dir->Size)
        return 0;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; t->u1.Function; t++) {
            if ((const void *)t->u1.Function == target) {
                DWORD old;
                if (VirtualProtect(&t->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                    t->u1.Function = (ULONG_PTR)repl;
                    VirtualProtect(&t->u1.Function, sizeof(void *), old, &old);
                    patched++;
                }
            }
        }
    }
    return patched;
}

/* Patch every known alias of an API (ws2_32 / wsock32 / kernel32 / kernelbase). */
static int patch_all(HMODULE mod, const char *const *dlls, const char *name, const void *repl)
{
    int n = 0;
    for (; *dlls; dlls++) {
        HMODULE h = GetModuleHandleA(*dlls);
        if (h)
            n += sclan_patch_iat(mod, (const void *)GetProcAddress(h, name), repl);
    }
    return n;
}

static const char *const sock_dlls[]   = { "ws2_32.dll", "wsock32.dll", NULL };
static const char *const kernel_dlls[] = { "kernel32.dll", "kernelbase.dll", NULL };
static const char *const ole_dlls[]    = { "ole32.dll", "combase.dll", NULL };

/* ----------------------------------------------------------- broadcasts */

static int is_enum_broadcast(const struct sockaddr *to, int tolen)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)to;
    sclan_iface ifs[SCLAN_MAX_IFACES];
    int n, i;

    if (!to || tolen < (int)sizeof *sin || sin->sin_family != AF_INET)
        return 0;
    if (ntohs(sin->sin_port) != DPLAY_ENUM_PORT)
        return 0;
    if (sin->sin_addr.s_addr == INADDR_BROADCAST)
        return 1;
    n = sclan_ifaces(ifs, SCLAN_MAX_IFACES);
    for (i = 0; i < n; i++)
        if (ifs[i].bcast == sin->sin_addr.s_addr)
            return 1;
    return 0;
}

static void send_copy(SOCKET s, const char *buf, int len, int flags, ULONG addr)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DPLAY_ENUM_PORT);
    sin.sin_addr.s_addr = addr;
    real_sendto(s, buf, len, flags, (struct sockaddr *)&sin, sizeof sin);
}

static void fan_out(SOCKET s, const char *buf, int len, int flags, ULONG orig)
{
    sclan_iface ifs[SCLAN_MAX_IFACES];
    ULONG peers[SCLAN_MAX_PEERS];
    int n, i, np;

    if (len > 0 && len <= (int)sizeof g_enum_pkt) {
        EnterCriticalSection(&g_enumcs);
        memcpy(g_enum_pkt, buf, len);
        g_enum_len = len;
        g_enum_sock = s;
        g_enum_time = GetTickCount();
        LeaveCriticalSection(&g_enumcs);
    }

    if (g_cfg.broadcast_all_adapters) {
        n = sclan_ifaces(ifs, SCLAN_MAX_IFACES);
        for (i = 0; i < n; i++) {
            if (ifs[i].bcast == orig || ifs[i].mask == 0xFFFFFFFFu)
                continue;
            send_copy(s, buf, len, flags, ifs[i].bcast);
        }
    }
    if (g_cfg.unicast_to_peers) {
        np = sclan_discovery_peers(peers, SCLAN_MAX_PEERS);
        for (i = 0; i < np; i++)
            send_copy(s, buf, len, flags, peers[i]);
    }
    sclan_discovery_kick();
}

/* Called by the discovery thread when a new peer shows up. */
void sclan_resend_enum_to(ULONG addr)
{
    char pkt[1500];
    int len = 0;
    SOCKET s = INVALID_SOCKET;

    if (!g_cfg.unicast_to_peers || !real_sendto)
        return;
    EnterCriticalSection(&g_enumcs);
    if (g_enum_len && GetTickCount() - g_enum_time < 15000) {
        memcpy(pkt, g_enum_pkt, g_enum_len);
        len = g_enum_len;
        s = g_enum_sock;
    }
    LeaveCriticalSection(&g_enumcs);
    if (len && s != INVALID_SOCKET) {
        struct in_addr ia;
        ia.s_addr = addr;
        sclan_log("re-sending last enum request to new peer %s", inet_ntoa(ia));
        send_copy(s, pkt, len, 0, addr);
    }
}

static int WSAAPI hk_sendto(SOCKET s, const char *buf, int len, int flags,
                            const struct sockaddr *to, int tolen)
{
    int rc = real_sendto(s, buf, len, flags, to, tolen);
    if (is_enum_broadcast(to, tolen))
        fan_out(s, buf, len, flags, ((const struct sockaddr_in *)to)->sin_addr.s_addr);
    return rc;
}

static int WSAAPI hk_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD nbufs, LPDWORD sent, DWORD flags,
                               const struct sockaddr *to, int tolen, LPWSAOVERLAPPED ov,
                               LPWSAOVERLAPPED_COMPLETION_ROUTINE cr)
{
    int rc = real_WSASendTo(s, bufs, nbufs, sent, flags, to, tolen, ov, cr);
    if (is_enum_broadcast(to, tolen)) {
        char tmp[1500];
        int len = 0;
        DWORD i;
        for (i = 0; i < nbufs; i++) {
            if (len + (int)bufs[i].len > (int)sizeof tmp) { len = -1; break; }
            memcpy(tmp + len, bufs[i].buf, bufs[i].len);
            len += bufs[i].len;
        }
        if (len > 0)
            fan_out(s, tmp, len, (int)flags, ((const struct sockaddr_in *)to)->sin_addr.s_addr);
    }
    return rc;
}

/* -------------------------------------------------------------- sockets */

static void tune_stream(SOCKET s)
{
    BOOL on = TRUE;
    int type = 0, tl = sizeof type;
    if (s == INVALID_SOCKET)
        return;
    if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char *)&type, &tl) || type != SOCK_STREAM)
        return;
    if (g_cfg.tcp_nodelay)
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof on);
    if (g_cfg.tcp_keepalive)
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof on);
}

static SOCKET WSAAPI hk_socket(int af, int type, int proto)
{
    SOCKET s = real_socket(af, type, proto);
    if (type == SOCK_STREAM)
        tune_stream(s);
    return s;
}

static SOCKET WSAAPI hk_accept(SOCKET ls, struct sockaddr *addr, int *alen)
{
    SOCKET s = real_accept(ls, addr, alen);
    tune_stream(s);
    return s;
}

/* -------------------------------------------------------- gethostbyname */

static int addr_rank(ULONG a)
{
    sclan_iface ifs[SCLAN_MAX_IFACES];
    int n = sclan_ifaces(ifs, SCLAN_MAX_IFACES), i;

    if (g_cfg.preferred_ip[0] && inet_addr(g_cfg.preferred_ip) == a)
        return 0;
    for (i = 0; i < n; i++) {
        if (ifs[i].addr != a)
            continue;
        if (!ifs[i].is_virtual && ifs[i].has_gateway) return 1;
        if (!ifs[i].is_virtual) return 2;
        return 3;
    }
    if ((ntohl(a) >> 24) == 127)
        return 5;
    return 4;
}

static struct hostent *WSAAPI hk_gethostbyname(const char *name)
{
    struct hostent *h = real_gethostbyname(name);
    char host[256];
    int i, j, n;

    if (!h || !g_cfg.prefer_lan_address || h->h_addrtype != AF_INET || !h->h_addr_list)
        return h;
    /* only reorder lookups of our own host name (or the empty name) */
    if (name && *name && gethostname(host, sizeof host) == 0 && _stricmp(name, host))
        return h;
    for (n = 0; h->h_addr_list[n]; n++)
        ;
    /* stable insertion sort by rank */
    for (i = 1; i < n; i++) {
        char *p = h->h_addr_list[i];
        int r = addr_rank(*(ULONG *)p);
        for (j = i; j > 0 && addr_rank(*(ULONG *)h->h_addr_list[j - 1]) > r; j--)
            h->h_addr_list[j] = h->h_addr_list[j - 1];
        h->h_addr_list[j] = p;
    }
    return h;
}

/* ------------------------------------------------------- module watcher */

static void patch_sp(HMODULE sp)
{
    int n = 0;
    n += patch_all(sp, sock_dlls, "sendto", (void *)hk_sendto);
    n += patch_all(sp, sock_dlls, "WSASendTo", (void *)hk_WSASendTo);
    n += patch_all(sp, sock_dlls, "socket", (void *)hk_socket);
    n += patch_all(sp, sock_dlls, "accept", (void *)hk_accept);
    n += patch_all(sp, sock_dlls, "gethostbyname", (void *)hk_gethostbyname);
    /* dpwsockx imports the data symbol gdwDPlaySPRefCount from "DPLAYX.dll" -
       the loader may have bound it to this proxy; point it at the real one. */
    if (sclan_real_dplayx()) {
        extern DWORD gdwDPlaySPRefCount;
        void *realref = (void *)GetProcAddress(sclan_real_dplayx(), "gdwDPlaySPRefCount");
        if (realref)
            n += sclan_patch_iat(sp, &gdwDPlaySPRefCount, realref);
    }
    if (n)
        sclan_log("patched %d imports in dpwsockx.dll (%p)", n, sp);
}

static void scan_modules(void);

static HMODULE WINAPI hk_LoadLibraryA(LPCSTR n)   { HMODULE h = real_LoadLibraryA(n); scan_modules(); return h; }
static HMODULE WINAPI hk_LoadLibraryW(LPCWSTR n)  { HMODULE h = real_LoadLibraryW(n); scan_modules(); return h; }
static HMODULE WINAPI hk_LoadLibraryExA(LPCSTR n, HANDLE f, DWORD fl)
{ HMODULE h = real_LoadLibraryExA(n, f, fl); scan_modules(); return h; }
static HMODULE WINAPI hk_LoadLibraryExW(LPCWSTR n, HANDLE f, DWORD fl)
{ HMODULE h = real_LoadLibraryExW(n, f, fl); scan_modules(); return h; }

static void scan_modules(void)
{
    HMODULE dp = GetModuleHandleA("dpwsockx.dll");
    HMODULE sysdp = sclan_real_dplayx();

    if (dp)
        patch_sp(dp);

    if (sysdp) {
        int n = 0;
        n += patch_all(sysdp, kernel_dlls, "LoadLibraryA", (void *)hk_LoadLibraryA);
        n += patch_all(sysdp, kernel_dlls, "LoadLibraryW", (void *)hk_LoadLibraryW);
        n += patch_all(sysdp, kernel_dlls, "LoadLibraryExA", (void *)hk_LoadLibraryExA);
        n += patch_all(sysdp, kernel_dlls, "LoadLibraryExW", (void *)hk_LoadLibraryExW);
        if (n)
            sclan_log("patched %d loader imports in system dplayx.dll (%p)", n, sysdp);
    }
}

static DWORD WINAPI watcher(LPVOID unused)
{
    (void)unused;
    for (;;) {
        scan_modules();
        Sleep(250);
    }
    return 0;
}

static void resolve_reals(void)
{
    HMODULE ws = LoadLibraryA("ws2_32.dll");
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    LoadLibraryA("wsock32.dll");
    real_sendto        = (sendto_fn)GetProcAddress(ws, "sendto");
    real_WSASendTo     = (wsasendto_fn)GetProcAddress(ws, "WSASendTo");
    real_socket        = (socket_fn)GetProcAddress(ws, "socket");
    real_accept        = (accept_fn)GetProcAddress(ws, "accept");
    real_gethostbyname = (ghbn_fn)GetProcAddress(ws, "gethostbyname");
    real_LoadLibraryA  = (lla_fn)GetProcAddress(k32, "LoadLibraryA");
    real_LoadLibraryW  = (llw_fn)GetProcAddress(k32, "LoadLibraryW");
    real_LoadLibraryExA = (llexa_fn)GetProcAddress(k32, "LoadLibraryExA");
    real_LoadLibraryExW = (llexw_fn)GetProcAddress(k32, "LoadLibraryExW");
}

void sclan_hooks_on_dplay_loaded(void)
{
    if (!g_cfg.enabled)
        return;
    if (InterlockedCompareExchange(&g_watch_started, 1, 0) == 0) {
        WSADATA wd;
        WSAStartup(MAKEWORD(2, 2), &wd);
        sclan_log("DirectPlay in use - starting hooks and discovery");
        sclan_net_start();
        CreateThread(NULL, 0, watcher, NULL, 0, NULL);
        sclan_discovery_start();
    }
    scan_modules();
}

/* ---------------------------------------------------------- game exe */

static HRESULT WINAPI hk_CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *ppv)
{
    static const GUID CLSID_DirectPlay =
        { 0xd1eb6d20, 0x8923, 0x11d0, { 0x9d, 0x97, 0x00, 0xa0, 0xc9, 0x0a, 0x43, 0xcb } };
    static const GUID CLSID_DirectPlayLobby =
        { 0x2fe8f810, 0xb2a5, 0x11d0, { 0xa7, 0x87, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc } };
    HRESULT hr = real_CoCreateInstance(clsid, outer, ctx, iid, ppv);

    if (clsid && (IsEqualGUID(clsid, &CLSID_DirectPlay) || IsEqualGUID(clsid, &CLSID_DirectPlayLobby))) {
        sclan_log("CoCreateInstance(%s) -> 0x%08lx",
                  IsEqualGUID(clsid, &CLSID_DirectPlay) ? "CLSID_DirectPlay" : "CLSID_DirectPlayLobby",
                  (unsigned long)hr);
        sclan_hooks_on_dplay_loaded();
    }
    return hr;
}

void sclan_hooks_install_exe(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    HMODULE ole;
    int n = 0;

    InitializeCriticalSection(&g_enumcs);
    resolve_reals();

    ole = LoadLibraryA("ole32.dll");
    real_CoCreateInstance = (cci_fn)GetProcAddress(ole, "CoCreateInstance");
    n += patch_all(exe, ole_dlls, "CoCreateInstance", (void *)hk_CoCreateInstance);
    n += patch_all(exe, sock_dlls, "gethostbyname", (void *)hk_gethostbyname);
    sclan_log("patched %d imports in game executable", n);
}

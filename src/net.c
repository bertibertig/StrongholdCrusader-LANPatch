/*
 * Local network interface enumeration (IPv4 only - DirectPlay is IPv4 only).
 */
#include "sclan.h"
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>

static CRITICAL_SECTION g_ifcs;
static volatile LONG g_ifcs_init;
static sclan_iface g_if[SCLAN_MAX_IFACES];
static int g_nif;
static DWORD g_if_time;

static const char *virtual_hints[] = {
    "virtualbox", "vmware", "hyper-v", "vethernet", "docker", "wsl",
    "tap-", "tap ", "tun", "wireguard", "openvpn", "npcap", "loopback",
    "bluetooth", "vpn", "zerotier", "hamachi", "tailscale", "radmin", NULL
};

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return 0;
    for (; *hay; hay++)
        if (!_strnicmp(hay, needle, n))
            return 1;
    return 0;
}

static int user_ignored(const char *name)
{
    char buf[512], *tok, *ctx = NULL;
    lstrcpynA(buf, g_cfg.ignore_adapters, sizeof buf);
    for (tok = strtok_s(buf, ",;", &ctx); tok; tok = strtok_s(NULL, ",;", &ctx)) {
        while (*tok == ' ') tok++;
        if (*tok && contains_ci(name, tok))
            return 1;
    }
    return 0;
}

static void log_ifaces(const sclan_iface *f, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        struct in_addr ia, ib;
        char a1[16];
        ia.s_addr = f[i].addr; ib.s_addr = f[i].bcast;
        lstrcpynA(a1, inet_ntoa(ia), sizeof a1);
        sclan_log("iface %-30s %-15s bcast %-15s gw=%d virtual=%d",
                  f[i].name, a1, inet_ntoa(ib), f[i].has_gateway, f[i].is_virtual);
    }
}

/* returns number of interfaces, -1 on error */
static int query(sclan_iface *out, int max)
{
    ULONG len = 32 * 1024;
    IP_ADAPTER_ADDRESSES *aa = NULL, *a;
    ULONG rc = ERROR_SUCCESS;
    int n = 0, tries;

    for (tries = 0; tries < 3; tries++) {
        aa = HeapAlloc(GetProcessHeap(), 0, len);
        if (!aa) return -1;
        rc = GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                GAA_FLAG_INCLUDE_GATEWAYS, NULL, aa, &len);
        if (rc == ERROR_SUCCESS) break;
        HeapFree(GetProcessHeap(), 0, aa);
        aa = NULL;
        if (rc != ERROR_BUFFER_OVERFLOW) return -1;
    }
    if (!aa) return -1;

    for (a = aa; a && n < max; a = a->Next) {
        IP_ADAPTER_UNICAST_ADDRESS *u;
        char name[128] = "", desc[256] = "";
        int virt = 0, i;
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        if (a->FriendlyName)
            WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof name, NULL, NULL);
        if (!name[0] && a->AdapterName)
            lstrcpynA(name, a->AdapterName, sizeof name);
        if (a->Description)
            WideCharToMultiByte(CP_UTF8, 0, a->Description, -1, desc, sizeof desc, NULL, NULL);
        if (user_ignored(name) || user_ignored(desc))
            continue;
        for (i = 0; virtual_hints[i]; i++)
            if (contains_ci(name, virtual_hints[i]) || contains_ci(desc, virtual_hints[i]))
                virt = 1;
        for (u = a->FirstUnicastAddress; u && n < max; u = u->Next) {
            struct sockaddr_in *sin = (struct sockaddr_in *)u->Address.lpSockaddr;
            ULONG prefix = u->OnLinkPrefixLength, mask, host;
            if (!sin || sin->sin_family != AF_INET)
                continue;
            host = ntohl(sin->sin_addr.s_addr);
            if ((host >> 24) == 127 || (host >> 16) == 0xA9FE)   /* loopback, APIPA */
                continue;
            if (prefix == 0 || prefix > 32) prefix = 24;
            mask = prefix == 32 ? 0xFFFFFFFFu : htonl(~(0xFFFFFFFFu >> prefix));
            memset(&out[n], 0, sizeof out[n]);
            out[n].addr = sin->sin_addr.s_addr;
            out[n].mask = mask;
            out[n].bcast = sin->sin_addr.s_addr | ~mask;
            out[n].has_gateway = a->FirstGatewayAddress != NULL;
            out[n].is_virtual = virt;
            lstrcpynA(out[n].name, name, sizeof out[n].name);
            n++;
        }
    }
    HeapFree(GetProcessHeap(), 0, aa);
    return n;
}

static void ensure_cs(void)
{
    if (InterlockedCompareExchange(&g_ifcs_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_ifcs);
        InterlockedExchange(&g_ifcs_init, 2);
    }
    while (g_ifcs_init != 2) Sleep(0);
}

/* GetAdaptersAddresses can take a long time (seconds under Wine), so it never
   runs on a game/DirectPlay thread: a background thread keeps a cached copy. */
static DWORD WINAPI refresher(LPVOID unused)
{
    static sclan_iface tmp[SCLAN_MAX_IFACES];
    (void)unused;
    for (;;) {
        int n = query(tmp, SCLAN_MAX_IFACES);
        EnterCriticalSection(&g_ifcs);
        if (n >= 0) {
            if (n != g_nif || memcmp(tmp, g_if, n * sizeof *g_if))
                log_ifaces(tmp, n);
            memcpy(g_if, tmp, n * sizeof *g_if);
            g_nif = n;
            g_if_time = GetTickCount();
        }
        LeaveCriticalSection(&g_ifcs);
        Sleep(5000);
    }
    return 0;
}

void sclan_net_start(void)
{
    static volatile LONG started;
    ensure_cs();
    if (InterlockedCompareExchange(&started, 1, 0) == 0)
        CreateThread(NULL, 0, refresher, NULL, 0, NULL);
}

int sclan_ifaces(sclan_iface *out, int max)
{
    int n;
    ensure_cs();
    EnterCriticalSection(&g_ifcs);
    n = g_nif < max ? g_nif : max;
    memcpy(out, g_if, n * sizeof *out);
    LeaveCriticalSection(&g_ifcs);
    return n;
}

int sclan_is_local_addr(ULONG addr)
{
    sclan_iface ifs[SCLAN_MAX_IFACES];
    int n = sclan_ifaces(ifs, SCLAN_MAX_IFACES), i;
    if ((ntohl(addr) >> 24) == 127)
        return 1;
    for (i = 0; i < n; i++)
        if (ifs[i].addr == addr)
            return 1;
    return 0;
}

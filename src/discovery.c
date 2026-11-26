/*
 * TCP neighbour discovery.
 *
 * Every patched game runs a tiny TCP service (default port 47630). While the
 * game is looking for sessions, the patch
 *
 *   1. connects to every address of the local subnets (clipped to /24 for big
 *      networks), to the static peers from sclanpatch.ini and to every peer
 *      learned from other peers (gossip),
 *   2. exchanges a short hello + its peer list with whoever answers,
 *   3. sends DirectPlay's enumeration request directly (unicast) to every
 *      verified peer - no broadcast needed, works across adapters, VPNs
 *      (Tailscale, ZeroTier, Hamachi, ...) and routed subnets.
 *
 * Wire format (ASCII lines, connection closed after the exchange):
 *   SCLAN1 HELLO <version> <hostname>\n
 *   PEERS <ip>,<ip>,...\n
 *   END\n
 */
#include "sclan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PEER_TTL_MS     45000
#define ACTIVE_MS       20000
#define MAX_CANDIDATES  4096
#define BATCH           512

typedef struct {
    ULONG addr;
    DWORD seen;       /* last successful exchange */
    int   verified;   /* answered on the discovery port */
} peer;

static CRITICAL_SECTION g_pcs;
static peer   g_peers[SCLAN_MAX_PEERS];
static int    g_npeers;
static HANDLE g_kick;
static volatile DWORD g_last_kick;
static volatile LONG g_started;
static volatile LONG g_ready;

/* --------------------------------------------------------------- peers */

static void peer_note(ULONG addr, int verified)
{
    int i, isnew = 0;
    struct in_addr ia;

    if (!g_ready || !addr || addr == INADDR_BROADCAST || sclan_is_local_addr(addr))
        return;
    EnterCriticalSection(&g_pcs);
    for (i = 0; i < g_npeers; i++)
        if (g_peers[i].addr == addr)
            break;
    if (i == g_npeers) {
        if (g_npeers == SCLAN_MAX_PEERS) {
            LeaveCriticalSection(&g_pcs);
            return;
        }
        g_peers[g_npeers].addr = addr;
        g_peers[g_npeers].verified = 0;
        g_peers[g_npeers].seen = 0;
        g_npeers++;
    }
    if (verified) {
        isnew = !g_peers[i].verified || GetTickCount() - g_peers[i].seen > PEER_TTL_MS;
        g_peers[i].verified = 1;
        g_peers[i].seen = GetTickCount();
    }
    LeaveCriticalSection(&g_pcs);

    if (isnew) {
        ia.s_addr = addr;
        sclan_log("peer found: %s", inet_ntoa(ia));
        sclan_resend_enum_to(addr);
    }
}

int sclan_discovery_peers(ULONG *out, int max)
{
    int i, n = 0;
    DWORD now = GetTickCount();
    if (!g_ready)
        return 0;
    EnterCriticalSection(&g_pcs);
    for (i = 0; i < g_npeers && n < max; i++)
        if (g_peers[i].verified && now - g_peers[i].seen < PEER_TTL_MS)
            out[n++] = g_peers[i].addr;
    LeaveCriticalSection(&g_pcs);
    return n;
}

static int peer_list_text(char *buf, int len)
{
    ULONG p[SCLAN_MAX_PEERS];
    int n = sclan_discovery_peers(p, SCLAN_MAX_PEERS), i, off;
    off = snprintf(buf, len, "PEERS ");
    for (i = 0; i < n && off < len - 20; i++) {
        struct in_addr ia;
        ia.s_addr = p[i];
        off += snprintf(buf + off, len - off, "%s%s", i ? "," : "", inet_ntoa(ia));
    }
    off += snprintf(buf + off, len - off, "\n");
    return off;
}

/* ----------------------------------------------------------- protocol */

static int send_all(SOCKET s, const char *b, int n)
{
    while (n > 0) {
        int r = send(s, b, n, 0);
        if (r <= 0) return -1;
        b += r; n -= r;
    }
    return 0;
}

static int send_hello(SOCKET s)
{
    char buf[8192], host[128] = "?";
    int off;
    gethostname(host, sizeof host);
    off = snprintf(buf, sizeof buf, "SCLAN1 HELLO %s %s\n", SCLAN_VERSION, host);
    off += peer_list_text(buf + off, sizeof buf - off);
    off += snprintf(buf + off, sizeof buf - off, "END\n");
    return send_all(s, buf, off);
}

/* reads until "END\n" or timeout; returns 1 if a valid hello was received */
static int recv_hello(SOCKET s)
{
    char buf[8192];
    int off = 0, r, ok = 0;
    char *line, *ctx = NULL;

    while (off < (int)sizeof buf - 1) {
        r = recv(s, buf + off, sizeof buf - 1 - off, 0);
        if (r <= 0) break;
        off += r;
        buf[off] = 0;
        if (strstr(buf, "END\n")) break;
    }
    buf[off] = 0;
    for (line = strtok_s(buf, "\r\n", &ctx); line; line = strtok_s(NULL, "\r\n", &ctx)) {
        if (!strncmp(line, "SCLAN1 HELLO", 12))
            ok = 1;
        else if (ok && !strncmp(line, "PEERS ", 6)) {
            char *tok, *c2 = NULL;
            for (tok = strtok_s(line + 6, ",", &c2); tok; tok = strtok_s(NULL, ",", &c2)) {
                ULONG a = inet_addr(tok);
                if (a != INADDR_NONE)
                    peer_note(a, 0);      /* candidate - verified on next scan */
            }
        }
    }
    return ok;
}

static void set_timeouts(SOCKET s, int ms)
{
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&ms, sizeof ms);
}

/* ----------------------------------------------------------- listener */

static DWORD WINAPI listener(LPVOID unused)
{
    SOCKET ls;
    struct sockaddr_in sin;
    BOOL on = TRUE;
    (void)unused;

    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        sclan_log("discovery: socket failed (%d)", WSAGetLastError());
        return 0;
    }
    setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&on, sizeof on);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons((u_short)g_cfg.discovery_port);
    if (bind(ls, (struct sockaddr *)&sin, sizeof sin) || listen(ls, 16)) {
        sclan_log("discovery: cannot listen on TCP %d (%d) - other instance running?",
                  g_cfg.discovery_port, WSAGetLastError());
        closesocket(ls);
        return 0;
    }
    sclan_log("discovery: listening on TCP %d", g_cfg.discovery_port);

    for (;;) {
        struct sockaddr_in from;
        int fl = sizeof from;
        SOCKET c = accept(ls, (struct sockaddr *)&from, &fl);
        if (c == INVALID_SOCKET) {
            Sleep(100);
            continue;
        }
        set_timeouts(c, 1500);
        if (recv_hello(c)) {
            send_hello(c);
            peer_note(from.sin_addr.s_addr, 1);
        }
        shutdown(c, SD_SEND);
        closesocket(c);
    }
    return 0;
}

/* ------------------------------------------------------------ scanner */

static int add_cand(ULONG *c, int n, ULONG a)
{
    int i;
    if (n >= MAX_CANDIDATES || !a || a == INADDR_NONE || a == INADDR_BROADCAST)
        return n;
    if (sclan_is_local_addr(a))
        return n;
    for (i = 0; i < n; i++)
        if (c[i] == a) return n;
    c[n] = a;
    return n + 1;
}

static int static_peers(ULONG *c, int n)
{
    char buf[1024], *tok, *ctx = NULL;
    lstrcpynA(buf, g_cfg.peers, sizeof buf);
    for (tok = strtok_s(buf, ",; ", &ctx); tok; tok = strtok_s(NULL, ",; ", &ctx)) {
        ULONG a = inet_addr(tok);
        if (a == INADDR_NONE) {
            struct addrinfo hints, *res = NULL, *r;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET;
            if (!getaddrinfo(tok, NULL, &hints, &res)) {
                for (r = res; r; r = r->ai_next)
                    n = add_cand(c, n, ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr);
                freeaddrinfo(res);
            }
            continue;
        }
        n = add_cand(c, n, a);
    }
    return n;
}

static int subnet_hosts(ULONG *c, int n)
{
    sclan_iface ifs[SCLAN_MAX_IFACES];
    int ni = sclan_ifaces(ifs, SCLAN_MAX_IFACES), i;

    for (i = 0; i < ni; i++) {
        ULONG addr = ntohl(ifs[i].addr), mask = ntohl(ifs[i].mask), net, last, h;
        int prefix = 0;
        while (prefix < 32 && (mask & (0x80000000u >> prefix))) prefix++;
        if (prefix >= 31)
            continue;                         /* point-to-point / VPN host route */
        if (prefix < g_cfg.max_scan_prefix)
            mask = 0xFFFFFF00u;               /* huge network: only our /24 */
        net = addr & mask;
        last = net | ~mask;
        for (h = net + 1; h < last; h++)
            n = add_cand(c, n, htonl(h));
    }
    return n;
}

static void scan_batch(ULONG *cand, int n)
{
    SOCKET s[BATCH];
    int i, open = 0;
    DWORD start;

    for (i = 0; i < n; i++) {
        struct sockaddr_in sin;
        u_long nb = 1;
        s[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s[i] == INVALID_SOCKET) continue;
        ioctlsocket(s[i], FIONBIO, &nb);
        memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons((u_short)g_cfg.discovery_port);
        sin.sin_addr.s_addr = cand[i];
        if (connect(s[i], (struct sockaddr *)&sin, sizeof sin) &&
            WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(s[i]);
            s[i] = INVALID_SOCKET;
            continue;
        }
        open++;
    }

    start = GetTickCount();
    while (open > 0) {
        fd_set wr, ex;
        struct timeval tv;
        int left = g_cfg.connect_timeout_ms - (int)(GetTickCount() - start), r;
        if (left <= 0) break;
        FD_ZERO(&wr); FD_ZERO(&ex);
        for (i = 0; i < n; i++)
            if (s[i] != INVALID_SOCKET) { FD_SET(s[i], &wr); FD_SET(s[i], &ex); }
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        r = select(0, NULL, &wr, &ex, &tv);
        if (r <= 0) break;
        for (i = 0; i < n; i++) {
            u_long nb = 0;
            if (s[i] == INVALID_SOCKET) continue;
            if (FD_ISSET(s[i], &ex)) {
                closesocket(s[i]); s[i] = INVALID_SOCKET; open--;
                continue;
            }
            if (!FD_ISSET(s[i], &wr)) continue;
            /* connected */
            ioctlsocket(s[i], FIONBIO, &nb);
            set_timeouts(s[i], 1000);
            if (send_hello(s[i]) == 0) {
                shutdown(s[i], SD_SEND);
                if (recv_hello(s[i]))
                    peer_note(cand[i], 1);
            }
            closesocket(s[i]); s[i] = INVALID_SOCKET; open--;
        }
    }
    for (i = 0; i < n; i++)
        if (s[i] != INVALID_SOCKET)
            closesocket(s[i]);
}

static DWORD WINAPI scanner(LPVOID unused)
{
    static ULONG cand[MAX_CANDIDATES];
    (void)unused;

    for (;;) {
        int n = 0, i;
        WaitForSingleObject(g_kick, g_cfg.scan_interval_ms);
        if (!g_last_kick || GetTickCount() - g_last_kick > ACTIVE_MS)
            continue;                         /* game is not searching right now */

        n = static_peers(cand, n);
        EnterCriticalSection(&g_pcs);
        for (i = 0; i < g_npeers; i++)
            n = add_cand(cand, n, g_peers[i].addr);
        LeaveCriticalSection(&g_pcs);
        if (g_cfg.subnet_scan)
            n = subnet_hosts(cand, n);

        for (i = 0; i < n; i += BATCH)
            scan_batch(cand + i, n - i < BATCH ? n - i : BATCH);

        /* drop stale entries */
        EnterCriticalSection(&g_pcs);
        for (i = 0; i < g_npeers; ) {
            if (g_peers[i].verified && GetTickCount() - g_peers[i].seen > 3 * PEER_TTL_MS)
                g_peers[i] = g_peers[--g_npeers];
            else
                i++;
        }
        LeaveCriticalSection(&g_pcs);

        /* rate limit: at most one full scan per interval */
        Sleep(g_cfg.scan_interval_ms);
    }
    return 0;
}

void sclan_discovery_kick(void)
{
    DWORD prev = g_last_kick;
    g_last_kick = GetTickCount();
    if (g_kick && (!prev || g_last_kick - prev > ACTIVE_MS))
        SetEvent(g_kick);                     /* first search after a pause: scan now */
}

void sclan_discovery_start(void)
{
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
        return;
    InitializeCriticalSection(&g_pcs);
    g_kick = CreateEventA(NULL, FALSE, FALSE, NULL);
    InterlockedExchange(&g_ready, 1);
    CreateThread(NULL, 0, listener, NULL, 0, NULL);
    CreateThread(NULL, 0, scanner, NULL, 0, NULL);
}

/*
 * NAT hairpin workaround.
 *
 * A DirectPlay peer whose NAT helper is active (a PC without this patch, or
 * with BlockNATHelp=0) opens UPnP port mappings on the router and advertises
 * the router's PUBLIC address and the EXTERNAL port. Inside the LAN the other
 * PCs then connect to their own router's public address - most home routers
 * drop that (no NAT loopback / hairpinning), joining hangs until DirectPlay
 * times out.
 *
 * We ask the same router (UPnP IGD, GetSpecificPortMappingEntry) which LAN
 * address and port are behind that mapping and connect/send there directly.
 * Only addresses equal to the router's external address are touched.
 */
#include "sclan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_MAX      64
#define CACHE_TTL_MS   60000
#define RETRY_MS       3000
#define NET_TIMEOUT_MS 1500

typedef struct {
    ULONG  ext_ip;
    USHORT ext_port;      /* network byte order */
    int    tcp;
    ULONG  in_ip;
    USHORT in_port;       /* network byte order, 0 = no mapping */
    DWORD  when;
} map_entry;

static CRITICAL_SECTION g_ucs;
static volatile LONG g_uinit;
static ULONG  g_ctl_ip;            /* control URL of the IGD */
static USHORT g_ctl_port;          /* host byte order */
static char   g_ctl_path[256];
static char   g_svc[128];          /* service type */
static ULONG  g_ext_ip;            /* router's public address */
static DWORD  g_ext_when, g_fail_when;
static int    g_have_igd;
static map_entry g_cache[CACHE_MAX];
static int    g_ncache;

/* --------------------------------------------------------------- http */

static SOCKET tcp_open(ULONG ip, USHORT port)
{
    struct sockaddr_in sin;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int ms = NET_TIMEOUT_MS;
    u_long nb = 1;
    fd_set w;
    struct timeval tv = { NET_TIMEOUT_MS / 1000, (NET_TIMEOUT_MS % 1000) * 1000 };

    if (s == INVALID_SOCKET)
        return s;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&ms, sizeof ms);
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ip;
    sin.sin_port = htons(port);
    ioctlsocket(s, FIONBIO, &nb);
    connect(s, (struct sockaddr *)&sin, sizeof sin);
    FD_ZERO(&w);
    FD_SET(s, &w);
    if (select(0, NULL, &w, NULL, &tv) != 1) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    return s;
}

/* HTTP/1.0 request (no chunked replies); returns body length or -1 */
static int http(ULONG ip, USHORT port, const char *req, int reqlen, char *out, int outlen)
{
    SOCKET s = tcp_open(ip, port);
    int off = 0, r;
    char *body;

    if (s == INVALID_SOCKET)
        return -1;
    while (off < reqlen && (r = send(s, req + off, reqlen - off, 0)) > 0)
        off += r;
    off = 0;
    while (off < outlen - 1 && (r = recv(s, out + off, outlen - 1 - off, 0)) > 0)
        off += r;
    closesocket(s);
    out[off] = 0;
    if (strncmp(out, "HTTP/", 5) || !strstr(out, " 200") || !(body = strstr(out, "\r\n\r\n")))
        return -1;
    body += 4;
    memmove(out, body, strlen(body) + 1);
    return (int)strlen(out);
}

/* "http://1.2.3.4:5678/path" -> parts; returns 1 on success */
static int parse_url(const char *url, ULONG *ip, USHORT *port, char *path, int plen)
{
    char host[64];
    const char *p, *slash;
    int hl;
    if (_strnicmp(url, "http://", 7))
        return 0;
    p = url + 7;
    slash = strchr(p, '/');
    hl = slash ? (int)(slash - p) : (int)strlen(p);
    if (hl <= 0 || hl >= (int)sizeof host)
        return 0;
    memcpy(host, p, hl);
    host[hl] = 0;
    *port = 80;
    if (strchr(host, ':')) {
        *port = (USHORT)atoi(strchr(host, ':') + 1);
        *strchr(host, ':') = 0;
    }
    *ip = inet_addr(host);
    if (*ip == INADDR_NONE)
        return 0;
    lstrcpynA(path, slash ? slash : "/", plen);
    return 1;
}

/* copy the text of the first <tag>...</tag> at or after p */
static int xml_tag(const char *p, const char *tag, char *out, int len)
{
    char open[64], close[64];
    const char *a, *b;
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    if (!(a = strstr(p, open)))
        return 0;
    a += strlen(open);
    if (!(b = strstr(a, close)) || b - a >= len)
        return 0;
    memcpy(out, a, b - a);
    out[b - a] = 0;
    while (*out == ' ' || *out == '\r' || *out == '\n' || *out == '\t')
        memmove(out, out + 1, strlen(out));
    return 1;
}

/* --------------------------------------------------------------- IGD */

static int find_igd(void)
{
    static const char msearch[] =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    static const char msearch2[] =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\nST: urn:schemas-upnp-org:service:WANIPConnection:1\r\n\r\n";
    sclan_iface ifs[SCLAN_MAX_IFACES];
    int n =sclan_ifaces(ifs, SCLAN_MAX_IFACES), i, usable = 0;
    char loc[256] = "";

    for (i = 0; i < n; i++)
        usable += !ifs[i].is_virtual && ifs[i].has_gateway;
    (void)usable;
    /* always finish with an unbound socket on the default route: binding to the
       adapter address alone did not get an answer on some PCs */
    if (n >= SCLAN_MAX_IFACES)
        n = SCLAN_MAX_IFACES - 1;
    memset(&ifs[n], 0, sizeof ifs[n]);
    ifs[n].has_gateway = 1;
    n++;
    for (i = 0; i < n && !loc[0]; i++) {
        struct sockaddr_in sin, dst;
        SOCKET s;
        int ms = NET_TIMEOUT_MS;
        DWORD end;

        if (ifs[i].is_virtual || !ifs[i].has_gateway)
            continue;
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
            continue;
        memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = ifs[i].addr;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&ms, sizeof ms);
        if (bind(s, (struct sockaddr *)&sin, sizeof sin) == 0) {
            int ttl = 4;
            if (ifs[i].addr)
                setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&ifs[i].addr, sizeof ifs[i].addr);
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof ttl);
            memset(&dst, 0, sizeof dst);
            dst.sin_family = AF_INET;
            dst.sin_port = htons(1900);
            dst.sin_addr.s_addr = inet_addr("239.255.255.250");
            sendto(s, msearch, sizeof msearch - 1, 0, (struct sockaddr *)&dst, sizeof dst);
            sendto(s, msearch2, sizeof msearch2 - 1, 0, (struct sockaddr *)&dst, sizeof dst);
            sendto(s, msearch, sizeof msearch - 1, 0, (struct sockaddr *)&dst, sizeof dst);
            end = GetTickCount() + NET_TIMEOUT_MS;
            while (GetTickCount() < end && !loc[0]) {
                char buf[1500], *l;
                int r = recv(s, buf, sizeof buf - 1, 0);
                if (r <= 0)
                    break;
                buf[r] = 0;
                for (l = buf; (l = strchr(l, '\n')); ) {
                    l++;
                    if (!_strnicmp(l, "LOCATION:", 9)) {
                        char *e;
                        l += 9;
                        while (*l == ' ') l++;
                        lstrcpynA(loc, l, sizeof loc);
                        if ((e = strpbrk(loc, "\r\n"))) *e = 0;
                        break;
                    }
                }
            }
        }
        closesocket(s);
    }
    if (!loc[0])
        return 0;
    sclan_log("upnp: SSDP answer, description %s", loc);

    {
        ULONG ip; USHORT port; char path[256], req[512], *desc, *p;
        int len;
        if (!parse_url(loc, &ip, &port, path, sizeof path))
            return 0;
        len = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s:%u\r\n\r\n",
                       path, inet_ntoa(*(struct in_addr *)&ip), port);
        desc = (char *)malloc(65536);
        if (!desc)
            return 0;
        if (http(ip, port, req, len, desc, 65536) <= 0) {
            free(desc);
            return 0;
        }
        for (p = desc; (p = strstr(p, "<serviceType>")); p++) {
            char st[128], ctl[256];
            if (!xml_tag(p, "serviceType", st, sizeof st))
                continue;
            if (!strstr(st, ":WANIPConnection:") && !strstr(st, ":WANPPPConnection:"))
                continue;
            if (!xml_tag(p, "controlURL", ctl, sizeof ctl))
                continue;
            lstrcpynA(g_svc, st, sizeof g_svc);
            if (!_strnicmp(ctl, "http://", 7)) {
                if (!parse_url(ctl, &g_ctl_ip, &g_ctl_port, g_ctl_path, sizeof g_ctl_path))
                    continue;
            } else {
                g_ctl_ip = ip;
                g_ctl_port = port;
                snprintf(g_ctl_path, sizeof g_ctl_path, "%s%s", ctl[0] == '/' ? "" : "/", ctl);
            }
            free(desc);
            sclan_log("upnp: router %s:%u, service %s", inet_ntoa(*(struct in_addr *)&g_ctl_ip),
                      g_ctl_port, strstr(st, "PPP") ? "WANPPPConnection" : "WANIPConnection");
            return 1;
        }
        free(desc);
    }
    return 0;
}

static int soap(const char *action, const char *args, char *out, int outlen)
{
    char body[1024], req[2048];
    int blen, rlen;
    blen = snprintf(body, sizeof body,
        "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
        "<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>", action, g_svc, args, action);
    rlen = snprintf(req, sizeof req,
        "POST %s HTTP/1.0\r\nHost: %s:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\n\r\n%s",
        g_ctl_path, inet_ntoa(*(struct in_addr *)&g_ctl_ip), g_ctl_port, g_svc, action, blen, body);
    return http(g_ctl_ip, g_ctl_port, req, rlen, out, outlen);
}

/* make sure the IGD and its external address are known; caller holds g_ucs */
static int ensure_igd(void)
{
    char resp[4096], ip[64];
    DWORD now = GetTickCount();

    if (g_have_igd && g_ext_ip && now - g_ext_when < CACHE_TTL_MS)
        return 1;
    if (!g_have_igd && g_fail_when && now - g_fail_when < RETRY_MS)
        return 0;
    if (!g_have_igd && !(g_have_igd = find_igd())) {
        g_fail_when = now ? now : 1;
        sclan_log("upnp: no router (IGD) found - hairpin workaround inactive");
        return 0;
    }
    if (soap("GetExternalIPAddress", "", resp, sizeof resp) > 0 &&
        xml_tag(resp, "NewExternalIPAddress", ip, sizeof ip) && inet_addr(ip) != INADDR_NONE) {
        if (g_ext_ip != inet_addr(ip))
            sclan_log("upnp: router external address %s", ip);
        g_ext_ip = inet_addr(ip);
        g_ext_when = now;
    }
    return g_ext_ip != 0;
}

static DWORD WINAPI warmup(LPVOID unused)
{
    (void)unused;
    EnterCriticalSection(&g_ucs);
    ensure_igd();
    LeaveCriticalSection(&g_ucs);
    return 0;
}

void sclan_upnp_start(void)
{
    if (!g_cfg.hairpin_fix)
        return;
    if (InterlockedCompareExchange(&g_uinit, 1, 0) == 0) {
        InitializeCriticalSection(&g_ucs);
        InterlockedExchange(&g_uinit, 2);
        CloseHandle(CreateThread(NULL, 0, warmup, NULL, 0, NULL));
    }
}

static int is_private(ULONG a)
{
    ULONG h = ntohl(a);
    return (h >> 24) == 10 || (h >> 24) == 127 || (h >> 20) == 0xAC1 || (h >> 16) == 0xC0A8 ||
           (h >> 16) == 0xA9FE || h == 0 || h == 0xFFFFFFFFu;
}

/*
 * If `to` is the router's public address, fill `alt` with the LAN address the
 * UPnP mapping points to and return 1. Cheap for every other address.
 */
int sclan_hairpin_translate(const struct sockaddr *to, int tolen, int tcp, struct sockaddr_in *alt)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)to;
    char args[512], resp[4096], cl[64], port[16];
    int i, hit = 0;
    DWORD now;

    if (!g_cfg.hairpin_fix || g_uinit != 2 || !to || tolen < (int)sizeof *sin ||
        sin->sin_family != AF_INET || is_private(sin->sin_addr.s_addr) ||
        sclan_is_local_addr(sin->sin_addr.s_addr))
        return 0;

    EnterCriticalSection(&g_ucs);
    now = GetTickCount();
    {
        static ULONG seen[32];
        static int nseen;
        int k;
        for (k = 0; k < nseen && seen[k] != sin->sin_addr.s_addr; k++)
            ;
        if (k == nseen && nseen < 32) {
            seen[nseen++] = sin->sin_addr.s_addr;
            sclan_log("hairpin: %s to public address %s:%u", tcp ? "connect" : "sendto",
                      inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
        }
    }
    if (!ensure_igd() || sin->sin_addr.s_addr != g_ext_ip)
        goto out;

    for (i = 0; i < g_ncache; i++) {
        map_entry *e = &g_cache[i];
        if (e->ext_ip == sin->sin_addr.s_addr && e->ext_port == sin->sin_port && e->tcp == tcp &&
            now - e->when < CACHE_TTL_MS) {
            hit = e->in_port != 0;
            if (hit) {
                *alt = *sin;
                alt->sin_addr.s_addr = e->in_ip;
                alt->sin_port = e->in_port;
            }
            goto out;
        }
    }

    snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
             "<NewProtocol>%s</NewProtocol>", ntohs(sin->sin_port), tcp ? "TCP" : "UDP");
    {
        map_entry e;
        memset(&e, 0, sizeof e);
        e.ext_ip = sin->sin_addr.s_addr;
        e.ext_port = sin->sin_port;
        e.tcp = tcp;
        e.when = now;
        if (soap("GetSpecificPortMappingEntry", args, resp, sizeof resp) > 0 &&
            xml_tag(resp, "NewInternalClient", cl, sizeof cl) &&
            xml_tag(resp, "NewInternalPort", port, sizeof port) && inet_addr(cl) != INADDR_NONE) {
            e.in_ip = inet_addr(cl);
            e.in_port = htons((USHORT)atoi(port));
        }
        if (g_ncache < CACHE_MAX)
            g_cache[g_ncache++] = e;
        else
            g_cache[now % CACHE_MAX] = e;
        {
            char from[32];
            lstrcpynA(from, inet_ntoa(sin->sin_addr), sizeof from);
            if (e.in_port)
                sclan_log("hairpin: %s:%u/%s is a UPnP mapping -> connecting to %s:%u instead",
                          from, ntohs(sin->sin_port), tcp ? "tcp" : "udp",
                          inet_ntoa(*(struct in_addr *)&e.in_ip), ntohs(e.in_port));
            else
                sclan_log("hairpin: no UPnP mapping for %s:%u/%s", from, ntohs(sin->sin_port),
                          tcp ? "tcp" : "udp");
        }
        if (e.in_port) {
            *alt = *sin;
            alt->sin_addr.s_addr = e.in_ip;
            alt->sin_port = e.in_port;
            hit = 1;
        }
    }
out:
    LeaveCriticalSection(&g_ucs);
    return hit;
}

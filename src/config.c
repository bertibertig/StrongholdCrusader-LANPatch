#include "sclan.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

sclan_config g_cfg;
char g_moddir[MAX_PATH];

static CRITICAL_SECTION g_logcs;
static FILE *g_logf;

static int ini_int(const char *ini, const char *key, int def)
{
    char buf[64], *env, envname[96];
    /* environment overrides (handy for automated tests): SCLAN_<KEY> */
    snprintf(envname, sizeof envname, "SCLAN_%s", key);
    if ((env = getenv(envname)) && *env)
        return atoi(env);
    GetPrivateProfileStringA("LANPatch", key, "", buf, sizeof buf, ini);
    return buf[0] ? atoi(buf) : def;
}

static void ini_str(const char *ini, const char *key, const char *def, char *out, int len)
{
    char *env, envname[96];
    snprintf(envname, sizeof envname, "SCLAN_%s", key);
    if ((env = getenv(envname))) {
        lstrcpynA(out, env, len);
        return;
    }
    GetPrivateProfileStringA("LANPatch", key, def, out, len, ini);
}

void sclan_config_load(void)
{
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\sclanpatch.ini", g_moddir);

    g_cfg.enabled                = ini_int(ini, "Enabled", 1);
    g_cfg.discovery_port         = ini_int(ini, "DiscoveryPort", 47630);
    ini_str(ini, "Peers", "", g_cfg.peers, sizeof g_cfg.peers);
    g_cfg.subnet_scan            = ini_int(ini, "SubnetScan", 1);
    g_cfg.max_scan_prefix        = ini_int(ini, "MaxScanPrefix", 22);
    g_cfg.scan_interval_ms       = ini_int(ini, "ScanIntervalMs", 3000);
    g_cfg.connect_timeout_ms     = ini_int(ini, "ConnectTimeoutMs", 400);
    g_cfg.broadcast_all_adapters = ini_int(ini, "BroadcastAllAdapters", 1);
    g_cfg.unicast_to_peers       = ini_int(ini, "UnicastToPeers", 1);
    g_cfg.tcp_nodelay            = ini_int(ini, "TcpNoDelay", 1);
    g_cfg.tcp_keepalive          = ini_int(ini, "TcpKeepAlive", 1);
    g_cfg.prefer_lan_address     = ini_int(ini, "PreferLanAddress", 1);
    ini_str(ini, "PreferredIP", "", g_cfg.preferred_ip, sizeof g_cfg.preferred_ip);
    ini_str(ini, "IgnoreAdapters", "", g_cfg.ignore_adapters, sizeof g_cfg.ignore_adapters);
    g_cfg.log                    = ini_int(ini, "Log", 1);

    if (g_cfg.scan_interval_ms < 500) g_cfg.scan_interval_ms = 500;
    if (g_cfg.connect_timeout_ms < 50) g_cfg.connect_timeout_ms = 50;
    if (g_cfg.max_scan_prefix < 16) g_cfg.max_scan_prefix = 16;
}

void sclan_log_init(void)
{
    char path[MAX_PATH];
    InitializeCriticalSection(&g_logcs);
    if (!g_cfg.log)
        return;
    snprintf(path, sizeof path, "%s\\sclanpatch.log", g_moddir);
    g_logf = fopen(path, "w");
}

void sclan_log(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME st;
    if (!g_logf)
        return;
    GetLocalTime(&st);
    EnterCriticalSection(&g_logcs);
    fprintf(g_logf, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(g_logf, fmt, ap);
    va_end(ap);
    fputc('\n', g_logf);
    fflush(g_logf);
    LeaveCriticalSection(&g_logcs);
}

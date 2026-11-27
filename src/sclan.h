/*
 * SC LAN Patch - DirectPlay proxy for Stronghold Crusader (HD / Extreme HD)
 *
 * Shared declarations.
 */
#ifndef SCLAN_H
#define SCLAN_H

#define FD_SETSIZE 1024
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>

#define SCLAN_VERSION        "1.1.0"
#define DPLAY_ENUM_PORT      47624   /* DirectPlay TCP/IP SP enumeration port */
#define SCLAN_MAX_PEERS      256
#define SCLAN_MAX_IFACES     32

typedef struct {
    int   enabled;
    int   discovery_port;          /* TCP port of the discovery service */
    char  peers[1024];             /* comma separated static peers (IPs / hostnames) */
    int   subnet_scan;             /* scan local subnets over TCP */
    int   max_scan_prefix;         /* subnets larger than this are clipped to a /24 */
    int   scan_interval_ms;
    int   connect_timeout_ms;
    int   broadcast_all_adapters;  /* fan out the DirectPlay broadcast to every adapter */
    int   unicast_to_peers;        /* send DirectPlay enum requests directly to known peers */
    int   tcp_nodelay;             /* disable Nagle on DirectPlay stream sockets */
    int   tcp_keepalive;
    int   block_nat_help;          /* keep dpwsockx from loading the UPnP NAT helper */
    int   hairpin_fix;             /* connect to the LAN target of UPnP mappings of our own router */
    int   prefer_lan_address;      /* sort gethostbyname() results: real LAN first */
    char  preferred_ip[64];        /* explicit address that must come first */
    char  ignore_adapters[512];    /* comma separated substrings of adapter names to ignore */
    int   log;
    double ui_scale;               /* 1.0 = off, 1.5, 2 ... */
    int   ui_debug;                /* extra logging + memory dumps on request */
} sclan_config;

typedef struct {
    ULONG addr;       /* network byte order */
    ULONG mask;       /* network byte order */
    ULONG bcast;      /* network byte order */
    int   has_gateway;
    int   is_virtual;
    char  name[128];
} sclan_iface;

extern sclan_config g_cfg;
extern char g_moddir[MAX_PATH];

/* log.c */
void sclan_log_init(void);
void sclan_log(const char *fmt, ...);

/* config.c */
void sclan_config_load(void);

/* net.c - interfaces */
void sclan_net_start(void);                     /* background interface refresher */
int  sclan_ifaces(sclan_iface *out, int max);   /* cached copy, never blocks long */
int  sclan_is_local_addr(ULONG addr);

/* hooks.c */
void sclan_hooks_install_exe(void);
void sclan_hooks_on_dplay_loaded(void);
int  sclan_patch_iat(HMODULE mod, const void *target, const void *repl);

/* uiscale.c */
void sclan_ui_install(void);

/* proxy.c */
HMODULE sclan_real_dplayx(void);

/* upnp.c - NAT hairpin workaround */
void sclan_upnp_start(void);
int  sclan_hairpin_translate(const struct sockaddr *to, int tolen, int tcp, struct sockaddr_in *alt);

/* discovery.c */
void sclan_discovery_start(void);
void sclan_discovery_kick(void);                /* called whenever an enumeration broadcast happens */
int  sclan_discovery_peers(ULONG *out, int max);
void sclan_resend_enum_to(ULONG addr);          /* hooks.c */

#endif

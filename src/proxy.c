/*
 * dplayx.dll proxy: forwards every export to the system dplayx.dll and
 * installs the LAN hooks.
 */
#include "sclan.h"
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *DirectPlayCreate_fn)(LPGUID, void **, IUnknown *);
typedef HRESULT (WINAPI *DirectPlayEnumerate_fn)(void *, LPVOID);
typedef HRESULT (WINAPI *DirectPlayLobbyCreate_fn)(LPGUID, void **, IUnknown *, LPVOID, DWORD);
typedef HRESULT (WINAPI *DllGetClassObject_fn)(REFCLSID, REFIID, LPVOID *);

DWORD gdwDPlaySPRefCount;

static HMODULE g_self;
static HMODULE g_real;
static CRITICAL_SECTION g_realcs;

HMODULE sclan_real_dplayx(void)
{
    char path[MAX_PATH];
    if (g_real)
        return g_real;
    EnterCriticalSection(&g_realcs);
    if (!g_real) {
        /* in a 32 bit process on 64 bit Windows this is redirected to SysWOW64 */
        GetSystemDirectoryA(path, sizeof path);
        lstrcatA(path, "\\dplayx.dll");
        g_real = LoadLibraryA(path);
        if (g_real == g_self) {           /* must never happen, but don't recurse */
            FreeLibrary(g_real);
            g_real = NULL;
        }
        sclan_log("system dplayx: %s -> %p", path, g_real);
        if (!g_real)
            MessageBoxA(NULL,
                "DirectPlay (dplayx.dll) wurde nicht gefunden.\n\n"
                "Windows 10/11: install.bat als Administrator ausfuehren oder\n"
                "Systemsteuerung > Programme > Windows-Features > Legacykomponenten > DirectPlay aktivieren.",
                "SC LAN Patch", MB_ICONERROR);
    }
    LeaveCriticalSection(&g_realcs);
    return g_real;
}

static FARPROC real_proc(const char *name)
{
    HMODULE h = sclan_real_dplayx();
    return h ? GetProcAddress(h, name) : NULL;
}

#define DPERR_GENERIC_ ((HRESULT)0x80004005L)

HRESULT WINAPI DirectPlayCreate(LPGUID sp, void **dp, IUnknown *outer)
{
    DirectPlayCreate_fn f = (DirectPlayCreate_fn)real_proc("DirectPlayCreate");
    HRESULT hr = f ? f(sp, dp, outer) : DPERR_GENERIC_;
    sclan_hooks_on_dplay_loaded();
    return hr;
}

HRESULT WINAPI DirectPlayEnumerateA(void *cb, LPVOID ctx)
{
    DirectPlayEnumerate_fn f = (DirectPlayEnumerate_fn)real_proc("DirectPlayEnumerateA");
    return f ? f(cb, ctx) : DPERR_GENERIC_;
}

HRESULT WINAPI DirectPlayEnumerateW(void *cb, LPVOID ctx)
{
    DirectPlayEnumerate_fn f = (DirectPlayEnumerate_fn)real_proc("DirectPlayEnumerateW");
    return f ? f(cb, ctx) : DPERR_GENERIC_;
}

HRESULT WINAPI DirectPlayEnumerate(void *cb, LPVOID ctx)
{
    return DirectPlayEnumerateA(cb, ctx);
}

HRESULT WINAPI DirectPlayLobbyCreateA(LPGUID g, void **lp, IUnknown *o, LPVOID d, DWORD s)
{
    DirectPlayLobbyCreate_fn f = (DirectPlayLobbyCreate_fn)real_proc("DirectPlayLobbyCreateA");
    HRESULT hr = f ? f(g, lp, o, d, s) : DPERR_GENERIC_;
    sclan_hooks_on_dplay_loaded();
    return hr;
}

HRESULT WINAPI DirectPlayLobbyCreateW(LPGUID g, void **lp, IUnknown *o, LPVOID d, DWORD s)
{
    DirectPlayLobbyCreate_fn f = (DirectPlayLobbyCreate_fn)real_proc("DirectPlayLobbyCreateW");
    HRESULT hr = f ? f(g, lp, o, d, s) : DPERR_GENERIC_;
    sclan_hooks_on_dplay_loaded();
    return hr;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID *ppv)
{
    DllGetClassObject_fn f = (DllGetClassObject_fn)real_proc("DllGetClassObject");
    HRESULT hr = f ? f(clsid, iid, ppv) : CLASS_E_CLASSNOTAVAILABLE;
    sclan_hooks_on_dplay_loaded();
    return hr;
}

HRESULT WINAPI DllRegisterServer(void)   { return S_OK; }
HRESULT WINAPI DllUnregisterServer(void) { return S_OK; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char *slash;
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_realcs);

        GetModuleFileNameA(NULL, g_moddir, sizeof g_moddir);
        if ((slash = strrchr(g_moddir, '\\')))
            *slash = 0;

        sclan_config_load();
        sclan_log_init();
        sclan_log("SC LAN Patch " SCLAN_VERSION " loaded (enabled=%d)", g_cfg.enabled);
        {
            char exe[MAX_PATH];
            GetModuleFileNameA(NULL, exe, sizeof exe);
            sclan_log("game: %s", (slash = strrchr(exe, '\\')) ? slash + 1 : exe);
        }
        if (g_cfg.enabled)
            sclan_hooks_install_exe();
        if (g_cfg.ui_scale > 1.0 || g_cfg.ui_debug)
            sclan_ui_install();
    }
    return TRUE;
}

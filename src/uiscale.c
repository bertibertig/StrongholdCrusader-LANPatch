/*
 * UI scaling for Stronghold Crusader HD / Extreme HD.
 *
 * The game renders each frame in software into a system-memory DirectDraw
 * surface ("frame") and copies it to the screen with Blt/BltFast (then Flip
 * in fullscreen). At high resolutions the map is nice and wide, but the UI
 * is drawn at its original pixel size and becomes tiny.
 *
 * We hook DirectDraw (exe import of DirectDrawCreate + the COM vtables).
 * Whenever the game copies its frame to the screen, we additionally copy
 * enlarged versions of the UI areas on top:
 *
 *   menus        the 1024x768 menu page in the middle of the screen
 *   in game      the bottom panel incl. the scribe, and an open dialog
 *                (Esc menu, options, confirmations ...)
 *
 * The map keeps the full resolution. Mouse coordinates (window messages and
 * GetCursorPos) that fall into an enlarged area are mapped back, so the game
 * sees the click where it drew the button.
 *
 * Game state comes from two globals of the exe, located by a code signature
 * so both Stronghold Crusader.exe and Stronghold_Crusader_Extreme.exe work:
 *   scene         0x0e = in game, other values = menus
 *   dialog block  scene-0x88: id, x, y, w, h ... ; scene-0x60: open id (-1 = none)
 */
#include "sclan.h"
#include <ddraw.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SCENE_INGAME   0x0e
#define SCENE_STARTUP  0
#define MAX_REGIONS    3

/* ------------------------------------------------------------ hooks */

typedef HRESULT (WINAPI *dd_create_fn)(GUID *, LPDIRECTDRAW *, IUnknown *);
typedef HRESULT (WINAPI *dd_createsurface_fn)(IDirectDraw *, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE *, IUnknown *);
typedef HRESULT (WINAPI *dd_setmode_fn)(IDirectDraw *, DWORD, DWORD, DWORD);
typedef HRESULT (WINAPI *dd_setcoop_fn)(IDirectDraw *, HWND, DWORD);
typedef HRESULT (WINAPI *ds_blt_fn)(IDirectDrawSurface *, LPRECT, IDirectDrawSurface *, LPRECT, DWORD, LPDDBLTFX);
typedef HRESULT (WINAPI *ds_bltfast_fn)(IDirectDrawSurface *, DWORD, DWORD, IDirectDrawSurface *, LPRECT, DWORD);
typedef HRESULT (WINAPI *ds_flip_fn)(IDirectDrawSurface *, IDirectDrawSurface *, DWORD);
typedef ULONG   (WINAPI *ds_release_fn)(IDirectDrawSurface *);
typedef HRESULT (WINAPI *ds_lock_fn)(IDirectDrawSurface *, LPRECT, LPDDSURFACEDESC, DWORD, HANDLE);
typedef HRESULT (WINAPI *ds_getdc_fn)(IDirectDrawSurface *, HDC *);
typedef BOOL    (WINAPI *gcp_fn)(LPPOINT);
typedef ATOM    (WINAPI *rca_fn)(const WNDCLASSA *);

static dd_create_fn        real_DirectDrawCreate;
static dd_createsurface_fn real_CreateSurface;
static dd_setmode_fn       real_SetDisplayMode;
static dd_setcoop_fn       real_SetCooperativeLevel;
static ds_blt_fn           real_Blt;
static ds_bltfast_fn       real_BltFast;
static ds_flip_fn          real_Flip;
static ds_release_fn       real_Release;
static ds_lock_fn          real_Lock;
static ds_getdc_fn         real_GetDC;
static gcp_fn              real_GetCursorPos;
static rca_fn              real_RegisterClassA;
static WNDPROC             real_WndProc;

/* ------------------------------------------------------------ state */

typedef struct {
    RECT  src;          /* UI area in game coordinates */
    RECT  dst;          /* where it ends up on screen (src scaled about the anchor) */
    double ax, ay;      /* anchor (fixed point of the scaling) */
    double k;           /* factor */
    int   kind;         /* 0 = whole rect is UI, 1 = in-game panel (star-shaped mask) */
} region;

static CRITICAL_SECTION g_cs;
static IDirectDrawSurface *g_primary, *g_back, *g_frame, *g_tmp, *g_shadow;
static IDirectDraw *g_dd;
static HWND  g_wnd;
static int   g_fullscreen;
static int   g_w, g_h;                 /* size of the game frame */
static int   g_565 = 1;
static volatile LONG *g_scene;         /* located by signature, may stay NULL */
static int   g_restore;                /* full unscaled copies still to do after a layout change */
static region g_last[MAX_REGIONS];
static int   g_nlast;
static int   g_last_scene = -12345;
static LONG  g_dbg_left;

static void patch_vtbl(void *obj, int slot, void *repl, void **orig)
{
    void **vt = *(void ***)obj;
    DWORD old;
    if (vt[slot] == repl)
        return;
    if (!*orig)
        *orig = vt[slot];
    if (VirtualProtect(&vt[slot], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
        vt[slot] = repl;
        VirtualProtect(&vt[slot], sizeof(void *), old, &old);
    }
}

/* ----------------------------------------------------- game globals */

/* Find the scene variable: the global that the code compares most often
   against the scene ids 0x10 / 0x0e / 0x0c (cmp dword ptr [abs32], imm8). */
static void find_scene_var(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    DWORD lo = 0, hi = 0;
    BYTE *t = NULL;
    DWORD tlen = 0, i;
    enum { NC = 64 };
    DWORD cand[NC], cnt[NC][3];
    int nc = 0, j, best = -1, bestn = 0;

    for (j = 0; j < nt->FileHeader.NumberOfSections; j++) {
        if (!memcmp(sec[j].Name, ".text", 5)) {
            t = base + sec[j].VirtualAddress;
            tlen = sec[j].Misc.VirtualSize;
        }
        if (!memcmp(sec[j].Name, ".data", 5)) {
            lo = (DWORD)(base + sec[j].VirtualAddress);
            hi = lo + sec[j].Misc.VirtualSize;
        }
    }
    if (!t || !lo)
        return;
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i + 7 <= tlen; i++) {
        DWORD a;
        int which;
        if (t[i] != 0x83 || t[i + 1] != 0x3d)
            continue;
        which = t[i + 6] == 0x10 ? 0 : t[i + 6] == 0x0e ? 1 : t[i + 6] == 0x0c ? 2 : -1;
        if (which < 0)
            continue;
        memcpy(&a, t + i + 2, 4);
        if (a < lo || a + 4 > hi)
            continue;
        for (j = 0; j < nc && cand[j] != a; j++)
            ;
        if (j == nc) {
            if (nc == NC)
                continue;
            cand[nc++] = a;
        }
        cnt[j][which]++;
    }
    for (j = 0; j < nc; j++) {
        int n = cnt[j][0] + cnt[j][1] + cnt[j][2];
        if (cnt[j][0] >= 10 && cnt[j][1] >= 5 && cnt[j][2] >= 5 && n > bestn) {
            best = j;
            bestn = n;
        }
    }
    if (best >= 0 && cand[best] - 0x88 >= lo) {
        g_scene = (volatile LONG *)cand[best];
        sclan_log("ui: scene variable at %p (%d references)", (void *)g_scene, bestn);
    } else {
        sclan_log("ui: scene variable NOT found - only the in-game panel is scaled");
    }
}

static int read_scene(void)
{
    return g_scene ? (int)*g_scene : SCENE_INGAME;
}

/* open dialog in game coordinates, 0 if none */
static int read_dialog(RECT *r)
{
    volatile LONG *b;
    if (!g_scene)
        return 0;
    b = (volatile LONG *)((BYTE *)g_scene - 0x88);
    if (*(volatile LONG *)((BYTE *)g_scene - 0x60) == -1)
        return 0;
    if (b[3] <= 0 || b[4] <= 0 || b[3] > g_w || b[4] > g_h)
        return 0;
    r->left = b[1];
    r->top = b[2];
    r->right = b[1] + b[3];
    r->bottom = b[2] + b[4];
    if (r->left < 0 || r->top < 0 || r->right > g_w || r->bottom > g_h)
        return 0;
    return 1;
}

/* ------------------------------------------------------------ layout */

static double fit(double k, double need_w, double need_h, double avail_w, double avail_h)
{
    if (need_w * k > avail_w) k = avail_w / need_w;
    if (need_h * k > avail_h) k = avail_h / need_h;
    return k < 1.0 ? 1.0 : k;
}

static void set_dst(region *r)
{
    r->dst.left   = (LONG)floor(r->ax + (r->src.left   - r->ax) * r->k);
    r->dst.top    = (LONG)floor(r->ay + (r->src.top    - r->ay) * r->k);
    r->dst.right  = (LONG)ceil (r->ax + (r->src.right  - r->ax) * r->k);
    r->dst.bottom = (LONG)ceil (r->ay + (r->src.bottom - r->ay) * r->k);
    if (r->dst.left < 0) r->dst.left = 0;
    if (r->dst.top < 0) r->dst.top = 0;
    if (r->dst.right > g_w) r->dst.right = g_w;
    if (r->dst.bottom > g_h) r->dst.bottom = g_h;
}

/* In-game panel geometry (same for every resolution, centred at the bottom):
   800 px panel strip of 128 px with pillars reaching ~24 px higher; the scribe
   (hood, shoulders, book) sticks out right of the strip and above it. */
#define PANEL_HALF   400
#define PANEL_H      128
#define PILLAR_H     152
#define SCRIBE_R     472          /* right edge of the scribe's book */
#define SCRIBE_H     195          /* top of the hood above the bottom edge */

static int build_layout(region *out)
{
    int n = 0, scene = read_scene();
    double k = g_cfg.ui_scale;
    double cx = g_w / 2.0;
    RECT dlg;

    if (k <= 1.0 || g_w < 800 || g_h < 600)
        return 0;
    if (scene == SCENE_STARTUP)             /* intro videos */
        return 0;
    if (scene == SCENE_INGAME) {
        region *r = &out[n++];
        r->kind = 1;
        r->ax = cx;
        r->ay = g_h;
        r->k = fit(k, 2.0 * SCRIBE_R, SCRIBE_H, g_w, g_h * 0.5);
        r->src.left = (LONG)(cx - PANEL_HALF);
        r->src.right = (LONG)(cx + SCRIBE_R);
        r->src.top = g_h - SCRIBE_H;
        r->src.bottom = g_h;
        /* scale symmetrically: the dst rect must cover the left half too */
        set_dst(r);
        if (read_dialog(&dlg)) {
            r = &out[n++];
            r->kind = 0;
            r->src = dlg;
            r->ax = (dlg.left + dlg.right) / 2.0;
            r->ay = (dlg.top + dlg.bottom) / 2.0;
            r->k = fit(k, dlg.right - dlg.left, dlg.bottom - dlg.top, g_w, g_h);
            /* keep the enlarged dialog on screen */
            {
                double hw = (dlg.right - dlg.left) * r->k / 2, hh = (dlg.bottom - dlg.top) * r->k / 2;
                double nx = r->ax, ny = r->ay;
                if (nx - hw < 0) nx = hw;
                if (nx + hw > g_w) nx = g_w - hw;
                if (ny - hh < 0) ny = hh;
                if (ny + hh > g_h) ny = g_h - hh;
                /* anchor so that the dialog centre moves to (nx, ny) */
                if (r->k > 1.0) {
                    r->ax = (nx - r->k * r->ax) / (1.0 - r->k);
                    r->ay = (ny - r->k * r->ay) / (1.0 - r->k);
                }
            }
            set_dst(r);
        }
    } else {
        /* menus: all controls live in the 800x600 core in the middle; the
           1024x768 page around it adds the stone frame. Take the whole page
           when it fits with the full factor, otherwise just the core. */
        region *r = &out[n++];
        int pw = 1024, ph = 768;
        if (fit(k, pw, ph, g_w, g_h) < k - 0.001) {
            pw = 800;
            ph = 600;
        }
        r->kind = 0;
        r->ax = cx;
        r->ay = g_h / 2.0;
        r->k = fit(k, pw, ph, g_w, g_h);
        r->src.left = (LONG)(cx - pw / 2);
        r->src.top = (LONG)(g_h / 2.0 - ph / 2);
        r->src.right = r->src.left + pw;
        r->src.bottom = r->src.top + ph;
        set_dst(r);
    }
    for (int i = n - 1; i >= 0; i--)
        if (out[i].k <= 1.0001) {
            memmove(&out[i], &out[i + 1], (n - i - 1) * sizeof *out);
            n--;
        }
    return n;
}

/* is game pixel (x, y) part of the UI shape of r? */
static int in_shape(const region *r, double x, double y)
{
    double cx, rel, h;
    if (x < r->src.left || x >= r->src.right || y < r->src.top || y >= r->src.bottom)
        return 0;
    if (r->kind == 0)
        return 1;
    /* panel: band + pillar tips, plus the scribe's silhouette */
    cx = r->ax;
    rel = x - cx;
    h = g_h - y;                                  /* height above the bottom edge */
    if (rel >= -PANEL_HALF && rel < PANEL_HALF) {
        int top = PANEL_H;                        /* things sticking out of the strip: */
        if (rel < -383) top = PILLAR_H;           /*   left pillar */
        else if (rel >= 112 && rel < 142) top = 138;   /* red i/x column */
        else if (rel >= 142 && rel < 164) top = PILLAR_H; /* pillar */
        else if (rel >= 164 && rel < 308) top = 146;   /* minimap frame */
        if (h <= top)
            return 1;
    }
    if (h <= 97)                                  /* book and hands */
        return rel >= 288 && rel < SCRIBE_R;
    if (h <= PANEL_H)                             /* shoulders */
        return rel >= 300 && rel < 455;
    if (h <= SCRIBE_H) {                          /* hood, narrowing upwards */
        double t = (h - PANEL_H) / (double)(SCRIBE_H - PANEL_H);
        return rel >= 318 + t * (350 - 318) && rel < 430 + t * (405 - 430);
    }
    return 0;
}

/* screen -> game coordinates; returns 1 if p was inside an enlarged area */
static int map_point(LONG *x, LONG *y)
{
    region rs[MAX_REGIONS];
    int n, i;
    EnterCriticalSection(&g_cs);
    n = build_layout(rs);
    LeaveCriticalSection(&g_cs);
    for (i = n - 1; i >= 0; i--) {
        const region *r = &rs[i];
        double sx, sy;
        if (*x < r->dst.left || *x >= r->dst.right || *y < r->dst.top || *y >= r->dst.bottom)
            continue;
        sx = r->ax + (*x + 0.5 - r->ax) / r->k;
        sy = r->ay + (*y + 0.5 - r->ay) / r->k;
        if (!in_shape(r, sx, sy))
            continue;
        *x = (LONG)sx;
        *y = (LONG)sy;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------- pixels */

static inline unsigned px_lerp(unsigned a, unsigned b, unsigned c, unsigned d, unsigned fx, unsigned fy)
{
    /* fx, fy in 0..64; works on 565 and 555 by spreading the word into 32 bits */
    unsigned ea = (a | (a << 16)) & 0x07e0f81f, eb = (b | (b << 16)) & 0x07e0f81f;
    unsigned ec = (c | (c << 16)) & 0x07e0f81f, ed = (d | (d << 16)) & 0x07e0f81f;
    unsigned w00 = (64 - fx) * (64 - fy) >> 7, w10 = fx * (64 - fy) >> 7;
    unsigned w01 = (64 - fx) * fy >> 7, w11 = 32 - w00 - w10 - w01;  /* sum = 32 */
    unsigned e = ((ea * w00 + eb * w10 + ec * w01 + ed * w11) >> 5) & 0x07e0f81f;
    return (e | (e >> 16)) & 0xffff;
}

static inline unsigned px_lerp555(unsigned a, unsigned b, unsigned c, unsigned d, unsigned fx, unsigned fy)
{
    unsigned ea = (a | (a << 16)) & 0x03e07c1f, eb = (b | (b << 16)) & 0x03e07c1f;
    unsigned ec = (c | (c << 16)) & 0x03e07c1f, ed = (d | (d << 16)) & 0x03e07c1f;
    unsigned w00 = (64 - fx) * (64 - fy) >> 7, w10 = fx * (64 - fy) >> 7;
    unsigned w01 = (64 - fx) * fy >> 7, w11 = 32 - w00 - w10 - w01;
    unsigned e = ((ea * w00 + eb * w10 + ec * w01 + ed * w11) >> 5) & 0x03e07c1f;
    return (e | (e >> 16)) & 0xffff;
}

/* Render region r from the screen copy into tmp (same layout). Pixels whose
   source is not UI keep the screen's own (unscaled) pixel.
   Integer tables per column/row - this runs every frame on up to ~2 Mpixel. */
#define MAXW 8192
static int t_x0[MAXW], t_x1[MAXW], t_nx[MAXW];
static unsigned char t_fx[MAXW];
static int t_top[MAXW];                    /* per source column: first UI row */

static void render_region(const region *r, const BYTE *src, LONG spitch, BYTE *dst, LONG dpitch)
{
    int x, y, dl = r->dst.left, dr = r->dst.right;
    int sharp = fabs(r->k - floor(r->k + 0.5)) < 0.01;   /* integer factor: crisp pixels */
    double inv = 1.0 / r->k;

    if (dr > MAXW || r->src.right > MAXW)
        return;
    for (x = dl; x < dr; x++) {
        double sx = r->ax + (x + 0.5 - r->ax) * inv - 0.5;
        int x0 = (int)floor(sx), nx = (int)floor(sx + 0.5);
        t_fx[x] = (unsigned char)((sx - x0) * 64);
        t_x1[x] = x0 + 1;
        if (x0 < r->src.left) x0 = r->src.left;
        if (x0 >= r->src.right) x0 = r->src.right - 1;
        if (t_x1[x] >= r->src.right) t_x1[x] = r->src.right - 1;
        if (t_x1[x] < r->src.left) t_x1[x] = r->src.left;
        t_x0[x] = x0;
        t_nx[x] = nx;                      /* may lie outside the source: not UI then */
    }
    for (x = r->src.left; x < r->src.right; x++) {
        if (r->kind == 0) {
            t_top[x] = r->src.top;
        } else {                           /* the panel shape is bottom-aligned per column */
            for (y = r->src.top; y < r->src.bottom && !in_shape(r, x + 0.5, y + 0.5); y++)
                ;
            t_top[x] = y;
        }
    }
    for (y = r->dst.top; y < r->dst.bottom; y++) {
        WORD *d = (WORD *)(dst + y * dpitch);
        const WORD *orig = (const WORD *)(src + y * spitch);
        double sy = r->ay + (y + 0.5 - r->ay) * inv - 0.5;
        int y0 = (int)floor(sy), ny = (int)floor(sy + 0.5), y1;
        unsigned fy = (unsigned)((sy - y0) * 64);
        const WORD *r0, *r1, *rn;
        y1 = y0 + 1;
        if (y0 < r->src.top) y0 = r->src.top;
        if (y0 >= r->src.bottom) y0 = r->src.bottom - 1;
        if (y1 >= r->src.bottom) y1 = r->src.bottom - 1;
        if (y1 < r->src.top) y1 = r->src.top;
        r0 = (const WORD *)(src + y0 * spitch);
        r1 = (const WORD *)(src + y1 * spitch);
        rn = (const WORD *)(src + (ny < r->src.top ? r->src.top : ny >= r->src.bottom ? r->src.bottom - 1 : ny) * spitch);
        for (x = dl; x < dr; x++) {
            int nx = t_nx[x];
            if (nx < r->src.left || nx >= r->src.right || ny < t_top[nx] || ny >= r->src.bottom) {
                d[x] = orig[x];
            } else if (sharp) {
                d[x] = rn[nx];
            } else {
                int x0 = t_x0[x], x1 = t_x1[x];
                d[x] = (WORD)(g_565 ? px_lerp(r0[x0], r0[x1], r1[x0], r1[x1], t_fx[x], fy)
                                    : px_lerp555(r0[x0], r0[x1], r1[x0], r1[x1], t_fx[x], fy));
            }
        }
    }
}

static int same_layout(const region *a, int na, const region *b, int nb)
{
    int i;
    if (na != nb)
        return 0;
    for (i = 0; i < na; i++)
        if (memcmp(&a[i].dst, &b[i].dst, sizeof(RECT)) || memcmp(&a[i].src, &b[i].src, sizeof(RECT)))
            return 0;
    return 1;
}

static IDirectDrawSurface *make_sysmem_surface(void)
{
    DDSURFACEDESC d;
    IDirectDrawSurface *out = NULL;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    if (FAILED(g_frame->lpVtbl->GetSurfaceDesc(g_frame, &d)))
        return NULL;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    if (FAILED(real_CreateSurface(g_dd, &d, &out, NULL)))
        return NULL;
    g_565 = d.ddpfPixelFormat.dwGBitMask == 0x7e0;
    return out;
}

/* g_shadow: what the game put on the screen (unscaled), g_tmp: our output */
static int ensure_work(void)
{
    if (g_tmp && g_shadow)
        return 1;
    if (!g_dd || !g_frame)
        return 0;
    if (!g_shadow && !(g_shadow = make_sysmem_surface()))
        goto fail;
    if (!g_tmp && !(g_tmp = make_sysmem_surface()))
        goto fail;
    sclan_log("ui: work surfaces %dx%d %s ready", g_w, g_h, g_565 ? "RGB565" : "RGB555");
    return 1;
fail:
    sclan_log("ui: cannot create work surfaces");
    g_cfg.ui_scale = 1.0;           /* give up quietly instead of drawing garbage */
    return 0;
}

static void client_origin(LONG *ox, LONG *oy)
{
    POINT p = { 0, 0 };
    if (!g_fullscreen && g_wnd)
        ClientToScreen(g_wnd, &p);
    *ox = p.x;
    *oy = p.y;
}

/* Draw the enlarged UI areas onto `screen` ((ox, oy) = position of the game
   picture on that surface, non-zero only for a window). */
static void present_ui(IDirectDrawSurface *screen, LONG ox, LONG oy)
{
    region rs[MAX_REGIONS];
    DDSURFACEDESC fs, ts;
    int n, i, scene = read_scene();

    if (scene != g_last_scene) {
        if (g_cfg.ui_debug) {
            sclan_log("ui: scene %d -> %d", g_last_scene, scene);
            g_dbg_left = 30;
        }
        g_last_scene = scene;
    }
    if (!ensure_work())
        return;
    n = build_layout(rs);
    if (!same_layout(rs, n, g_last, g_nlast)) {
        /* old enlarged areas must disappear (from both buffers of a flip chain) */
        g_restore = g_fullscreen ? 2 : 1;
        memcpy(g_last, rs, sizeof rs);
        g_nlast = n;
    }
    if (g_restore > 0) {
        RECT all = { 0, 0, g_w, g_h };
        RECT d = { ox, oy, ox + g_w, oy + g_h };
        real_Blt(screen, &d, g_shadow, &all, DDBLT_WAIT, NULL);
        g_restore--;
    }
    if (!n)
        return;

    memset(&fs, 0, sizeof fs); fs.dwSize = sizeof fs;
    memset(&ts, 0, sizeof ts); ts.dwSize = sizeof ts;
    if (FAILED(g_shadow->lpVtbl->Lock(g_shadow, NULL, &fs, DDLOCK_WAIT | DDLOCK_READONLY, NULL)))
        return;
    if (FAILED(g_tmp->lpVtbl->Lock(g_tmp, NULL, &ts, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL))) {
        g_shadow->lpVtbl->Unlock(g_shadow, NULL);
        return;
    }
    {
        static LARGE_INTEGER freq, t0, t1;
        static double acc;
        static int frames;
        if (g_cfg.ui_debug)
            QueryPerformanceCounter(&t0);
        for (i = 0; i < n; i++)
            render_region(&rs[i], fs.lpSurface, fs.lPitch, ts.lpSurface, ts.lPitch);
        if (g_cfg.ui_debug) {
            QueryPerformanceCounter(&t1);
            if (!freq.QuadPart)
                QueryPerformanceFrequency(&freq);
            acc += (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
            if (++frames == 200) {
                sclan_log("ui: scaling takes %.2f ms per frame", acc * 1000 / frames);
                acc = 0;
                frames = 0;
            }
        }
    }
    g_tmp->lpVtbl->Unlock(g_tmp, NULL);
    g_shadow->lpVtbl->Unlock(g_shadow, NULL);

    for (i = 0; i < n; i++) {
        RECT s = rs[i].dst;
        RECT d = { s.left + ox, s.top + oy, s.right + ox, s.bottom + oy };
        real_Blt(screen, &d, g_tmp, &s, DDBLT_WAIT, NULL);
    }
}

/* ------------------------------------------------------------- surfaces */

static int is_screen(IDirectDrawSurface *s)
{
    return s && (s == g_primary || s == g_back);
}

static int scaling(void)
{
    return g_cfg.ui_scale > 1.0 && g_frame && g_dd;
}

static HRESULT WINAPI hk_Blt(IDirectDrawSurface *s, LPRECT dr, IDirectDrawSurface *src, LPRECT sr,
                             DWORD fl, LPDDBLTFX fx)
{
    HRESULT hr = real_Blt(s, dr, src, sr, fl, fx);
    if (g_cfg.ui_debug && InterlockedDecrement(&g_dbg_left) >= 0)
        sclan_log("ui: Blt dst=%p src=%p%s dr=%ld,%ld-%ld,%ld sr=%ld,%ld-%ld,%ld fl=%lx",
                  s, src, src == g_frame ? "(frame)" : "",
                  dr ? dr->left : -1, dr ? dr->top : -1, dr ? dr->right : -1, dr ? dr->bottom : -1,
                  sr ? sr->left : -1, sr ? sr->top : -1, sr ? sr->right : -1, sr ? sr->bottom : -1,
                  (unsigned long)fl);
    if (SUCCEEDED(hr) && is_screen(s) && scaling()) {
        EnterCriticalSection(&g_cs);
        if (ensure_work()) {
            LONG ox, oy;
            client_origin(&ox, &oy);
            if (dr) {
                RECT d = { dr->left - ox, dr->top - oy, dr->right - ox, dr->bottom - oy };
                real_Blt(g_shadow, &d, src, sr, fl & ~(DWORD)DDBLT_ASYNC, fx);
            } else {
                real_Blt(g_shadow, NULL, src, sr, fl & ~(DWORD)DDBLT_ASYNC, fx);
            }
            if (s == g_primary)                 /* window, or straight to the front buffer */
                present_ui(s, ox, oy);
        }
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

static HRESULT WINAPI hk_BltFast(IDirectDrawSurface *s, DWORD x, DWORD y, IDirectDrawSurface *src,
                                 LPRECT sr, DWORD fl)
{
    HRESULT hr = real_BltFast(s, x, y, src, sr, fl);
    if (g_cfg.ui_debug && InterlockedDecrement(&g_dbg_left) >= 0)
        sclan_log("ui: BltFast dst=%p at %lu,%lu src=%p%s sr=%ld,%ld-%ld,%ld fl=%lx",
                  s, (unsigned long)x, (unsigned long)y, src, src == g_frame ? "(frame)" : "",
                  sr ? sr->left : -1, sr ? sr->top : -1, sr ? sr->right : -1, sr ? sr->bottom : -1,
                  (unsigned long)fl);
    if (SUCCEEDED(hr) && is_screen(s) && scaling()) {
        EnterCriticalSection(&g_cs);
        if (ensure_work()) {
            LONG ox, oy;
            client_origin(&ox, &oy);
            real_BltFast(g_shadow, x - ox, y - oy, src, sr, fl);
            if (s == g_primary)
                present_ui(s, ox, oy);
        }
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

static HRESULT WINAPI hk_Flip(IDirectDrawSurface *s, IDirectDrawSurface *tgt, DWORD fl)
{
    if (g_cfg.ui_debug && InterlockedDecrement(&g_dbg_left) >= 0)
        sclan_log("ui: Flip %p", s);
    if (s == g_primary && g_back && scaling()) {
        EnterCriticalSection(&g_cs);
        present_ui(g_back, 0, 0);
        LeaveCriticalSection(&g_cs);
    }
    return real_Flip(s, tgt, fl);
}

static HRESULT WINAPI hk_Lock(IDirectDrawSurface *s, LPRECT r, LPDDSURFACEDESC d, DWORD fl, HANDLE h)
{
    if (s != g_frame && s != g_tmp && s != g_shadow && InterlockedDecrement(&g_dbg_left) >= 0)
        sclan_log("ui: Lock %p rect=%s fl=%lx", s, r ? "yes" : "all", (unsigned long)fl);
    return real_Lock(s, r, d, fl, h);
}

static HRESULT WINAPI hk_GetDC(IDirectDrawSurface *s, HDC *dc)
{
    if (InterlockedDecrement(&g_dbg_left) >= 0)
        sclan_log("ui: GetDC %p", s);
    return real_GetDC(s, dc);
}

static ULONG WINAPI hk_Release(IDirectDrawSurface *s)
{
    ULONG n;
    EnterCriticalSection(&g_cs);
    n = real_Release(s);
    if (n == 0) {
        if (s == g_frame) {
            g_frame = NULL;
            if (g_tmp)
                real_Release(g_tmp);
            if (g_shadow)
                real_Release(g_shadow);
            g_tmp = g_shadow = NULL;
        }
        if (s == g_primary)
            g_primary = g_back = NULL;
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

/* ------------------------------------------------------------ DirectDraw */

static HRESULT WINAPI hk_CreateSurface(IDirectDraw *dd, LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE *out, IUnknown *o)
{
    HRESULT hr = real_CreateSurface(dd, d, out, o);
    if (FAILED(hr) || !out || !*out)
        return hr;
    if (g_cfg.ui_debug)
        sclan_log("ui: CreateSurface flags=%lx caps=%lx %lux%lu -> %p",
                  (unsigned long)d->dwFlags, (unsigned long)d->ddsCaps.dwCaps,
                  (unsigned long)d->dwWidth, (unsigned long)d->dwHeight, *out);
    EnterCriticalSection(&g_cs);
    g_dd = dd;
    if (d->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {
        g_primary = *out;
        g_back = NULL;
        if (d->ddsCaps.dwCaps & DDSCAPS_FLIP) {
            DDSCAPS c = { DDSCAPS_BACKBUFFER };
            IDirectDrawSurface *bb = NULL;
            if (SUCCEEDED((*out)->lpVtbl->GetAttachedSurface(*out, &c, &bb)) && bb) {
                g_back = bb;
                bb->lpVtbl->Release(bb);    /* the flip chain keeps it alive */
            }
        }
    } else if ((d->dwFlags & DDSD_WIDTH) && (d->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
               && (int)d->dwWidth == g_w && (int)d->dwHeight == g_h) {
        /* screen-sized system memory surface = the game's frame (re-created
           after a resolution change, maybe before the old one is released) */
        if (g_tmp)
            real_Release(g_tmp);
        if (g_shadow)
            real_Release(g_shadow);
        g_tmp = g_shadow = NULL;
        g_frame = *out;
        g_last_scene = -12345;
        g_nlast = 0;
        sclan_log("ui: game frame %dx%d (%s), UI scale %.2f", g_w, g_h,
                  g_fullscreen ? "fullscreen" : "window", g_cfg.ui_scale);
    }
    patch_vtbl(*out, 2, (void *)hk_Release, (void **)&real_Release);
    patch_vtbl(*out, 5, (void *)hk_Blt, (void **)&real_Blt);
    patch_vtbl(*out, 7, (void *)hk_BltFast, (void **)&real_BltFast);
    patch_vtbl(*out, 11, (void *)hk_Flip, (void **)&real_Flip);
    if (g_cfg.ui_debug) {
        patch_vtbl(*out, 17, (void *)hk_GetDC, (void **)&real_GetDC);
        patch_vtbl(*out, 25, (void *)hk_Lock, (void **)&real_Lock);
    }
    LeaveCriticalSection(&g_cs);
    return hr;
}

static HRESULT WINAPI hk_SetDisplayMode(IDirectDraw *dd, DWORD w, DWORD h, DWORD bpp)
{
    HRESULT hr = real_SetDisplayMode(dd, w, h, bpp);
    if (SUCCEEDED(hr)) {
        g_w = (int)w;
        g_h = (int)h;
    }
    return hr;
}

static HRESULT WINAPI hk_SetCooperativeLevel(IDirectDraw *dd, HWND wnd, DWORD fl)
{
    g_fullscreen = (fl & DDSCL_FULLSCREEN) != 0;
    if (!g_fullscreen) {
        RECT rc;
        if (GetClientRect(wnd, &rc)) {
            g_w = rc.right;
            g_h = rc.bottom;
        }
    }
    return real_SetCooperativeLevel(dd, wnd, fl);
}

static HRESULT WINAPI hk_DirectDrawCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer)
{
    HRESULT hr = real_DirectDrawCreate(guid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        g_dd = *out;
        patch_vtbl(*out, 6, (void *)hk_CreateSurface, (void **)&real_CreateSurface);
        patch_vtbl(*out, 20, (void *)hk_SetCooperativeLevel, (void **)&real_SetCooperativeLevel);
        patch_vtbl(*out, 21, (void *)hk_SetDisplayMode, (void **)&real_SetDisplayMode);
    }
    return hr;
}

/* ----------------------------------------------------------------- mouse */

static int active(void)
{
    return g_frame != NULL && g_cfg.ui_scale > 1.0;
}

static BOOL WINAPI hk_GetCursorPos(LPPOINT p)
{
    BOOL ok = real_GetCursorPos(p);
    if (ok && p && active() && g_wnd) {
        POINT c = *p;
        ScreenToClient(g_wnd, &c);
        if (map_point(&c.x, &c.y)) {
            ClientToScreen(g_wnd, &c);
            *p = c;
        }
    }
    return ok;
}

static LRESULT CALLBACK hk_WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_wnd)
        g_wnd = w;
    /* client coordinates in lParam (the wheel messages carry screen coordinates) */
    if (active() && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != 0x020E) {
        LONG x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (map_point(&x, &y))
            lp = MAKELPARAM((WORD)(short)x, (WORD)(short)y);
    }
    return CallWindowProcA(real_WndProc, w, msg, wp, lp);
}

static ATOM WINAPI hk_RegisterClassA(const WNDCLASSA *wc)
{
    /* the game registers exactly one window class - wrap its window procedure */
    if (wc && wc->lpfnWndProc && !real_WndProc) {
        WNDCLASSA c = *wc;
        real_WndProc = wc->lpfnWndProc;
        c.lpfnWndProc = hk_WndProc;
        sclan_log("ui: window class '%s' hooked", HIWORD(wc->lpszClassName) ? wc->lpszClassName : "#");
        return real_RegisterClassA(&c);
    }
    return real_RegisterClassA(wc);
}

/* ------------------------------------------------------------- debug dump */

static DWORD WINAPI dump_thread(LPVOID unused)
{
    char req[MAX_PATH], out[MAX_PATH];
    int n = 0, i;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    (void)unused;
    snprintf(req, sizeof req, "%s\\sclan_dump.req", g_moddir);
    for (;;) {
        Sleep(300);
        if (GetFileAttributesA(req) == INVALID_FILE_ATTRIBUTES)
            continue;
        DeleteFileA(req);
        for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            FILE *f;
            if (memcmp(sec[i].Name, ".data", 5))
                continue;
            snprintf(out, sizeof out, "%s\\sclan_dump_%d.bin", g_moddir, n++);
            if ((f = fopen(out, "wb"))) {
                fwrite(base + sec[i].VirtualAddress, 1, sec[i].Misc.VirtualSize, f);
                fclose(f);
            }
            sclan_log("ui: dumped .data -> %s (scene %d)", out, read_scene());
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- setup */

void sclan_ui_install(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    HMODULE dd = GetModuleHandleA("ddraw.dll");
    HMODULE u32 = GetModuleHandleA("user32.dll");
    int n;

    InitializeCriticalSection(&g_cs);
    if (g_cfg.ui_debug)
        CreateThread(NULL, 0, dump_thread, NULL, 0, NULL);
    find_scene_var();

    if (!dd)
        dd = LoadLibraryA("ddraw.dll");
    real_DirectDrawCreate = (dd_create_fn)GetProcAddress(dd, "DirectDrawCreate");
    n = sclan_patch_iat(exe, (void *)real_DirectDrawCreate, (void *)hk_DirectDrawCreate);
    if (g_cfg.ui_scale > 1.0) {
        real_GetCursorPos = (gcp_fn)GetProcAddress(u32, "GetCursorPos");
        real_RegisterClassA = (rca_fn)GetProcAddress(u32, "RegisterClassA");
        n += sclan_patch_iat(exe, (void *)real_GetCursorPos, (void *)hk_GetCursorPos);
        n += sclan_patch_iat(exe, (void *)real_RegisterClassA, (void *)hk_RegisterClassA);
    }
    sclan_log("ui: UI scale %.2f, %d hooks installed", g_cfg.ui_scale, n);
}

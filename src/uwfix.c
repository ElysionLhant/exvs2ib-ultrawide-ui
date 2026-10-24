// uwfix.c — EXVS2IB ultrawide UI-centering fix
// Hooks lumen quad/triangle submission and pre-compresses only UI X coordinates
// into a centered 16:9 region of the fixed 1920x1080 render buffer. Both pixel
// and NDC vertex formats are handled; the 3D pipeline is left unchanged.
// ViewportPlug and viewport_apply hooks remain available for diagnostics.
//
// Build variants:
//   - injected as uwfix.dll during development
//   - renamed to nbamUsbFinder.dll in the game dir for permanent install
//     (exports forward to nbamUsbFinder_orig.dll via the .def file)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define LOG_PATH "uwfix_log.txt"   // relative to game exe working dir
#define INI_PATH "uwfix.ini"

// ViewportPlug factory: CreateViewportPlug(out, name, rect, graphlist)
//   rcx=out  rdx=name?  r8=string  r9=rect {vftbl@0, x@8, y@0xc, w@0x10, h@0x14, minD@0x18, maxD@0x1c}
#define STEAL_LEN        5      // first instruction is exactly 5 bytes: mov [rsp+8],rbx

static unsigned char g_stolen[STEAL_LEN];
static unsigned char *g_factory = NULL;
static void *g_tramp = NULL;
static void log_line(const char *fmt, ...);
void *g_uwfix_tramp_slot = NULL;   // read by the asm stub (factory)
void *g_vp_tramp_slot = NULL;      // read by the asm stub (viewport_apply)

static int do_hook(unsigned char *target, int steal,
                   void *entry_stub, void **tramp_slot, unsigned char *stolen_save) {
    // stolen_save: original bytes saved; trampoline = stolen + movabs rax,target+steal; jmp rax
    memcpy(stolen_save, target, steal);
    void *tramp = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return 0;
    unsigned char *p = (unsigned char *)tramp;
    memcpy(p, stolen_save, steal); p += steal;
    *p++ = 0x48; *p++ = 0xB8;
    *(void **)p = target + steal; p += 8;
    *p++ = 0xFF; *p++ = 0xE0;
    *tramp_slot = tramp;
    unsigned char *cave = NULL;
    uintptr_t base_a = (uintptr_t)target;
    for (long off = 0x10000; off < 0x40000000 && !cave; off += 0x10000) {
        cave = (unsigned char *)VirtualAlloc((void *)(base_a - off), 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!cave) cave = (unsigned char *)VirtualAlloc((void *)(base_a + off), 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    if (!cave) return 0;
    unsigned char *c = cave;
    *c++ = 0x48; *c++ = 0xB8;
    *(void **)c = entry_stub; c += 8;
    *c++ = 0xFF; *c++ = 0xE0;
    long long rel = (long long)cave - (base_a + 5);
    if (rel < -2147483648LL || rel > 2147483647LL) return 0;
    DWORD oldprot;
    if (!VirtualProtect(target, steal, PAGE_EXECUTE_READWRITE, &oldprot)) return 0;
    target[0] = 0xE9;
    *(int *)(target + 1) = (int)rel;
    for (int i = 5; i < steal; i++) target[i] = 0x90;
    VirtualProtect(target, steal, oldprot, &oldprot);
    FlushInstructionCache(GetCurrentProcess(), target, steal);
    return 1;
}

// ---- nbamUsbFinder.dll forwarding (used when this file is installed as the proxy) ----
void *g_fwd_table[6] = {0, 0, 0, 0, 0, 0};
static const char *const g_fwd_names[6] = {
    "nbamUsbFinderGetSerialNumber", "nbamUsbFinderInitialize", "nbamUsbFinderRead",
    "nbamUsbFinderRelease", "nbamUsbFinderWrite", "nbamUsbFinder_GetInformation"
};
// generic "return 0" fallback for unresolved forwards
static void fwd_missing(void) { }

static void resolve_forwards(void) {
    HMODULE orig = GetModuleHandleA("nbamUsbFinder_orig.dll");
    if (!orig) orig = LoadLibraryA("nbamUsbFinder_orig.dll");
    for (int i = 0; i < 6; i++) {
        void *p = orig ? (void *)GetProcAddress(orig, g_fwd_names[i]) : NULL;
        g_fwd_table[i] = p ? p : (void *)&fwd_missing;
    }
}

static unsigned long long g_exe_base = 0;

typedef struct {
    const char *name;
    unsigned long long manager_rva, factory_rva, vpapply_rva;
    unsigned long long pixel_quad_rva, pixel_tri_rva, ndc_quad_rva, ndc_tri_rva, direct_cmd_rva;
    unsigned long long lumen_min_rva, lumen_max_rva;
    unsigned long long extra_ui_rvas[3];
    unsigned long long direct_rvas[2];
} LumenProfile;

static const LumenProfile g_profiles[] = {
    {"ib1.0.4-legacy", 0x27dcba0, 0x120800, 0x872f0,
     0x3028b0, 0x303ce0, 0x15c170, 0x303b50, 0x1560f0,
     0x302000, 0x307000, {0x445c3a, 0x76007a, 0x760a37}, {0x891f6e, 0xa23d8c}},
    {"ib1.0.4-hotupdate-20261024", 0x287f540, 0, 0,
     0x306220, 0x307650, 0x15fa70, 0x3074c0, 0x1599f0,
     0x305970, 0x30a970, {0x44feca, 0x77144a, 0x771e07}, {0x8a8b4e, 0xa4c78c}}
};
static const LumenProfile *g_profile = NULL;

static int lumen_prologue_at(unsigned long long rva) {
    static const unsigned char expected[13] = {
        0x48, 0x89, 0x5c, 0x24, 0x18, 0x57,
        0x48, 0x81, 0xec, 0x30, 0x01, 0x00, 0x00
    };
    return g_exe_base && memcmp((const void *)(g_exe_base + rva), expected, 13) == 0;
}

static void detect_profile(void) {
    g_profile = NULL;
    for (int i = 0; i < (int)(sizeof(g_profiles) / sizeof(g_profiles[0])); i++) {
        if (lumen_prologue_at(g_profiles[i].pixel_quad_rva)) {
            g_profile = &g_profiles[i];
            return;
        }
    }
}

static volatile long g_log_idx = 0;

// ---- config (re-read periodically by the worker thread) ----
static int  g_enabled   = 1;
static int  g_log_all   = 1;
static int  g_hook_vpapply = 0;   // also hook viewport_apply (RVA 0x872f0) and log every programmed viewport
static double g_aspect  = 16.0 / 9.0;
#define MAX_TARGET_RVAS 32
static unsigned long long g_targets[MAX_TARGET_RVAS];
static int g_num_targets = 0;

#pragma pack(push, 1)
typedef struct {
    unsigned long long rva;
    float x, y, w, h, minD, maxD;
    int patched;
} LogEntry;
#pragma pack(pop)
static LogEntry g_log[2048];

static void log_line(const char *fmt, ...) {
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

static int is_target(unsigned long long rva) {
    for (int i = 0; i < g_num_targets; i++)
        if (g_targets[i] == rva) return 1;
    return 0;
}

// called from the asm stub: rcx,rdx,r8,r9 are the factory args, retaddr is [rsp] at entry
void uwfix_on_factory(void *rcx_, void *rdx_, void *r8_, float *r9_rect, void *retaddr) {
    if (!r9_rect) return;
    unsigned long long rva = (unsigned long long)retaddr - g_exe_base;

    float x = 0, y = 0, w = 0, h = 0, minD = 0, maxD = 0;
    __try {
        // r9 points at { vftable(8), x, y, w, h, minD, maxD }
        const float *f = (const float *)((const unsigned char *)r9_rect + 8);
        x = f[0]; y = f[1]; w = f[2]; h = f[3]; minD = f[4]; maxD = f[5];
    } __except (1) { return; }

    long i = InterlockedIncrement(&g_log_idx) - 1;
    int patched = 0;

    if (g_enabled && is_target(rva) && w > 1.0f && h > 1.0f) {
        double cur = (double)w / (double)h;
        if (cur > g_aspect * 1.001) {           // ultrawide only
            double neww = (double)h * g_aspect;
            double newx = (double)x + ((double)w - neww) * 0.5;
            __try {
                float *f = (float *)((unsigned char *)r9_rect + 8);
                f[0] = (float)newx;
                f[2] = (float)neww;
                patched = 1;
            } __except (1) { }
        }
    }

    if (g_log_all && i >= 0 && i < 2048) {
        g_log[i].rva = rva;
        g_log[i].x = x; g_log[i].y = y; g_log[i].w = w; g_log[i].h = h;
        g_log[i].minD = minD; g_log[i].maxD = maxD;
        g_log[i].patched = patched;
    }
    if (patched) {
        log_line("PATCHED rva=%#llx rect=(%.0f,%.0f %.0fx%.0f) -> x=%.0f w=%.0f\n",
                 rva, x, y, w, h, x + (w - h * (float)g_aspect) * 0.5f, h * (float)g_aspect);
    }
}

static void flush_log(void) {
    long n = g_log_idx;
    if (n == 0) return;
    if (n > 2048) n = 2048;
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    for (long i = 0; i < n; i++) {
        LogEntry *e = &g_log[i];
        fprintf(f, "factory call rva=%#llx rect=(%.0f,%.0f %.0fx%.0f d %.2f..%.2f)%s\n",
                e->rva, e->x, e->y, e->w, e->h, e->minD, e->maxD,
                e->patched ? " [PATCHED]" : "");
    }
    fclose(f);
    g_log_idx = 0;
}

// ---- second hook: viewport_apply(rcx=cmdlist_state, edx=slot, r8=rect) ----
#define VP_STEAL_LEN 11   // sub rsp,0x18 (4) + mov r9,[rcx+0xb8] (7)
static unsigned char g_vp_stolen[VP_STEAL_LEN];
static unsigned char *g_vpapply = NULL;
static volatile long g_vp_idx = 0;
#pragma pack(push, 1)
typedef struct { unsigned int slot; float x, y, w, h, minD, maxD; } VpEntry;
#pragma pack(pop)
static VpEntry g_vp_log[2048];

// called from vp stub: rcx=state, rdx=slot, r8=rect, r9=retaddr
void uwfix_on_vpapply(void *rcx_, unsigned int slot, float *rect, void *retaddr) {
    float x=0,y=0,w=0,h=0,a=0,b=0;
    __try {
        const float *f = (const float *)((const unsigned char *)rect + 8);
        x=f[0]; y=f[1]; w=f[2]; h=f[3]; a=f[4]; b=f[5];
    } __except (1) { return; }
    long i = InterlockedIncrement(&g_vp_idx) - 1;
    if (i < 0 || i >= 2048) return;
    g_vp_log[i].slot = slot;
    g_vp_log[i].x=x; g_vp_log[i].y=y; g_vp_log[i].w=w; g_vp_log[i].h=h;
    g_vp_log[i].minD=a; g_vp_log[i].maxD=b;
}

static void flush_vp_log(void) {
    long n = g_vp_idx;
    if (n == 0) return;
    if (n > 2048) n = 2048;
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    for (long i = 0; i < n; i++) {
        VpEntry *e = &g_vp_log[i];
        fprintf(f, "vpapply slot=%u rect=(%.0f,%.0f %.0fx%.0f d %.2f..%.2f)\n",
                e->slot, e->x, e->y, e->w, e->h, e->minD, e->maxD);
    }
    fclose(f);
    g_vp_idx = 0;
}

// ---- lumen UI vertex hooks: center the 16:9 UI without changing the 3D pass ----
#define LUMEN_STEAL_LEN 13
#define LUMEN_BUFFER_W 1920.0
static unsigned char g_lumen_stolen[LUMEN_STEAL_LEN];
static double g_lumen_xscale = 1.0;
static double g_lumen_xoffset = 0.0;
static volatile long g_lumen_hits[4] = {0, 0, 0, 0};
void *g_lq_tramp_slot = NULL;
void *g_lt_tramp_slot = NULL;
void *g_lnq_tramp_slot = NULL;
void *g_lnt_tramp_slot = NULL;
void *g_l2d_tramp_slot = NULL;
static unsigned char g_l2d_stolen[5];

static void update_lumen_transform(void) {
    double scale = 1.0, offset = 0.0;
    int ww = 0, wh = 0;
    __try {
        unsigned long long mgr = g_profile ? *(unsigned long long *)(g_exe_base + g_profile->manager_rva) : 0;
        HWND hwnd = mgr ? *(HWND *)(mgr + 0x18) : NULL;
        RECT rc;
        if (hwnd && GetClientRect(hwnd, &rc)) {
            ww = rc.right - rc.left;
            wh = rc.bottom - rc.top;
        }
    } __except (1) { }
    if (g_enabled && ww > 0 && wh > 0) {
        double current = (double)ww / (double)wh;
        if (current > g_aspect * 1.001) {
            scale = g_aspect / current;
            offset = (LUMEN_BUFFER_W - LUMEN_BUFFER_W * scale) * 0.5;
        }
    }
    if (scale != g_lumen_xscale || offset != g_lumen_xoffset) {
        log_line("lumen transform: window=%dx%d scale=%.6f offset=%.3f enabled=%d\n",
                 ww, wh, scale, offset, g_enabled);
    }
    g_lumen_xscale = scale;
    g_lumen_xoffset = offset;
}

// kind: 0=pixel quad, 1=pixel triangle, 2=NDC quad, 3=NDC triangle, 4=direct 2D quad
void uwfix_on_lumen(void *rcx_, unsigned char *vertices, void *retaddr, int kind) {
    static volatile long direct_hits = 0;
    (void)rcx_;
    if (!vertices || kind < 0 || kind > 4 || !g_profile) return;
    unsigned long long rva = (unsigned long long)retaddr - g_exe_base;
    int extra_ui = rva == g_profile->extra_ui_rvas[0] ||
                   rva == g_profile->extra_ui_rvas[1] || rva == g_profile->extra_ui_rvas[2];
    if (kind == 2 && (rva < g_profile->lumen_min_rva || rva >= g_profile->lumen_max_rva) && !extra_ui)
        return;
    if (kind == 4 && rva != g_profile->direct_rvas[0] && rva != g_profile->direct_rvas[1]) return;

    long hit = kind == 4 ? InterlockedIncrement(&direct_hits) : InterlockedIncrement(&g_lumen_hits[kind]);
    double scale = g_lumen_xscale;
    double offset = kind < 2 ? g_lumen_xoffset : 0.0;
    __try {
        if (hit == 1) {
            log_line("lumen hit kind=%d rva=%#llx x0=%.3f scale=%.6f offset=%.3f\n",
                     kind, rva, *(float *)vertices, scale, offset);
        }
        if (!g_enabled || scale >= 0.999999) return;
        int count = (kind & 1) ? 3 : 4;
        int stride = kind < 2 ? 12 : 8;
        for (int i = 0; i < count; i++) {
            float *x = (float *)(vertices + i * stride);
            *x = (float)(offset + (double)*x * scale);
        }
    } __except (1) { }
}

static int install_lumen_one(unsigned long long rva, void *entry_stub, void **tramp_slot) {
    unsigned char *target = (unsigned char *)(g_exe_base + rva);
    if (!lumen_prologue_at(rva)) {
        log_line("uwfix: lumen prologue mismatch at %#llx, hook skipped\n", rva);
        return 0;
    }
    return do_hook(target, LUMEN_STEAL_LEN, entry_stub, tramp_slot, g_lumen_stolen);
}

static int install_lumen_hooks(void) {
    static const unsigned char expected_2d[5] = {0x44, 0x89, 0x44, 0x24, 0x18};
    extern void uwfix_lumen_quad_entry(void);
    extern void uwfix_lumen_tri_entry(void);
    extern void uwfix_lumen_ndc_quad_entry(void);
    extern void uwfix_lumen_ndc_tri_entry(void);
    extern void uwfix_lumen_2dcmd_entry(void);
    if (!g_profile) {
        log_line("uwfix: unsupported executable, lumen hooks disabled\n");
        return 0;
    }
    int mask = 0;
    if (install_lumen_one(g_profile->pixel_quad_rva, &uwfix_lumen_quad_entry, &g_lq_tramp_slot)) mask |= 1;
    if (install_lumen_one(g_profile->pixel_tri_rva, &uwfix_lumen_tri_entry, &g_lt_tramp_slot)) mask |= 2;
    if (install_lumen_one(g_profile->ndc_quad_rva, &uwfix_lumen_ndc_quad_entry, &g_lnq_tramp_slot)) mask |= 4;
    if (install_lumen_one(g_profile->ndc_tri_rva, &uwfix_lumen_ndc_tri_entry, &g_lnt_tramp_slot)) mask |= 8;
    unsigned char *cmd_target = (unsigned char *)(g_exe_base + g_profile->direct_cmd_rva);
    if (memcmp(cmd_target, expected_2d, 5) == 0) {
        if (do_hook(cmd_target, 5, &uwfix_lumen_2dcmd_entry, &g_l2d_tramp_slot, g_l2d_stolen))
            mask |= 0x10;
    } else {
        log_line("uwfix: direct 2D prologue mismatch, hook skipped\n");
    }
    log_line("uwfix: lumen hooks mask=%#x\n", mask);
    return mask == 0x1f;
}

static void trim(char **p) {
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    *p = s;
}

static void read_ini(void) {
    FILE *f = fopen(INI_PATH, "r");
    if (!f) return;
    char line[512];
    unsigned long long new_targets[MAX_TARGET_RVAS];
    int ntargets = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        trim(&p);
        if (*p == ';' || *p == '#' || *p == 0 || *p == '\n') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p, *val = eq + 1;
        trim(&key); trim(&val);
        // strip trailing whitespace/newline from val
        int L = (int)strlen(val);
        while (L > 0 && (val[L-1] == '\n' || val[L-1] == '\r' || val[L-1] == ' ' || val[L-1] == '\t'))
            val[--L] = 0;
        if (_stricmp(key, "enabled") == 0) g_enabled = atoi(val);
        else if (_stricmp(key, "log_all") == 0) g_log_all = atoi(val);
        else if (_stricmp(key, "hook_vpapply") == 0) g_hook_vpapply = atoi(val);
        else if (_stricmp(key, "aspect") == 0) { double a = atof(val); if (a > 0.5 && a < 10.0) g_aspect = a; }
        else if (_stricmp(key, "target_rva") == 0) {
            // comma-separated hex RVAs (of the return address after the call)
            char *tok = strtok(val, ",");
            while (tok && ntargets < MAX_TARGET_RVAS) {
                trim(&tok);
                new_targets[ntargets++] = strtoull(tok, NULL, 0);
                tok = strtok(NULL, ",");
            }
        }
    }
    fclose(f);
    if (ntargets > 0) {
        memcpy(g_targets, new_targets, sizeof(unsigned long long) * ntargets);
        g_num_targets = ntargets;
    }
}

static int install_hook(void) {
    g_exe_base = (unsigned long long)GetModuleHandleA(NULL);
    if (!g_exe_base) return 0;
    detect_profile();
    if (!g_profile) return 0;
    if (!g_profile->factory_rva) {
        g_factory = NULL;
        return 2;
    }
    g_factory = (unsigned char *)(g_exe_base + g_profile->factory_rva);

    // sanity check first bytes: 48 89 5C 24 08 = mov [rsp+8], rbx
    if (g_factory[0] != 0x48 || g_factory[1] != 0x89 || g_factory[2] != 0x5C) {
        log_line("uwfix: factory prologue mismatch (%02x %02x %02x), aborting\n",
                 g_factory[0], g_factory[1], g_factory[2]);
        return 0;
    }
    extern void uwfix_hook_entry(void);
    return do_hook(g_factory, STEAL_LEN, &uwfix_hook_entry, &g_uwfix_tramp_slot, g_stolen);
}

static int install_vpapply_hook(void) {
    extern void uwfix_vp_entry(void);
    if (!g_profile || !g_profile->vpapply_rva) return 0;
    g_vpapply = (unsigned char *)(g_exe_base + g_profile->vpapply_rva);
    return do_hook(g_vpapply, VP_STEAL_LEN, &uwfix_vp_entry, &g_vp_tramp_slot, g_vp_stolen);
}

static DWORD WINAPI worker(LPVOID unused) {
    // default target: VsngFinal0 builder's call site (call @0x14011db89 -> ret 0x14011db8e)
    g_targets[0] = 0x11db8e;
    g_num_targets = 1;
    read_ini();   // ini may override everything
    int ok = install_hook();
    update_lumen_transform();
    int okl = install_lumen_hooks();
    int ok2 = 0;
    if (g_hook_vpapply) ok2 = install_vpapply_hook();
    log_line("uwfix loaded. exe_base=%#llx profile=%s hook=%s lumen=%s vpapply=%s targets=%d enabled=%d aspect=%.4f log_all=%d\n",
             g_exe_base, g_profile ? g_profile->name : "unsupported",
             ok == 2 ? "off" : (ok == 1 ? "OK" : "FAILED"), okl ? "OK" : "PARTIAL",
             g_hook_vpapply ? (ok2 ? "OK" : "FAILED") : "off",
             g_num_targets, g_enabled, g_aspect, g_log_all);
    for (;;) {
        Sleep(1500);
        flush_log();
        flush_vp_log();
        read_ini();
        update_lumen_transform();
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        resolve_forwards();   // no-op failure if nbamUsbFinder_orig.dll absent (dev injection)
        CreateThread(NULL, 0, worker, NULL, 0, NULL);
    }
    return TRUE;
}

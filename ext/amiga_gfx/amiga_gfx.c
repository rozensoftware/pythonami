/* 2026 by Piotr Rozentreter (Rozsoft) */

/*
 * amiga_gfx.py68k — LoadSeg OS graphics + input for pythonami.
 * Custom screens or Workbench windows; draw/blit on RastPort; SimpleSprites;
 * keyboard/mouse via IDCMP; joystick via lowlevel.library ReadJoyPort.
 * Contract: docs/amiga-os-gfx.md / HAS AMIGA_OS_GFX_API.md
 *
 * Do not link startup.o / vc.lib / amiga.lib / pythonami (D-0027).
 */

#include "py68k_ext.h"

typedef struct Py68Runtime Py68Runtime;

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/gfx.h>
#include <graphics/sprite.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <libraries/lowlevel.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/lowlevel.h>

#define GFX_LIB_VERSION 37
#define GFX_MAX_BITMAPS 8
#define GFX_MAX_SPRITES 8
#define GFX_MAX_BOBS 8

#define GFX_EVT_NONE 0
#define GFX_EVT_CLOSE 1

#define GFX_IDCMP (IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | \
                   IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE)

struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LowLevelBase;

static struct Library *s_intuition;
static struct Library *s_graphics;
static struct Library *s_lowlevel;
static int s_inited;
static int s_owns_screen;
static struct Screen *s_screen;
static struct Window *s_window;
static struct RastPort *s_rp;
static struct ViewPort *s_vp;
static struct BitMap *s_bitmaps[GFX_MAX_BITMAPS];
static LONG s_bm_w[GFX_MAX_BITMAPS];
static LONG s_bm_h[GFX_MAX_BITMAPS];
static LONG s_bm_depth[GFX_MAX_BITMAPS];
static struct SimpleSprite s_sprites[GFX_MAX_SPRITES];
static UWORD *s_sprite_owned[GFX_MAX_SPRITES];
static int s_sprite_used[GFX_MAX_SPRITES];

/* Soft BOBs: optional background save/restore when moving. */
typedef struct GfxBob {
    int used;
    int save_bg;       /* 1 = remember bg under bob and paste back on move */
    int visible;
    int img_slot;      /* index into s_bitmaps[] */
    LONG w;
    LONG h;
    LONG x;
    LONG y;
    struct BitMap *back; /* allocated when save_bg */
    LONG back_depth;
} GfxBob;

static GfxBob s_bobs[GFX_MAX_BOBS];

static char s_title[96];
static char s_default_title[] = "pythonami gfx";

/* Input snapshot (updated by drain_idcmp). */
static LONG s_key;
static LONG s_rawkey;
static WORD s_mx;
static WORD s_my;
static ULONG s_mbuttons;
static int s_close_pending;

static void gfx_memset(void *dst, int value, ULONG n)
{
    UBYTE *p = (UBYTE *)dst;
    while (n != 0) {
        *p++ = (UBYTE)value;
        --n;
    }
}

static ULONG gfx_strlen(const char *s)
{
    ULONG n = 0;
    if (s == 0)
        return 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

static int require_int(Py68Value v, Py68I32 *out)
{
    if (v.type != PY68_VALUE_INT)
        return 0;
    *out = v.as.integer;
    return 1;
}

static int borrow_cstr(Py68Runtime *runtime, Py68Value v, char *buf, ULONG buflen)
{
    const Py68ExtServices *svc;
    const char *data;
    Py68U32 length;
    ULONG i;
    if (buflen == 0)
        return 0;
    if (v.type == PY68_VALUE_INT) {
        data = (const char *)(ULONG)v.as.integer;
        if (data == 0) {
            buf[0] = '\0';
            return 1;
        }
        i = 0;
        while (i + 1 < buflen && data[i] != '\0') {
            buf[i] = data[i];
            ++i;
        }
        buf[i] = '\0';
        return 1;
    }
    svc = py68_ext_services(runtime);
    if (svc == 0 || svc->string_borrow == 0)
        return 0;
    if (!svc->string_borrow(runtime, v, &data, &length))
        return 0;
    if (length + 1 > buflen)
        length = buflen - 1;
    for (i = 0; i < length; ++i)
        buf[i] = data[i];
    buf[length] = '\0';
    return 1;
}

static void reset_input(void)
{
    s_key = 0;
    s_rawkey = 0;
    s_mx = 0;
    s_my = 0;
    s_mbuttons = 0;
    s_close_pending = 0;
}

static void free_bitmap_slot(int slot)
{
    LONG p;
    struct BitMap *bm;
    if (slot < 0 || slot >= GFX_MAX_BITMAPS)
        return;
    bm = s_bitmaps[slot];
    if (bm == 0)
        return;
    for (p = 0; p < s_bm_depth[slot]; ++p) {
        if (bm->Planes[p] != 0)
            FreeRaster(bm->Planes[p], s_bm_w[slot], s_bm_h[slot]);
    }
    FreeMem(bm, (ULONG)sizeof(struct BitMap));
    s_bitmaps[slot] = 0;
    s_bm_w[slot] = 0;
    s_bm_h[slot] = 0;
    s_bm_depth[slot] = 0;
}

static struct BitMap *alloc_bitmap_v37(LONG w, LONG h, LONG depth)
{
    struct BitMap *bm;
    LONG p;
    bm = (struct BitMap *)AllocMem(sizeof(struct BitMap), MEMF_PUBLIC | MEMF_CLEAR);
    if (bm == 0)
        return 0;
    InitBitMap(bm, depth, w, h);
    for (p = 0; p < depth; ++p) {
        bm->Planes[p] = AllocRaster(w, h);
        if (bm->Planes[p] == 0) {
            while (--p >= 0)
                FreeRaster(bm->Planes[p], w, h);
            FreeMem(bm, (ULONG)sizeof(struct BitMap));
            return 0;
        }
    }
    return bm;
}

static void free_all_sprites(void)
{
    int i;
    for (i = 0; i < GFX_MAX_SPRITES; ++i) {
        if (s_sprite_used[i]) {
            FreeSprite((LONG)s_sprites[i].num);
            s_sprite_used[i] = 0;
        }
        if (s_sprite_owned[i] != 0) {
            FreeMem(s_sprite_owned[i], (ULONG)((s_sprites[i].height + 2) * 4));
            s_sprite_owned[i] = 0;
        }
        gfx_memset(&s_sprites[i], 0, sizeof(s_sprites[i]));
    }
}

static void free_all_bitmaps(void)
{
    int i;
    for (i = 0; i < GFX_MAX_BITMAPS; ++i)
        free_bitmap_slot(i);
}

static void free_raw_bitmap(struct BitMap *bm, LONG w, LONG h, LONG depth)
{
    LONG p;
    if (bm == 0)
        return;
    for (p = 0; p < depth; ++p) {
        if (bm->Planes[p] != 0)
            FreeRaster(bm->Planes[p], w, h);
    }
    FreeMem(bm, (ULONG)sizeof(struct BitMap));
}

static void free_bob_slot(int slot)
{
    GfxBob *bob;
    if (slot < 0 || slot >= GFX_MAX_BOBS)
        return;
    bob = &s_bobs[slot];
    if (!bob->used)
        return;
    if (bob->back != 0) {
        free_raw_bitmap(bob->back, bob->w, bob->h, bob->back_depth);
        bob->back = 0;
    }
    gfx_memset(bob, 0, sizeof(*bob));
}

static void free_all_bobs(void)
{
    int i;
    for (i = 0; i < GFX_MAX_BOBS; ++i)
        free_bob_slot(i);
}

static void close_window_safely(void)
{
    struct IntuiMessage *msg;
    if (s_window == 0)
        return;
    if (s_window->UserPort != 0) {
        while ((msg = (struct IntuiMessage *)GetMsg(s_window->UserPort)) != 0)
            ReplyMsg((struct Message *)msg);
        s_window->UserPort = 0;
        ModifyIDCMP(s_window, 0);
    }
    CloseWindow(s_window);
    s_window = 0;
}

static void do_close_display(void)
{
    free_all_sprites();
    free_all_bobs();
    free_all_bitmaps();
    close_window_safely();
    if (s_owns_screen && s_screen != 0)
        CloseScreen(s_screen);
    s_screen = 0;
    s_owns_screen = 0;
    s_rp = 0;
    s_vp = 0;
    reset_input();
}

static int display_ready(void)
{
    return s_inited && s_window != 0 && s_rp != 0 && s_vp != 0;
}

static int display_busy(void)
{
    return s_window != 0;
}

/* Non-blocking IDCMP drain; updates input + close flag. Returns close event. */
static LONG drain_idcmp(void)
{
    struct IntuiMessage *msg;
    ULONG class;
    UWORD code;
    LONG ev = GFX_EVT_NONE;

    if (s_window == 0 || s_window->UserPort == 0)
        return s_close_pending ? GFX_EVT_CLOSE : GFX_EVT_NONE;

    while ((msg = (struct IntuiMessage *)GetMsg(s_window->UserPort)) != 0) {
        class = msg->Class;
        code = msg->Code;
        s_mx = msg->MouseX;
        s_my = msg->MouseY;
        ReplyMsg((struct Message *)msg);

        if (class == IDCMP_CLOSEWINDOW) {
            s_close_pending = 1;
            ev = GFX_EVT_CLOSE;
        } else if (class == IDCMP_VANILLAKEY) {
            s_key = (LONG)code;
            if (code == 27 || code == 'q' || code == 'Q') {
                s_close_pending = 1;
                ev = GFX_EVT_CLOSE;
            }
        } else if (class == IDCMP_RAWKEY) {
            s_rawkey = (LONG)code;
        } else if (class == IDCMP_MOUSEBUTTONS) {
            if (code == SELECTDOWN)
                s_mbuttons |= 1UL;
            else if (code == SELECTUP)
                s_mbuttons &= ~1UL;
            else if (code == MENUDOWN)
                s_mbuttons |= 2UL;
            else if (code == MENUUP)
                s_mbuttons &= ~2UL;
            else if (code == MIDDLEDOWN)
                s_mbuttons |= 4UL;
            else if (code == MIDDLEUP)
                s_mbuttons &= ~4UL;
        } else if (class == IDCMP_MOUSEMOVE) {
            /* s_mx/s_my already updated */
        }
    }
    if (s_close_pending)
        ev = GFX_EVT_CLOSE;
    return ev;
}

static void sync_mouse_from_window(void)
{
    if (s_window != 0) {
        s_mx = s_window->MouseX;
        s_my = s_window->MouseY;
    }
}

static int ensure_lowlevel(void)
{
    if (s_lowlevel != 0)
        return 1;
    s_lowlevel = OpenLibrary((CONST_STRPTR)"lowlevel.library", 40);
    if (s_lowlevel == 0)
        return 0;
    LowLevelBase = s_lowlevel;
    return 1;
}

static Py68Status gfx_init(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    if (s_inited) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    SysBase = *(struct ExecBase **)4L;
    s_intuition = OpenLibrary((CONST_STRPTR)"intuition.library", GFX_LIB_VERSION);
    if (s_intuition == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    IntuitionBase = (struct IntuitionBase *)s_intuition;
    s_graphics = OpenLibrary((CONST_STRPTR)"graphics.library", GFX_LIB_VERSION);
    if (s_graphics == 0) {
        CloseLibrary(s_intuition);
        s_intuition = 0;
        IntuitionBase = 0;
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    GfxBase = (struct GfxBase *)s_graphics;
    /* lowlevel is optional (joy); opened lazily */
    s_inited = 1;
    reset_input();
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gfx_shutdown(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    do_close_display();
    if (s_lowlevel != 0) {
        CloseLibrary(s_lowlevel);
        s_lowlevel = 0;
        LowLevelBase = 0;
    }
    if (s_graphics != 0) {
        CloseLibrary(s_graphics);
        s_graphics = 0;
        GfxBase = 0;
    }
    if (s_intuition != 0) {
        CloseLibrary(s_intuition);
        s_intuition = 0;
        IntuitionBase = 0;
    }
    s_inited = 0;
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_open_screen(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 x, y, w, h, depth, mode;
    struct NewScreen ns;
    struct NewWindow nw;
    (void)runtime;
    (void)argc;
    if (!s_inited || display_busy()) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &x) || !require_int(args[1], &y) ||
        !require_int(args[2], &w) || !require_int(args[3], &h) ||
        !require_int(args[4], &depth) || !require_int(args[5], &mode)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (depth < 1)
        depth = 1;
    if (depth > 5)
        depth = 5;
    gfx_memset(&ns, 0, sizeof(ns));
    ns.LeftEdge = (WORD)x;
    ns.TopEdge = (WORD)y;
    ns.Width = (WORD)w;
    ns.Height = (WORD)h;
    ns.Depth = (WORD)depth;
    ns.DetailPen = 0;
    ns.BlockPen = 1;
    ns.ViewModes = (UWORD)(mode & (HIRES | LACE | SPRITES));
    ns.Type = CUSTOMSCREEN | SCREENQUIET;
    ns.Font = 0;
    ns.DefaultTitle = (STRPTR)s_default_title;
    ns.Gadgets = 0;
    ns.CustomBitMap = 0;
    s_screen = OpenScreen(&ns);
    if (s_screen == 0) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    s_owns_screen = 1;
    s_rp = &s_screen->RastPort;
    s_vp = &s_screen->ViewPort;
    SetDrMd(s_rp, JAM1);
    SetAPen(s_rp, 1);

    gfx_memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 0;
    nw.TopEdge = 0;
    nw.Width = (WORD)w;
    nw.Height = (WORD)h;
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.IDCMPFlags = GFX_IDCMP;
    nw.Flags = WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP | WFLG_BACKDROP |
               WFLG_REPORTMOUSE | WFLG_NOCAREREFRESH;
    nw.FirstGadget = 0;
    nw.CheckMark = 0;
    nw.Title = 0;
    nw.Screen = s_screen;
    nw.BitMap = 0;
    nw.MinWidth = 0;
    nw.MinHeight = 0;
    nw.MaxWidth = 0xFFFF;
    nw.MaxHeight = 0xFFFF;
    nw.Type = CUSTOMSCREEN;
    s_window = OpenWindow(&nw);
    if (s_window == 0) {
        CloseScreen(s_screen);
        s_screen = 0;
        s_owns_screen = 0;
        s_rp = 0;
        s_vp = 0;
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    reset_input();
    *result = py68_ext_value_int((Py68I32)(ULONG)s_screen);
    return PY68_STATUS_OK;
}

/* Workbench (or public) window: draw/blit on Window->RPort. Sprites use screen VP. */
static Py68Status gfx_open_window(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 x, y, w, h;
    struct NewWindow nw;
    (void)argc;
    if (!s_inited || display_busy()) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (!borrow_cstr(runtime, args[0], s_title, sizeof(s_title)) ||
        !require_int(args[1], &x) || !require_int(args[2], &y) ||
        !require_int(args[3], &w) || !require_int(args[4], &h)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (w < 64)
        w = 64;
    if (h < 40)
        h = 40;
    gfx_memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = (WORD)x;
    nw.TopEdge = (WORD)y;
    nw.Width = (WORD)w;
    nw.Height = (WORD)h;
    nw.DetailPen = (UBYTE)0xFF;
    nw.BlockPen = (UBYTE)0xFF;
    nw.IDCMPFlags = GFX_IDCMP;
    nw.Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE |
               WFLG_RMBTRAP | WFLG_REPORTMOUSE | WFLG_NOCAREREFRESH;
    nw.FirstGadget = 0;
    nw.CheckMark = 0;
    nw.Title = (STRPTR)s_title;
    nw.Screen = 0;
    nw.BitMap = 0;
    nw.MinWidth = 64;
    nw.MinHeight = 40;
    nw.MaxWidth = 0xFFFF;
    nw.MaxHeight = 0xFFFF;
    nw.Type = WBENCHSCREEN;
    s_window = OpenWindow(&nw);
    if (s_window == 0) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    s_owns_screen = 0;
    s_screen = s_window->WScreen;
    s_rp = s_window->RPort;
    s_vp = &s_screen->ViewPort;
    SetDrMd(s_rp, JAM1);
    SetAPen(s_rp, 1);
    reset_input();
    *result = py68_ext_value_int((Py68I32)(ULONG)s_window);
    return PY68_STATUS_OK;
}

static Py68Status gfx_close_screen(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    do_close_display();
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_wait_tof(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    if (s_inited)
        WaitTOF();
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_ink(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                          Py68Value *result)
{
    Py68I32 pen;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &pen)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    SetAPen(s_rp, (ULONG)pen);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_color(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                            Py68Value *result)
{
    Py68I32 index, r, g, b;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &index) || !require_int(args[1], &r) ||
        !require_int(args[2], &g) || !require_int(args[3], &b)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (r < 0)
        r = 0;
    if (r > 15)
        r = 15;
    if (g < 0)
        g = 0;
    if (g > 15)
        g = 15;
    if (b < 0)
        b = 0;
    if (b > 15)
        b = 15;
    SetRGB4(s_vp, (LONG)index, (ULONG)r, (ULONG)g, (ULONG)b);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_plot(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 x, y;
    (void)runtime;
    (void)argc;
    if (display_ready() && require_int(args[0], &x) && require_int(args[1], &y))
        WritePixel(s_rp, x, y);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_line(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 x0, y0, x1, y1;
    (void)runtime;
    (void)argc;
    if (display_ready() && require_int(args[0], &x0) && require_int(args[1], &y0) &&
        require_int(args[2], &x1) && require_int(args[3], &y1)) {
        Move(s_rp, x0, y0);
        Draw(s_rp, x1, y1);
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_rect(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 x, y, w, h, filled;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &x) || !require_int(args[1], &y) ||
        !require_int(args[2], &w) || !require_int(args[3], &h) ||
        !require_int(args[4], &filled)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (w <= 0 || h <= 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (filled) {
        RectFill(s_rp, x, y, x + w - 1, y + h - 1);
    } else {
        Move(s_rp, x, y);
        Draw(s_rp, x + w - 1, y);
        Draw(s_rp, x + w - 1, y + h - 1);
        Draw(s_rp, x, y + h - 1);
        Draw(s_rp, x, y);
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static void fill_circle_scan(LONG cx, LONG cy, LONG r)
{
    LONG y, x, rr;
    rr = r * r;
    for (y = -r; y <= r; ++y) {
        x = 0;
        while (x * x + y * y <= rr)
            ++x;
        if (x > 0)
            RectFill(s_rp, cx - (x - 1), cy + y, cx + (x - 1), cy + y);
    }
}

static Py68Status gfx_circle(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                             Py68Value *result)
{
    Py68I32 cx, cy, r, filled;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &cx) || !require_int(args[1], &cy) ||
        !require_int(args[2], &r) || !require_int(args[3], &filled)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (r <= 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (filled)
        fill_circle_scan(cx, cy, r);
    else
        DrawEllipse(s_rp, cx, cy, r, r);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_fill(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 x, y;
    (void)runtime;
    (void)argc;
    if (display_ready() && require_int(args[0], &x) && require_int(args[1], &y))
        Flood(s_rp, 0, x, y);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_text(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 x, y;
    char buf[256];
    (void)argc;
    if (!display_ready() || !require_int(args[0], &x) || !require_int(args[1], &y) ||
        !borrow_cstr(runtime, args[2], buf, sizeof(buf))) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    Move(s_rp, x, y);
    Text(s_rp, (STRPTR)buf, (ULONG)gfx_strlen(buf));
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static int handle_to_slot(Py68I32 handle)
{
    int i;
    if (handle <= 0)
        return -1;
    for (i = 0; i < GFX_MAX_BITMAPS; ++i) {
        if ((Py68I32)(ULONG)s_bitmaps[i] == handle)
            return i;
    }
    return -1;
}

static Py68Status gfx_alloc_bitmap(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 w, h, depth;
    struct BitMap *bm;
    int i;
    (void)runtime;
    (void)argc;
    if (!s_inited || !require_int(args[0], &w) || !require_int(args[1], &h) ||
        !require_int(args[2], &depth)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (w <= 0 || h <= 0) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    if (depth < 1)
        depth = 1;
    if (depth > 8)
        depth = 8;
    for (i = 0; i < GFX_MAX_BITMAPS; ++i) {
        if (s_bitmaps[i] == 0)
            break;
    }
    if (i >= GFX_MAX_BITMAPS) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    bm = alloc_bitmap_v37(w, h, depth);
    if (bm == 0) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    s_bitmaps[i] = bm;
    s_bm_w[i] = w;
    s_bm_h[i] = h;
    s_bm_depth[i] = depth;
    *result = py68_ext_value_int((Py68I32)(ULONG)bm);
    return PY68_STATUS_OK;
}

static Py68Status gfx_free_bitmap(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 handle;
    int slot;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &handle)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    slot = handle_to_slot(handle);
    if (slot >= 0)
        free_bitmap_slot(slot);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_bitmap_clear(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 handle;
    int slot;
    struct RastPort rp;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &handle)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    slot = handle_to_slot(handle);
    if (slot < 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    InitRastPort(&rp);
    rp.BitMap = s_bitmaps[slot];
    SetRast(&rp, 0);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_bitmap_plot(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 handle, x, y, pen;
    int slot;
    struct RastPort rp;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &handle) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &pen)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    slot = handle_to_slot(handle);
    if (slot < 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    InitRastPort(&rp);
    rp.BitMap = s_bitmaps[slot];
    SetAPen(&rp, (ULONG)pen);
    WritePixel(&rp, x, y);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_blit(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    Py68I32 src, sx, sy, w, h, dx, dy;
    int slot;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &src) || !require_int(args[1], &sx) ||
        !require_int(args[2], &sy) || !require_int(args[3], &w) ||
        !require_int(args[4], &h) || !require_int(args[5], &dx) ||
        !require_int(args[6], &dy)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    slot = handle_to_slot(src);
    if (slot < 0 || w <= 0 || h <= 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    BltBitMapRastPort(s_bitmaps[slot], sx, sy, s_rp, dx, dy, w, h, 0xC0);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static struct BitMap *screen_bitmap(void)
{
    if (s_rp == 0)
        return 0;
    return s_rp->BitMap;
}

static void bob_restore(GfxBob *bob)
{
    struct BitMap *scr;
    if (bob == 0 || !bob->visible || !bob->save_bg || bob->back == 0)
        return;
    scr = screen_bitmap();
    if (scr == 0)
        return;
    BltBitMap(bob->back, 0, 0, scr, bob->x, bob->y, bob->w, bob->h, 0xC0, 0xFF, 0);
    bob->visible = 0;
}

static void bob_save_under(GfxBob *bob, LONG x, LONG y)
{
    struct BitMap *scr;
    if (bob == 0 || !bob->save_bg || bob->back == 0)
        return;
    scr = screen_bitmap();
    if (scr == 0)
        return;
    BltBitMap(scr, x, y, bob->back, 0, 0, bob->w, bob->h, 0xC0, 0xFF, 0);
}

static void bob_paint(GfxBob *bob, LONG x, LONG y)
{
    if (bob == 0 || bob->img_slot < 0 || bob->img_slot >= GFX_MAX_BITMAPS)
        return;
    if (s_bitmaps[bob->img_slot] == 0 || s_rp == 0)
        return;
    BltBitMapRastPort(s_bitmaps[bob->img_slot], 0, 0, s_rp, x, y, bob->w, bob->h,
                      0xC0);
}

/*
 * bob_create(bitmap, save_bg) -> slot / -1
 * save_bg != 0: stash screen pixels under the bob and restore them when
 * bob_draw moves or bob_undraw is called. save_bg == 0: paint-only (trail).
 */
static Py68Status gfx_bob_create(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    Py68I32 handle, save_bg;
    int img;
    int i;
    GfxBob *bob;
    LONG depth;
    struct BitMap *scr;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &handle) ||
        !require_int(args[1], &save_bg)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    img = handle_to_slot(handle);
    if (img < 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < GFX_MAX_BOBS; ++i) {
        if (!s_bobs[i].used)
            break;
    }
    if (i >= GFX_MAX_BOBS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    bob = &s_bobs[i];
    gfx_memset(bob, 0, sizeof(*bob));
    bob->used = 1;
    bob->save_bg = save_bg != 0 ? 1 : 0;
    bob->img_slot = img;
    bob->w = s_bm_w[img];
    bob->h = s_bm_h[img];
    bob->visible = 0;
    if (bob->save_bg) {
        scr = screen_bitmap();
        depth = scr != 0 ? (LONG)scr->Depth : s_bm_depth[img];
        if (depth < 1)
            depth = 1;
        bob->back = alloc_bitmap_v37(bob->w, bob->h, depth);
        if (bob->back == 0) {
            gfx_memset(bob, 0, sizeof(*bob));
            *result = py68_ext_value_int(-1);
            return PY68_STATUS_OK;
        }
        bob->back_depth = depth;
    }
    *result = py68_ext_value_int((Py68I32)i);
    return PY68_STATUS_OK;
}

static Py68Status gfx_bob_draw(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    Py68I32 slot, x, y;
    GfxBob *bob;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &slot) || !require_int(args[1], &x) ||
        !require_int(args[2], &y)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (slot < 0 || slot >= GFX_MAX_BOBS || !s_bobs[slot].used) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    bob = &s_bobs[slot];
    if (bob->save_bg) {
        if (bob->visible)
            bob_restore(bob);
        bob_save_under(bob, x, y);
        bob_paint(bob, x, y);
        bob->x = x;
        bob->y = y;
        bob->visible = 1;
    } else {
        bob_paint(bob, x, y);
        bob->x = x;
        bob->y = y;
        bob->visible = 1;
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_bob_undraw(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    Py68I32 slot;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &slot)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (slot >= 0 && slot < GFX_MAX_BOBS && s_bobs[slot].used) {
        if (s_bobs[slot].save_bg)
            bob_restore(&s_bobs[slot]);
        else
            s_bobs[slot].visible = 0;
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_bob_free(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    Py68I32 slot;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &slot)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (slot >= 0 && slot < GFX_MAX_BOBS && s_bobs[slot].used) {
        if (display_ready() && s_bobs[slot].save_bg)
            bob_restore(&s_bobs[slot]);
        free_bob_slot((int)slot);
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static int alloc_sprite_slot(void)
{
    int i;
    for (i = 0; i < GFX_MAX_SPRITES; ++i) {
        if (!s_sprite_used[i])
            return i;
    }
    return -1;
}

static int install_sprite_data(int slot, UWORD *chip, LONG height)
{
    if (slot < 0 || slot >= GFX_MAX_SPRITES || !s_sprite_used[slot] || height <= 0 ||
        chip == 0 || s_vp == 0)
        return -1;
    if (s_sprite_owned[slot] != 0) {
        FreeMem(s_sprite_owned[slot], (ULONG)((s_sprites[slot].height + 2) * 4));
        s_sprite_owned[slot] = 0;
    }
    s_sprite_owned[slot] = chip;
    s_sprites[slot].height = (UWORD)height;
    s_sprites[slot].posctldata = chip;
    ChangeSprite(s_vp, &s_sprites[slot], chip);
    return 0;
}

static Py68Status gfx_sprite_get(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    Py68I32 prefer;
    WORD num;
    WORD pick;
    int i;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &prefer)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    i = alloc_sprite_slot();
    if (i < 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    gfx_memset(&s_sprites[i], 0, sizeof(s_sprites[i]));
    s_sprites[i].height = 1;
    /*
     * pick -1 = next free hardware sprite. Prefer 0..7 may fail when that
     * slot is held (Intuition pointer almost always owns sprite 0).
     * Fall back to -1 so callers that pass 0..7 still work when possible.
     */
    if (prefer < 0 || prefer > 7)
        pick = (WORD)-1;
    else
        pick = (WORD)prefer;
    num = GetSprite(&s_sprites[i], pick);
    if (num < 0 && pick != (WORD)-1)
        num = GetSprite(&s_sprites[i], (WORD)-1);
    if (num < 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    s_sprite_used[i] = 1;
    *result = py68_ext_value_int((Py68I32)i);
    return PY68_STATUS_OK;
}

static Py68Status gfx_sprite_data(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 slot, data, height;
    UWORD *src;
    ULONG bytes;
    UWORD *chip;
    ULONG i;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &slot) || !require_int(args[1], &data) ||
        !require_int(args[2], &height)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (slot < 0 || slot >= GFX_MAX_SPRITES || !s_sprite_used[slot] || height <= 0 ||
        data == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    src = (UWORD *)(ULONG)data;
    bytes = (ULONG)((height + 2) * 4);
    chip = (UWORD *)AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);
    if (chip == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < bytes; ++i)
        ((UBYTE *)chip)[i] = ((UBYTE *)src)[i];
    if (install_sprite_data((int)slot, chip, height) != 0) {
        FreeMem(chip, bytes);
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

/* Build a solid 16xH SimpleSprite in chip (pattern 1/2/3 = colors). */
static Py68Status gfx_sprite_box(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    Py68I32 slot, height, pattern;
    UWORD *chip;
    ULONG bytes;
    LONG row;
    UWORD p0, p1;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &slot) || !require_int(args[1], &height) ||
        !require_int(args[2], &pattern)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (slot < 0 || slot >= GFX_MAX_SPRITES || !s_sprite_used[slot] || height <= 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (pattern < 1)
        pattern = 1;
    if (pattern > 3)
        pattern = 3;
    p0 = (pattern & 1) ? 0xFFFF : 0;
    p1 = (pattern & 2) ? 0xFFFF : 0;
    bytes = (ULONG)((height + 2) * 4);
    chip = (UWORD *)AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);
    if (chip == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    /* control + rows + footer (footer already clear) */
    for (row = 0; row < height; ++row) {
        chip[2 + row * 2] = p0;
        chip[3 + row * 2] = p1;
    }
    if (install_sprite_data((int)slot, chip, height) != 0) {
        FreeMem(chip, bytes);
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gfx_sprite_move(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 slot, x, y;
    (void)runtime;
    (void)argc;
    if (!display_ready() || !require_int(args[0], &slot) || !require_int(args[1], &x) ||
        !require_int(args[2], &y)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (slot >= 0 && slot < GFX_MAX_SPRITES && s_sprite_used[slot])
        MoveSprite(s_vp, &s_sprites[slot], x, y);
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_sprite_free(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 slot;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &slot)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_OK;
    }
    if (slot >= 0 && slot < GFX_MAX_SPRITES && s_sprite_used[slot]) {
        FreeSprite((LONG)s_sprites[slot].num);
        s_sprite_used[slot] = 0;
        if (s_sprite_owned[slot] != 0) {
            FreeMem(s_sprite_owned[slot], (ULONG)((s_sprites[slot].height + 2) * 4));
            s_sprite_owned[slot] = 0;
        }
        gfx_memset(&s_sprites[slot], 0, sizeof(s_sprites[slot]));
    }
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gfx_poll(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    LONG ev;
    (void)runtime;
    (void)argc;
    (void)args;
    sync_mouse_from_window();
    ev = drain_idcmp();
    *result = py68_ext_value_int(ev);
    return PY68_STATUS_OK;
}

static Py68Status gfx_wait_event(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    ULONG sigmask;
    LONG ev;
    (void)runtime;
    (void)argc;
    (void)args;
    ev = GFX_EVT_NONE;
    if (s_window == 0 || s_window->UserPort == 0) {
        *result = py68_ext_value_int(ev);
        return PY68_STATUS_OK;
    }
    if (s_close_pending) {
        *result = py68_ext_value_int(GFX_EVT_CLOSE);
        return PY68_STATUS_OK;
    }
    sigmask = 1UL << s_window->UserPort->mp_SigBit;
    Wait(sigmask);
    sync_mouse_from_window();
    ev = drain_idcmp();
    *result = py68_ext_value_int(ev);
    return PY68_STATUS_OK;
}

static Py68Status gfx_key(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                          Py68Value *result)
{
    LONG k;
    (void)runtime;
    (void)argc;
    (void)args;
    drain_idcmp();
    k = s_key;
    s_key = 0;
    *result = py68_ext_value_int(k);
    return PY68_STATUS_OK;
}

static Py68Status gfx_rawkey(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                             Py68Value *result)
{
    LONG k;
    (void)runtime;
    (void)argc;
    (void)args;
    drain_idcmp();
    k = s_rawkey;
    s_rawkey = 0;
    *result = py68_ext_value_int(k);
    return PY68_STATUS_OK;
}

static Py68Status gfx_mouse_x(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                              Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    drain_idcmp();
    sync_mouse_from_window();
    *result = py68_ext_value_int((Py68I32)s_mx);
    return PY68_STATUS_OK;
}

static Py68Status gfx_mouse_y(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                              Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    drain_idcmp();
    sync_mouse_from_window();
    *result = py68_ext_value_int((Py68I32)s_my);
    return PY68_STATUS_OK;
}

static Py68Status gfx_mouse_buttons(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                    Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    drain_idcmp();
    *result = py68_ext_value_int((Py68I32)s_mbuttons);
    return PY68_STATUS_OK;
}

static ULONG read_joy(ULONG port)
{
    if (!ensure_lowlevel())
        return 0;
    return ReadJoyPort(port);
}

static Py68Status gfx_joy(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                          Py68Value *result)
{
    Py68I32 port;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &port)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    *result = py68_ext_value_int((Py68I32)read_joy((ULONG)port));
    return PY68_STATUS_OK;
}

/* Decoded axes for Language Levels without bitwise operators. */
static Py68Status gfx_joy_x(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                            Py68Value *result)
{
    Py68I32 port;
    ULONG v;
    LONG x;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &port)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    v = read_joy((ULONG)port);
    x = 0;
    if ((v & JPF_JOY_LEFT) != 0)
        x = -1;
    if ((v & JPF_JOY_RIGHT) != 0)
        x = 1;
    *result = py68_ext_value_int(x);
    return PY68_STATUS_OK;
}

static Py68Status gfx_joy_y(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                            Py68Value *result)
{
    Py68I32 port;
    ULONG v;
    LONG y;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &port)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    v = read_joy((ULONG)port);
    y = 0;
    if ((v & JPF_JOY_UP) != 0)
        y = -1;
    if ((v & JPF_JOY_DOWN) != 0)
        y = 1;
    *result = py68_ext_value_int(y);
    return PY68_STATUS_OK;
}

static Py68Status gfx_joy_fire(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    Py68I32 port;
    ULONG v;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &port)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    v = read_joy((ULONG)port);
    *result = py68_ext_value_int((v & JPF_BUTTON_RED) != 0 ? 1 : 0);
    return PY68_STATUS_OK;
}

const Py68ExtExport amiga_gfx_exports[39] = {
    { "init", 0, 0, gfx_init },
    { "shutdown", 0, 0, gfx_shutdown },
    { "open_screen", 6, 6, gfx_open_screen },
    { "open_window", 5, 5, gfx_open_window },
    { "close_screen", 0, 0, gfx_close_screen },
    { "wait_tof", 0, 0, gfx_wait_tof },
    { "ink", 1, 1, gfx_ink },
    { "color", 4, 4, gfx_color },
    { "plot", 2, 2, gfx_plot },
    { "line", 4, 4, gfx_line },
    { "rect", 5, 5, gfx_rect },
    { "circle", 4, 4, gfx_circle },
    { "fill", 2, 2, gfx_fill },
    { "text", 3, 3, gfx_text },
    { "alloc_bitmap", 3, 3, gfx_alloc_bitmap },
    { "free_bitmap", 1, 1, gfx_free_bitmap },
    { "bitmap_clear", 1, 1, gfx_bitmap_clear },
    { "bitmap_plot", 4, 4, gfx_bitmap_plot },
    { "blit", 7, 7, gfx_blit },
    { "bob_create", 2, 2, gfx_bob_create },
    { "bob_draw", 3, 3, gfx_bob_draw },
    { "bob_undraw", 1, 1, gfx_bob_undraw },
    { "bob_free", 1, 1, gfx_bob_free },
    { "sprite_get", 1, 1, gfx_sprite_get },
    { "sprite_data", 3, 3, gfx_sprite_data },
    { "sprite_box", 3, 3, gfx_sprite_box },
    { "sprite_move", 3, 3, gfx_sprite_move },
    { "sprite_free", 1, 1, gfx_sprite_free },
    { "poll", 0, 0, gfx_poll },
    { "wait_event", 0, 0, gfx_wait_event },
    { "key", 0, 0, gfx_key },
    { "rawkey", 0, 0, gfx_rawkey },
    { "mouse_x", 0, 0, gfx_mouse_x },
    { "mouse_y", 0, 0, gfx_mouse_y },
    { "mouse_buttons", 0, 0, gfx_mouse_buttons },
    { "joy", 1, 1, gfx_joy },
    { "joy_x", 1, 1, gfx_joy_x },
    { "joy_y", 1, 1, gfx_joy_y },
    { "joy_fire", 1, 1, gfx_joy_fire }
};

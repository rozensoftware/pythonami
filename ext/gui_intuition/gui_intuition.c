/* 2026 by Piotr Rozentreter (Rozsoft) */

/*
 * gui_intuition.py68k — LoadSeg Intuition dialog plugin for pythonami.
 * Layer 1 of docs/GUI_PYTHONAMI_API.md (HAS gui_intuition.s behavioral reference).
 *
 * Do not link startup.o / vc.lib / amiga.lib / pythonami (D-0027).
 * Uses vbcc inline LVO stubs via proto headers + own library bases.
 */

#include "py68k_ext.h"

typedef struct Py68Runtime Py68Runtime;

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

/* No vc.lib — local string helpers only. */
static void gui_memset(void *dst, int value, ULONG n)
{
    UBYTE *p = (UBYTE *)dst;
    while (n != 0) {
        *p++ = (UBYTE)value;
        --n;
    }
}

static void gui_memcpy(void *dst, const void *src, ULONG n)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    while (n != 0) {
        *d++ = *s++;
        --n;
    }
}

static ULONG gui_strlen(const char *s)
{
    ULONG n = 0;
    if (s == 0)
        return 0;
    while (s[n] != '\0')
        ++n;
    return n;
}

static void gui_strcpy(char *dst, const char *src)
{
    while (*src != '\0')
        *dst++ = *src++;
    *dst = '\0';
}

static void gui_strncpy(char *dst, const char *src, ULONG n)
{
    ULONG i = 0;
    if (n == 0)
        return;
    while (i + 1 < n && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void gui_strcat(char *dst, const char *src)
{
    while (*dst != '\0')
        ++dst;
    while (*src != '\0')
        *dst++ = *src++;
    *dst = '\0';
}

#define GUI_LIB_VERSION 37
#define GUI_MAX_GADGETS 32
#define GUI_MAX_LABELS 32
#define GUI_MAX_EDIT_LEN 128
#define GUI_FONT_W 8
#define GUI_FONT_H 8

#define GUI_EVT_NONE 0
#define GUI_EVT_CLOSE 1
#define GUI_EVT_BUTTON 2
#define GUI_EVT_PRESS 3
#define GUI_EVT_STRING 4
#define GUI_EVT_KEY 5
#define GUI_EVT_MOUSE 6
#define GUI_EVT_REFRESH 7
#define GUI_EVT_CHECKBOX 8
#define GUI_EVT_LIST 9

#define GUI_WIDGET_BUTTON 0
#define GUI_WIDGET_CHECKBOX 1
#define GUI_WIDGET_LIST 2
#define GUI_WIDGET_BITMAP 3
#define GUI_WIDGET_EDIT 4
#define GUI_WIDGET_PROGRESS 5

struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static struct Library *s_intuition;
static struct Library *s_graphics;
static int s_inited;
static int s_begun;
static int s_shown;

static struct NewWindow s_nw;
static struct Window *s_window;
static struct MsgPort *s_userport;
static ULONG s_sigmask;
static struct RastPort *s_rport;

static struct Gadget s_gadgets[GUI_MAX_GADGETS];
static struct Border s_borders[GUI_MAX_GADGETS * 2];
static WORD s_bordxy[GUI_MAX_GADGETS * 12];
static struct IntuiText s_gtexts[GUI_MAX_GADGETS];
static struct StringInfo s_sinfos[GUI_MAX_GADGETS];
static char s_edit_buf[GUI_MAX_GADGETS][GUI_MAX_EDIT_LEN];
static char s_edit_undo[GUI_MAX_GADGETS][GUI_MAX_EDIT_LEN];
static UWORD s_widget_kind[GUI_MAX_GADGETS];
static UWORD s_list_selected[GUI_MAX_GADGETS];
static UWORD s_list_rows[GUI_MAX_GADGETS];
static UWORD s_progress_pct[GUI_MAX_GADGETS];
static char s_captions[GUI_MAX_GADGETS][64];
static UWORD s_gadget_count;

static struct IntuiText s_labels[GUI_MAX_LABELS];
static char s_label_text[GUI_MAX_LABELS][64];
static WORD s_label_xy[GUI_MAX_LABELS][2];
static UWORD s_label_ids[GUI_MAX_LABELS];
static UWORD s_label_count;

static char s_title[96];
static LONG s_evt_id;
static LONG s_evt_code;
static WORD s_evt_mx;
static WORD s_evt_my;

static int require_int(Py68Value v, Py68I32 *out)
{
    if (v.type == PY68_VALUE_INT || v.type == PY68_VALUE_BOOL) {
        *out = v.as.integer;
        return 1;
    }
    return 0;
}

static int borrow_cstr(Py68Runtime *runtime, Py68Value v, char *dst, Py68U32 dst_size)
{
    const Py68ExtServices *svc;
    const char *data;
    Py68U32 length;
    Py68U32 copy;
    if (dst == 0 || dst_size == 0)
        return 0;
    dst[0] = '\0';
    svc = py68_ext_services(runtime);
    if (svc == 0 || svc->string_borrow == 0)
        return 0;
    if (!svc->string_borrow(runtime, v, &data, &length))
        return 0;
    copy = length;
    if (copy >= dst_size)
        copy = dst_size - 1;
    if (copy != 0 && data != 0)
        gui_memcpy(dst, data, copy);
    dst[copy] = '\0';
    return 1;
}

static void fill_button_border(UWORD slot, WORD w, WORD h)
{
    /* Match HAS GuiAddButton: two 3-point bevels (pen 2 highlight, pen 1 shadow). */
    WORD *xy1 = &s_bordxy[slot * 12];
    WORD *xy2 = &s_bordxy[slot * 12 + 6];
    struct Border *b1 = &s_borders[slot * 2];
    struct Border *b2 = &s_borders[slot * 2 + 1];
    WORD wm2 = (WORD)(w - 2);
    WORD hm2 = (WORD)(h - 2);
    WORD wm1 = (WORD)(w - 1);
    WORD hm1 = (WORD)(h - 1);

    xy1[0] = 0;
    xy1[1] = hm2;
    xy1[2] = 0;
    xy1[3] = 0;
    xy1[4] = wm2;
    xy1[5] = 0;

    b1->LeftEdge = 0;
    b1->TopEdge = 0;
    b1->FrontPen = 2;
    b1->BackPen = 0;
    b1->DrawMode = JAM1;
    b1->Count = 3;
    b1->XY = xy1;
    b1->NextBorder = b2;

    xy2[0] = wm1;
    xy2[1] = 0;
    xy2[2] = wm1;
    xy2[3] = hm1;
    xy2[4] = 0;
    xy2[5] = hm1;

    b2->LeftEdge = 0;
    b2->TopEdge = 0;
    b2->FrontPen = 1;
    b2->BackPen = 0;
    b2->DrawMode = JAM1;
    b2->Count = 3;
    b2->XY = xy2;
    b2->NextBorder = 0;
}

static void fill_string_border(UWORD slot, WORD outer_w, WORD outer_h)
{
    /* Match HAS GuiAddEditBox: recessed frame at -2 relative to inset text area. */
    WORD *xy1 = &s_bordxy[slot * 12];
    WORD *xy2 = &s_bordxy[slot * 12 + 6];
    struct Border *b1 = &s_borders[slot * 2];
    struct Border *b2 = &s_borders[slot * 2 + 1];
    WORD wm3 = (WORD)(outer_w - 3);
    WORD hm3 = (WORD)(outer_h - 3);

    xy1[0] = -2;
    xy1[1] = hm3;
    xy1[2] = -2;
    xy1[3] = -2;
    xy1[4] = wm3;
    xy1[5] = -2;

    b1->LeftEdge = 0;
    b1->TopEdge = 0;
    b1->FrontPen = 1;
    b1->BackPen = 0;
    b1->DrawMode = JAM1;
    b1->Count = 3;
    b1->XY = xy1;
    b1->NextBorder = b2;

    xy2[0] = wm3;
    xy2[1] = -2;
    xy2[2] = wm3;
    xy2[3] = hm3;
    xy2[4] = -2;
    xy2[5] = hm3;

    b2->LeftEdge = 0;
    b2->TopEdge = 0;
    b2->FrontPen = 2;
    b2->BackPen = 0;
    b2->DrawMode = JAM1;
    b2->Count = 3;
    b2->XY = xy2;
    b2->NextBorder = 0;
}

static void center_caption(struct IntuiText *it, WORD w, WORD h, const char *text)
{
    Py68U32 len = 0;
    while (text[len] != '\0')
        ++len;
    it->FrontPen = 1;
    it->BackPen = 0;
    it->DrawMode = JAM1;
    it->ITextFont = 0;
    it->IText = (UBYTE *)text;
    it->NextText = 0;
    it->LeftEdge = (WORD)((w - (WORD)(len * GUI_FONT_W)) >> 1);
    if (it->LeftEdge < 2)
        it->LeftEdge = 2;
    it->TopEdge = (WORD)((h - GUI_FONT_H) >> 1);
    if (it->TopEdge < 1)
        it->TopEdge = 1;
}

static void link_gadget(struct Gadget *g)
{
    if (s_gadget_count == 0) {
        s_nw.FirstGadget = g;
        g->NextGadget = 0;
    } else {
        s_gadgets[s_gadget_count - 1].NextGadget = g;
        g->NextGadget = 0;
    }
}

static void draw_labels(void)
{
    UWORD i;
    if (s_rport == 0 || IntuitionBase == 0)
        return;
    for (i = 0; i < s_label_count; ++i) {
        PrintIText(s_rport, &s_labels[i], s_label_xy[i][0], s_label_xy[i][1]);
    }
}

static void draw_progress_bars(void)
{
    UWORD i;
    if (s_rport == 0 || GfxBase == 0)
        return;
    for (i = 0; i < s_gadget_count; ++i) {
        struct Gadget *g;
        WORD x0, y0, x1, y1;
        WORD fill_w;
        UWORD pct;
        if (s_widget_kind[i] != GUI_WIDGET_PROGRESS)
            continue;
        g = &s_gadgets[i];
        pct = s_progress_pct[i];
        if (pct > 100)
            pct = 100;
        x0 = (WORD)(g->LeftEdge + 2);
        y0 = (WORD)(g->TopEdge + 2);
        x1 = (WORD)(g->LeftEdge + g->Width - 3);
        y1 = (WORD)(g->TopEdge + g->Height - 3);
        if (x1 < x0 || y1 < y0)
            continue;
        SetAPen(s_rport, 0);
        RectFill(s_rport, x0, y0, x1, y1);
        if (pct == 0)
            continue;
        {
            WORD maxw = (WORD)(g->Width - 4);
            ULONG acc = (ULONG)maxw * (ULONG)pct;
            fill_w = 0;
            while (acc >= 100UL) {
                acc -= 100UL;
                ++fill_w;
            }
        }
        if (fill_w <= 0)
            continue;
        SetAPen(s_rport, 2);
        RectFill(s_rport, x0, y0, (WORD)(x0 + fill_w - 1), y1);
    }
}

static void gui_full_redraw(void)
{
    if (s_window == 0 || s_rport == 0 || IntuitionBase == 0)
        return;
    if (s_gadget_count != 0 && s_nw.FirstGadget != 0)
        RefreshGList(s_nw.FirstGadget, s_window, 0, -1);
    draw_progress_bars();
    draw_labels();
}

static LONG find_gadget_slot(struct Gadget *g)
{
    UWORD i;
    for (i = 0; i < s_gadget_count; ++i) {
        if (&s_gadgets[i] == g)
            return (LONG)i;
    }
    return -1;
}

/* HAS gui_button_render: swap bevel pens and redraw (no GADGHCOMP flash). */
static void button_render(struct Gadget *g, int pressed)
{
    LONG slot;
    struct Border *b1;
    struct Border *b2;

    if (g == 0 || s_rport == 0 || IntuitionBase == 0)
        return;
    slot = find_gadget_slot(g);
    if (slot < 0 || s_widget_kind[slot] != GUI_WIDGET_BUTTON)
        return;
    b1 = &s_borders[slot * 2];
    b2 = &s_borders[slot * 2 + 1];
    if (pressed) {
        b1->FrontPen = 1; /* recessed: top-left black */
        b2->FrontPen = 2; /* bottom-right white */
    } else {
        b1->FrontPen = 2; /* raised: top-left white */
        b2->FrontPen = 1; /* bottom-right black */
    }
    DrawBorder(s_rport, b1, g->LeftEdge, g->TopEdge);
}

static void do_close_window(void)
{
    struct IntuiMessage *msg;
    struct Window *win;

    win = s_window;
    if (win == 0) {
        s_shown = 0;
        return;
    }
    if (IntuitionBase == 0) {
        s_window = 0;
        s_shown = 0;
        return;
    }

    /* HAS/RKM CloseWindowSafely order: drain BEFORE ModifyIDCMP(0). */
    Forbid();

    if (s_userport != 0) {
        for (;;) {
            msg = (struct IntuiMessage *)GetMsg(s_userport);
            if (msg == 0)
                break;
            ReplyMsg((struct Message *)msg);
        }
    }

    win->UserPort = 0;
    s_userport = 0;
    s_sigmask = 0;

    ModifyIDCMP(win, 0);

    if (s_gadget_count != 0 && s_nw.FirstGadget != 0)
        RemoveGList(win, s_nw.FirstGadget, (LONG)s_gadget_count);

    CloseWindow(win);

    s_window = 0;
    s_rport = 0;
    s_shown = 0;
    s_begun = 0;
    Permit();
}

/* ---- exports ------------------------------------------------------------ */

static Py68Status gui_init(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
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
    s_intuition = OpenLibrary((CONST_STRPTR) "intuition.library", GUI_LIB_VERSION);
    if (s_intuition == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    IntuitionBase = (struct IntuitionBase *)s_intuition;
    s_graphics = OpenLibrary((CONST_STRPTR) "graphics.library", GUI_LIB_VERSION);
    if (s_graphics == 0) {
        CloseLibrary(s_intuition);
        s_intuition = 0;
        IntuitionBase = 0;
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    GfxBase = (struct GfxBase *)s_graphics;
    s_inited = 1;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_shutdown(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    do_close_window();
    s_begun = 0;
    s_gadget_count = 0;
    s_label_count = 0;
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

static Py68Status gui_begin_window(Py68Runtime *runtime, Py68U16 argc,
                                   Py68Value *args, Py68Value *result)
{
    Py68I32 x, y, w, h, idcmp, flags;
    (void)argc;
    if (s_begun || s_shown) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!borrow_cstr(runtime, args[0], s_title, sizeof(s_title))) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[1], &x) || !require_int(args[2], &y) ||
        !require_int(args[3], &w) || !require_int(args[4], &h) ||
        !require_int(args[5], &idcmp) || !require_int(args[6], &flags)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    gui_memset(&s_nw, 0, sizeof(s_nw));
    s_nw.LeftEdge = (WORD)x;
    s_nw.TopEdge = (WORD)y;
    s_nw.Width = (WORD)w;
    s_nw.Height = (WORD)h;
    s_nw.DetailPen = 0;
    s_nw.BlockPen = 1;
    s_nw.IDCMPFlags = (ULONG)idcmp;
    s_nw.Flags = (ULONG)flags;
    s_nw.FirstGadget = 0;
    s_nw.Title = (UBYTE *)s_title;
    s_nw.Type = WBENCHSCREEN;
    s_nw.MinWidth = 90;
    s_nw.MinHeight = 26;
    s_nw.MaxWidth = (UWORD)~0;
    s_nw.MaxHeight = (UWORD)~0;
    s_gadget_count = 0;
    s_label_count = 0;
    s_begun = 1;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_label(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                Py68Value *result)
{
    Py68I32 id, x, y;
    UWORD slot;
    (void)argc;
    if (!s_begun || s_shown || s_label_count >= GUI_MAX_LABELS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    slot = s_label_count;
    if (!borrow_cstr(runtime, args[3], s_label_text[slot], sizeof(s_label_text[slot]))) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    gui_memset(&s_labels[slot], 0, sizeof(s_labels[slot]));
    s_labels[slot].FrontPen = 1;
    s_labels[slot].DrawMode = JAM1;
    s_labels[slot].IText = (UBYTE *)s_label_text[slot];
    s_label_xy[slot][0] = (WORD)x;
    s_label_xy[slot][1] = (WORD)y;
    s_label_ids[slot] = (UWORD)id;
    ++s_label_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_button(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    Py68I32 id, x, y, w, h;
    UWORD slot;
    struct Gadget *g;
    (void)argc;
    if (!s_begun || s_shown || s_gadget_count >= GUI_MAX_GADGETS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &w) ||
        !require_int(args[4], &h)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    slot = s_gadget_count;
    if (!borrow_cstr(runtime, args[5], s_captions[slot], sizeof(s_captions[slot]))) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    g = &s_gadgets[slot];
    gui_memset(g, 0, sizeof(*g));
    fill_button_border(slot, (WORD)w, (WORD)h);
    center_caption(&s_gtexts[slot], (WORD)w, (WORD)h, s_captions[slot]);
    g->LeftEdge = (WORD)x;
    g->TopEdge = (WORD)y;
    g->Width = (WORD)w;
    g->Height = (WORD)h;
    /* HAS: GFLG_GADGHNONE — complement flash looks like a white/orange frame. */
    g->Flags = GFLG_GADGHNONE;
    g->Activation = GACT_RELVERIFY | GACT_IMMEDIATE;
    g->GadgetType = GTYP_BOOLGADGET;
    g->GadgetRender = (APTR)&s_borders[slot * 2];
    g->GadgetText = &s_gtexts[slot];
    g->GadgetID = (UWORD)id;
    s_widget_kind[slot] = GUI_WIDGET_BUTTON;
    link_gadget(g);
    ++s_gadget_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_editbox(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    Py68I32 id, x, y, w, h, maxlen;
    UWORD slot;
    struct Gadget *g;
    struct StringInfo *si;
    (void)argc;
    if (!s_begun || s_shown || s_gadget_count >= GUI_MAX_GADGETS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &w) ||
        !require_int(args[4], &h) || !require_int(args[6], &maxlen)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (maxlen < 2)
        maxlen = 2;
    if (maxlen > GUI_MAX_EDIT_LEN)
        maxlen = GUI_MAX_EDIT_LEN;
    if (w < 8)
        w = 8;
    if (h < 8)
        h = 8;
    slot = s_gadget_count;
    gui_memset(s_edit_buf[slot], 0, GUI_MAX_EDIT_LEN);
    gui_memset(s_edit_undo[slot], 0, GUI_MAX_EDIT_LEN);
    borrow_cstr(runtime, args[5], s_edit_buf[slot], (Py68U32)maxlen);
    g = &s_gadgets[slot];
    si = &s_sinfos[slot];
    gui_memset(g, 0, sizeof(*g));
    gui_memset(si, 0, sizeof(*si));
    /* Designer supplies outer rect; text area is inset by 2 (HAS GuiAddEditBox). */
    fill_string_border(slot, (WORD)w, (WORD)h);
    si->Buffer = (UBYTE *)s_edit_buf[slot];
    si->UndoBuffer = (UBYTE *)s_edit_undo[slot];
    si->MaxChars = (WORD)maxlen;
    g->LeftEdge = (WORD)(x + 2);
    g->TopEdge = (WORD)(y + 2);
    g->Width = (WORD)(w - 4);
    g->Height = (WORD)(h - 4);
    g->Flags = GFLG_GADGHCOMP | GFLG_TABCYCLE;
    g->Activation = GACT_RELVERIFY;
    g->GadgetType = GTYP_STRGADGET;
    g->GadgetRender = (APTR)&s_borders[slot * 2];
    g->SpecialInfo = (APTR)si;
    g->GadgetID = (UWORD)id;
    s_widget_kind[slot] = GUI_WIDGET_EDIT;
    link_gadget(g);
    ++s_gadget_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_checkbox(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 id, x, y, w, h, checked;
    UWORD slot;
    struct Gadget *g;
    (void)argc;
    if (!s_begun || s_shown || s_gadget_count >= GUI_MAX_GADGETS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &w) ||
        !require_int(args[4], &h) || !require_int(args[6], &checked)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    slot = s_gadget_count;
    if (!borrow_cstr(runtime, args[5], s_captions[slot], sizeof(s_captions[slot]))) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    g = &s_gadgets[slot];
    gui_memset(g, 0, sizeof(*g));
    fill_button_border(slot, (WORD)w, (WORD)h);
    center_caption(&s_gtexts[slot], (WORD)w, (WORD)h, s_captions[slot]);
    g->LeftEdge = (WORD)x;
    g->TopEdge = (WORD)y;
    g->Width = (WORD)w;
    g->Height = (WORD)h;
    g->Flags = GFLG_GADGHNONE;
    if (checked)
        g->Flags |= GFLG_SELECTED;
    g->Activation = GACT_RELVERIFY | GACT_TOGGLESELECT;
    g->GadgetType = GTYP_BOOLGADGET;
    g->GadgetRender = (APTR)&s_borders[slot * 2];
    g->GadgetText = &s_gtexts[slot];
    g->GadgetID = (UWORD)id;
    s_widget_kind[slot] = GUI_WIDGET_CHECKBOX;
    link_gadget(g);
    ++s_gadget_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_list(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                               Py68Value *result)
{
    Py68I32 id, x, y, w, h, selected;
    UWORD slot;
    struct Gadget *g;
    const Py68ExtServices *svc;
    Py68U32 count;
    Py68U32 i;
    char label_buf[128];
    (void)argc;
    if (!s_begun || s_shown || s_gadget_count >= GUI_MAX_GADGETS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &w) ||
        !require_int(args[4], &h) || !require_int(args[6], &selected)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    svc = py68_ext_services(runtime);
    if (svc == 0 || svc->list_count == 0)
        count = 0;
    else
        count = svc->list_count(runtime, args[5]);
    if (count == 0)
        count = 1;
    slot = s_gadget_count;
    /* Build a multi-line caption from list items for display. */
    label_buf[0] = '\0';
    for (i = 0; i < count && i < 8; ++i) {
        Py68Value item;
        if (svc->list_get_copy(runtime, args[5], (Py68I32)i, &item) != PY68_STATUS_OK)
            break;
        {
            char tmp[48];
            if (borrow_cstr(runtime, item, tmp, sizeof(tmp))) {
                if (label_buf[0] != '\0' && gui_strlen(label_buf) + 1 < sizeof(label_buf))
                    gui_strcat(label_buf, "\n");
                if (gui_strlen(label_buf) + gui_strlen(tmp) < sizeof(label_buf))
                    gui_strcat(label_buf, tmp);
            }
            if (svc->value_release != 0)
                svc->value_release(runtime, item);
        }
    }
    if (label_buf[0] == '\0')
        gui_strcpy(label_buf, "(list)");
    gui_strncpy(s_captions[slot], label_buf, sizeof(s_captions[slot]) - 1);
    s_captions[slot][sizeof(s_captions[slot]) - 1] = '\0';
    g = &s_gadgets[slot];
    gui_memset(g, 0, sizeof(*g));
    fill_button_border(slot, (WORD)w, (WORD)h);
    center_caption(&s_gtexts[slot], (WORD)w, (WORD)h, s_captions[slot]);
    g->LeftEdge = (WORD)x;
    g->TopEdge = (WORD)y;
    g->Width = (WORD)w;
    g->Height = (WORD)h;
    g->Flags = GFLG_GADGHNONE;
    g->Activation = GACT_RELVERIFY | GACT_IMMEDIATE;
    g->GadgetType = GTYP_BOOLGADGET;
    g->GadgetRender = (APTR)&s_borders[slot * 2];
    g->GadgetText = &s_gtexts[slot];
    g->GadgetID = (UWORD)id;
    s_widget_kind[slot] = GUI_WIDGET_LIST;
    s_list_rows[slot] = (UWORD)count;
    if (selected < 0)
        selected = 0;
    if ((UWORD)selected >= count)
        selected = 0;
    s_list_selected[slot] = (UWORD)selected;
    link_gadget(g);
    ++s_gadget_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_bitmap(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    /* Bitmap packing deferred; keep export for API stability. */
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_add_progress(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 id, x, y, w, h, percent;
    UWORD slot;
    struct Gadget *g;
    (void)runtime;
    (void)argc;
    if (!s_begun || s_shown || s_gadget_count >= GUI_MAX_GADGETS) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (!require_int(args[0], &id) || !require_int(args[1], &x) ||
        !require_int(args[2], &y) || !require_int(args[3], &w) ||
        !require_int(args[4], &h) || !require_int(args[5], &percent)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    slot = s_gadget_count;
    g = &s_gadgets[slot];
    gui_memset(g, 0, sizeof(*g));
    fill_button_border(slot, (WORD)w, (WORD)h);
    g->LeftEdge = (WORD)x;
    g->TopEdge = (WORD)y;
    g->Width = (WORD)w;
    g->Height = (WORD)h;
    g->Flags = GFLG_GADGHNONE;
    g->Activation = GACT_RELVERIFY;
    g->GadgetType = GTYP_BOOLGADGET;
    g->GadgetRender = (APTR)&s_borders[slot * 2];
    g->GadgetText = 0;
    g->GadgetID = (UWORD)id;
    s_widget_kind[slot] = GUI_WIDGET_PROGRESS;
    s_progress_pct[slot] = (UWORD)percent;
    link_gadget(g);
    ++s_gadget_count;
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_show(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                           Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    if (!s_begun || s_shown) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    s_window = OpenWindow(&s_nw);
    if (s_window == 0) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    s_rport = s_window->RPort;
    s_userport = s_window->UserPort;
    if (s_userport != 0)
        s_sigmask = 1UL << s_userport->mp_SigBit;
    else
        s_sigmask = 0;
    draw_labels();
    draw_progress_bars();
    s_shown = 1;
    *result = py68_ext_value_int((Py68I32)(ULONG)s_window);
    return PY68_STATUS_OK;
}

static Py68Status gui_close_window(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    do_close_window();
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

static Py68Status gui_wait_event(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    struct IntuiMessage *msg;
    ULONG class_bits;
    UWORD code;
    APTR iaddr;
    LONG evt;
    (void)runtime;
    (void)argc;
    (void)args;
    if (!s_shown || s_userport == 0) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (;;) {
        msg = (struct IntuiMessage *)GetMsg(s_userport);
        if (msg == 0) {
            Wait(s_sigmask);
            continue;
        }
        class_bits = msg->Class;
        code = msg->Code;
        iaddr = msg->IAddress;
        s_evt_mx = msg->MouseX;
        s_evt_my = msg->MouseY;
        ReplyMsg((struct Message *)msg);

        s_evt_code = (LONG)code;
        s_evt_id = 0;
        evt = GUI_EVT_NONE;

        if (class_bits & IDCMP_CLOSEWINDOW) {
            evt = GUI_EVT_CLOSE;
        } else if (class_bits & IDCMP_GADGETUP) {
            struct Gadget *g = (struct Gadget *)iaddr;
            LONG slot;
            if (g != 0) {
                s_evt_id = (LONG)g->GadgetID;
                if ((g->GadgetType & GTYP_GTYPEMASK) == GTYP_STRGADGET) {
                    evt = GUI_EVT_STRING;
                } else {
                    slot = find_gadget_slot(g);
                    if (slot >= 0) {
                        if (s_widget_kind[slot] == GUI_WIDGET_CHECKBOX)
                            evt = GUI_EVT_CHECKBOX;
                        else if (s_widget_kind[slot] == GUI_WIDGET_LIST) {
                            WORD row = (WORD)((s_evt_my - g->TopEdge) >> 3); /* GUI_FONT_H==8 */
                            if (row < 0)
                                row = 0;
                            if ((UWORD)row >= s_list_rows[slot] && s_list_rows[slot] != 0)
                                row = (WORD)(s_list_rows[slot] - 1);
                            s_list_selected[slot] = (UWORD)row;
                            evt = GUI_EVT_LIST;
                        } else if (s_widget_kind[slot] == GUI_WIDGET_BITMAP ||
                                   s_widget_kind[slot] == GUI_WIDGET_PROGRESS)
                            evt = GUI_EVT_NONE;
                        else {
                            button_render(g, 0); /* release: raised bevel */
                            evt = GUI_EVT_BUTTON;
                        }
                    }
                }
            }
        } else if (class_bits & IDCMP_GADGETDOWN) {
            struct Gadget *g = (struct Gadget *)iaddr;
            if (g != 0) {
                s_evt_id = (LONG)g->GadgetID;
                button_render(g, 1); /* press: recessed bevel */
            }
            evt = GUI_EVT_PRESS;
        } else if (class_bits & IDCMP_VANILLAKEY) {
            evt = GUI_EVT_KEY;
        } else if (class_bits & IDCMP_MOUSEBUTTONS) {
            evt = GUI_EVT_MOUSE;
        } else if (class_bits & IDCMP_REFRESHWINDOW) {
            BeginRefresh(s_window);
            draw_progress_bars();
            draw_labels();
            EndRefresh(s_window, TRUE);
            evt = GUI_EVT_REFRESH;
        } else {
            continue;
        }
        *result = py68_ext_value_int(evt);
        return PY68_STATUS_OK;
    }
}

static Py68Status gui_get_event_id(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    *result = py68_ext_value_int(s_evt_id);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_event_code(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                     Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    *result = py68_ext_value_int(s_evt_code);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_event_x(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    *result = py68_ext_value_int((Py68I32)s_evt_mx);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_event_y(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                  Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    *result = py68_ext_value_int((Py68I32)s_evt_my);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_edit_text(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                    Py68Value *result)
{
    Py68I32 id;
    UWORD i;
    const Py68ExtServices *svc;
    (void)argc;
    if (!require_int(args[0], &id)) {
        *result = py68_ext_value_none();
        return PY68_STATUS_RUNTIME_ERROR;
    }
    svc = py68_ext_services(runtime);
    if (svc == 0 || svc->string_new_copy == 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_RUNTIME_ERROR;
    }
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id &&
            s_widget_kind[i] == GUI_WIDGET_EDIT) {
            const char *buf = s_edit_buf[i];
            Py68U32 len = 0;
            while (buf[len] != '\0')
                ++len;
            return svc->string_new_copy(runtime, buf, len, result);
        }
    }
    return svc->string_new_copy(runtime, "", 0, result);
}

static Py68Status gui_set_edit_text(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                    Py68Value *result)
{
    Py68I32 id;
    UWORD i;
    (void)argc;
    if (!require_int(args[0], &id)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id &&
            s_widget_kind[i] == GUI_WIDGET_EDIT) {
            struct StringInfo *si = &s_sinfos[i];
            Py68U32 maxlen = (Py68U32)si->MaxChars;
            if (maxlen > GUI_MAX_EDIT_LEN)
                maxlen = GUI_MAX_EDIT_LEN;
            if (!borrow_cstr(runtime, args[1], s_edit_buf[i], maxlen)) {
                *result = py68_ext_value_int(-1);
                return PY68_STATUS_OK;
            }
            {
                Py68U32 len = 0;
                while (s_edit_buf[i][len] != '\0')
                    ++len;
                si->BufferPos = 0;
                si->NumChars = (WORD)len;
            }
            if (s_shown && s_window != 0)
                RefreshGList(&s_gadgets[i], s_window, 0, 1);
            *result = py68_ext_value_int(0);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_checkbox(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 id;
    UWORD i;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &id)) {
        *result = py68_ext_value_int(0);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id &&
            s_widget_kind[i] == GUI_WIDGET_CHECKBOX) {
            *result = py68_ext_value_int((s_gadgets[i].Flags & GFLG_SELECTED) ? 1 : 0);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(0);
    return PY68_STATUS_OK;
}

static Py68Status gui_get_list_selected(Py68Runtime *runtime, Py68U16 argc,
                                        Py68Value *args, Py68Value *result)
{
    Py68I32 id;
    UWORD i;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &id)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id &&
            s_widget_kind[i] == GUI_WIDGET_LIST) {
            *result = py68_ext_value_int((Py68I32)s_list_selected[i]);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_set_progress(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                   Py68Value *result)
{
    Py68I32 id, percent;
    UWORD i;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &id) || !require_int(args[1], &percent)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id &&
            s_widget_kind[i] == GUI_WIDGET_PROGRESS) {
            s_progress_pct[i] = (UWORD)percent;
            if (s_shown)
                draw_progress_bars();
            *result = py68_ext_value_int(0);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_set_label_text(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                     Py68Value *result)
{
    Py68I32 id;
    UWORD i;
    (void)argc;
    if (!require_int(args[0], &id)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < s_label_count; ++i) {
        if (s_label_ids[i] == (UWORD)id) {
            if (!borrow_cstr(runtime, args[1], s_label_text[i],
                             sizeof(s_label_text[i]))) {
                *result = py68_ext_value_int(-1);
                return PY68_STATUS_OK;
            }
            s_labels[i].IText = s_label_text[i];
            if (s_shown)
                draw_labels();
            *result = py68_ext_value_int(0);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_enable_widget(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                    Py68Value *result)
{
    Py68I32 id, enable;
    UWORD i;
    (void)runtime;
    (void)argc;
    if (!require_int(args[0], &id) || !require_int(args[1], &enable)) {
        *result = py68_ext_value_int(-1);
        return PY68_STATUS_OK;
    }
    for (i = 0; i < s_gadget_count; ++i) {
        if (s_gadgets[i].GadgetID == (UWORD)id) {
            if (enable)
                s_gadgets[i].Flags = (UWORD)(s_gadgets[i].Flags & ~GFLG_DISABLED);
            else
                s_gadgets[i].Flags = (UWORD)(s_gadgets[i].Flags | GFLG_DISABLED);
            if (s_shown && s_window != 0)
                RefreshGList(&s_gadgets[i], s_window, 0, 1);
            *result = py68_ext_value_int(0);
            return PY68_STATUS_OK;
        }
    }
    *result = py68_ext_value_int(-1);
    return PY68_STATUS_OK;
}

static Py68Status gui_redraw(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                             Py68Value *result)
{
    (void)runtime;
    (void)argc;
    (void)args;
    gui_full_redraw();
    *result = py68_ext_value_none();
    return PY68_STATUS_OK;
}

const Py68ExtExport gui_intuition_exports[25] = {
    { "init", 0, 0, gui_init },
    { "shutdown", 0, 0, gui_shutdown },
    { "begin_window", 7, 7, gui_begin_window },
    { "add_label", 4, 4, gui_add_label },
    { "add_button", 6, 6, gui_add_button },
    { "add_editbox", 7, 7, gui_add_editbox },
    { "add_checkbox", 7, 7, gui_add_checkbox },
    { "add_list", 7, 7, gui_add_list },
    { "add_bitmap", 6, 6, gui_add_bitmap },
    { "add_progress", 6, 6, gui_add_progress },
    { "show", 0, 0, gui_show },
    { "close_window", 0, 0, gui_close_window },
    { "wait_event", 0, 0, gui_wait_event },
    { "get_event_id", 0, 0, gui_get_event_id },
    { "get_event_code", 0, 0, gui_get_event_code },
    { "get_event_x", 0, 0, gui_get_event_x },
    { "get_event_y", 0, 0, gui_get_event_y },
    { "get_edit_text", 1, 1, gui_get_edit_text },
    { "set_edit_text", 2, 2, gui_set_edit_text },
    { "get_checkbox", 1, 1, gui_get_checkbox },
    { "get_list_selected", 1, 1, gui_get_list_selected },
    { "set_progress", 2, 2, gui_set_progress },
    { "set_label_text", 2, 2, gui_set_label_text },
    { "enable_widget", 2, 2, gui_enable_widget },
    { "redraw", 0, 0, gui_redraw }
};

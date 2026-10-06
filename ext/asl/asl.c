/* 2026 by Piotr Rozentreter (Rozsoft)
 *
 * asl.py68k — drawer requester for the Amiga GUI installer.
 * Export: ask_drawer(title, initial_drawer) -> str (empty on cancel).
 *
 * Do not link startup.o / vc.lib / amiga.lib (D-0027).
 */

#include "py68k_ext.h"

typedef struct Py68Runtime Py68Runtime;

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <libraries/asl.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/asl.h>

struct ExecBase *SysBase;
struct Library *AslBase;

static int require_cstr(Py68Runtime *runtime, Py68Value v, char *dst, Py68U32 dst_size)
{
    const Py68ExtServices *svc;
    const char *data;
    Py68U32 length;
    Py68U32 copy;
    Py68U32 i;
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
    for (i = 0; i < copy; ++i)
        dst[i] = data[i];
    dst[copy] = '\0';
    return 1;
}

static Py68Status asl_ask_drawer(Py68Runtime *runtime, Py68U16 argc, Py68Value *args,
                                 Py68Value *result)
{
    char title[96];
    char initial[256];
    struct FileRequester *req;
    const Py68ExtServices *svc;
    LONG ok;
    (void)argc;

    svc = py68_ext_services(runtime);
    if (svc == 0 || svc->string_new_copy == 0) {
        *result = py68_ext_value_none();
        return PY68_STATUS_RUNTIME_ERROR;
    }

    title[0] = '\0';
    initial[0] = '\0';
    require_cstr(runtime, args[0], title, sizeof(title));
    require_cstr(runtime, args[1], initial, sizeof(initial));
    if (title[0] == '\0') {
        /* "Select drawer" without relying on libc strcpy */
        title[0] = 'S'; title[1] = 'e'; title[2] = 'l'; title[3] = 'e';
        title[4] = 'c'; title[5] = 't'; title[6] = ' '; title[7] = 'd';
        title[8] = 'r'; title[9] = 'a'; title[10] = 'w'; title[11] = 'e';
        title[12] = 'r'; title[13] = '\0';
    }

    if (SysBase == 0)
        SysBase = *((struct ExecBase **)4);
    if (AslBase == 0) {
        AslBase = OpenLibrary((UBYTE *)"asl.library", 37L);
        if (AslBase == 0)
            return svc->string_new_copy(runtime, "", 0, result);
    }

    req = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)title,
        ASLFR_InitialDrawer, (ULONG)initial,
        ASLFR_DrawersOnly, TRUE,
        TAG_DONE);
    if (req == 0)
        return svc->string_new_copy(runtime, "", 0, result);

    ok = AslRequest(req, NULL);
    if (!ok || req->fr_Drawer == 0) {
        FreeAslRequest(req);
        return svc->string_new_copy(runtime, "", 0, result);
    }
    {
        const char *drawer = (const char *)req->fr_Drawer;
        Py68U32 len = 0;
        Py68Status st;
        while (drawer[len] != '\0')
            ++len;
        st = svc->string_new_copy(runtime, drawer, len, result);
        FreeAslRequest(req);
        return st;
    }
}

const Py68ExtExport asl_exports[1] = {
    { "ask_drawer", 2, 2, asl_ask_drawer }
};

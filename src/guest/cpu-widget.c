// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
/* A user-space CPU view; the kernel owns accounting, not desktop windows. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/kernel.h>
#include <proto/timer.h>
#include <aros/apple/cpustats.h>
#include <devices/timer.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <intuition/screens.h>
#include <stdio.h>
#include <stdlib.h>

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
APTR KernelBase;
struct Device *TimerBase;
int __nowbsupport = 1;
int __nostdiowin = 1;

static void mark(const char *text)
{
    while (*text) RawPutChar(*text++);
}

static uint64_t counter(void)
{
    struct EClockVal value;
    ReadEClock(&value);
    return ((uint64_t)value.ev_hi << 32) | value.ev_lo;
}

int main(int argc, char **argv)
{
    unsigned seconds = 0;
    if (argc > 2) return 20;
    if (argc == 2) {
        char *end;
        unsigned long requested = strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end || requested > 3600) return 20;
        seconds = (unsigned)requested;
    }
    int result = 20, stopped = 0, reported = 0;
    struct Window *window = NULL;
    struct Screen *screen = NULL;
    struct MsgPort *port = NULL;
    struct timerequest *timer = NULL;
    struct AROSCPUSample before[AROS_APPLE_CPU_STATS_MAX] = {{0}};
    unsigned char valid[AROS_APPLE_CPU_STATS_MAX] = {0};
    UBYTE ready[4];
    unsigned wait;
    for (wait = 0; wait < 1000; ++wait) {
        if (GetVar((CONST_STRPTR)"Sys/DesktopAccepted", ready, sizeof(ready), GVF_GLOBAL_ONLY) == 1 && ready[0] == '1') break;
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) return 0;
        Delay(1);
    }
    if (wait == 1000) { mark("FAIL: CPU widget desktop not accepted\n"); return 20; }
    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    if (!KernelBase) goto done;
    const struct AROSAppleCPUStats *stats = (const void *)KrnGetSystemAttr(AROS_APPLE_KATTR_CPU_STATS);
    if ((IPTR)stats == (IPTR)-1 || !AROSAppleCPUStatsValid(stats)) goto done;
    for (unsigned i = 0; i < stats->cpu_count; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (stats->accounting[i] && stats->accounting[i] == stats->accounting[j]) goto done;
    port = CreateMsgPort();
    timer = port ? (struct timerequest *)CreateIORequest(port, sizeof(*timer)) : NULL;
    if (!timer || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &timer->tr_node, 0)) goto done;
    TimerBase = timer->tr_node.io_Device;
    struct EClockVal initial;
    if (ReadEClock(&initial) != stats->frequency) goto done;
    IntuitionBase = (void *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (void *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase) goto done;
    screen = LockPubScreen((CONST_STRPTR)"Workbench");
    if (!screen || screen->Width <= 48 || screen->Height <= 64 ||
        !screen->RastPort.Font || !screen->RastPort.Font->tf_XSize ||
        !screen->RastPort.Font->tf_YSize) goto done;
    unsigned font_x = screen->RastPort.Font->tf_XSize;
    unsigned font_y = screen->RastPort.Font->tf_YSize;
    unsigned columns = (stats->cpu_count + 7) / 8;
    unsigned available = screen->Width > 48 ? screen->Width - 48 : 0;
    unsigned max_columns = available / (font_x * 16 + 16);
    if (!max_columns) goto done;
    if (columns > max_columns) columns = max_columns;
    unsigned rows = (stats->cpu_count + columns - 1) / columns;
    unsigned cell = available / columns;
    if (cell > 200) cell = 200;
    unsigned row_height = font_y + 16;
    unsigned width = cell * columns + 16;
    unsigned height = rows * row_height + screen->WBorTop + font_y + 24;
    if (height >= (unsigned)screen->Height || width >= (unsigned)screen->Width) goto done;
    window = OpenWindowTags(NULL, WA_PubScreen, (IPTR)screen,
        WA_Left, screen->Width - width - 16, WA_Top, 28,
        WA_Width, width, WA_Height, height, WA_Title, (IPTR)"CPU load",
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, FALSE, WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!window) goto done;
    const uint64_t started = counter();
    unsigned frames = 0;
    while (!stopped && (!seconds || counter() - started < (uint64_t)seconds * stats->frequency)) {
        struct IntuiMessage *message;
        while ((message = (void *)GetMsg(window->UserPort))) {
            ULONG cls = message->Class;
            ReplyMsg((struct Message *)message);
            if (cls == IDCMP_CLOSEWINDOW) stopped = 1;
            if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(window); EndRefresh(window, TRUE); }
        }
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) stopped = 1;
        struct RastPort *rp = window->RPort;
        unsigned good = 0, online = AROSAppleCPUStatsOnline(stats);
        const uint64_t now = counter();
        for (unsigned i = 0; i < stats->cpu_count; ++i) {
            struct AROSCPUSample after;
            int sampled = stats->accounting[i] && AROSAppleCPURead(stats->accounting[i], now, &after);
            int load = sampled && valid[i] ? AROSAppleCPULoad(&before[i], &after) : -1;
            if (sampled) before[i] = after;
            valid[i] = sampled;
            if (load >= 0) ++good;
            unsigned x = window->BorderLeft + 6 + (i / rows) * cell;
            unsigned y = window->BorderTop + 4 + (i % rows) * row_height;
            SetAPen(rp, 0); RectFill(rp, x, y, x + cell - 10, y + row_height - 2);
            SetAPen(rp, 1); Move(rp, x, y + font_y);
            char label[48];
            int length = load < 0 ? snprintf(label, sizeof(label), "CPU %u --", i) :
                snprintf(label, sizeof(label), "CPU %u %u.%u %%", i, (unsigned)load / 10, (unsigned)load % 10);
            Text(rp, (CONST_STRPTR)label, length);
            unsigned top = y + font_y + 3, span = cell - 14;
            RectFill(rp, x, top, x + span, top + 7);
            SetAPen(rp, 2); RectFill(rp, x + 1, top + 1, x + span - 1, top + 6);
            if (load > 0) { SetAPen(rp, 3); RectFill(rp, x + 1, top + 1, x + 1 + (span - 2) * (unsigned)load / 1000, top + 6); }
        }
        ++frames;
        if (!reported && online && good == online && frames >= 2) {
            const unsigned x = window->BorderLeft + 6, y = window->BorderTop + font_y + 7;
            if (ReadPixel(rp, x, y) == ReadPixel(rp, x + 1, y + 1)) goto done;
            char text[128];
            snprintf(text, sizeof(text), "[VM] CPU widget ready cores=%u online=%u samples=%u\n", stats->cpu_count, online, good);
            mark(text); reported = 1;
        }
        Delay(5);
    }
    result = reported ? 0 : 20;
done:
    if (window) CloseWindow(window);
    if (screen) UnlockPubScreen(NULL, screen);
    if (GfxBase) CloseLibrary((void *)GfxBase);
    if (IntuitionBase) CloseLibrary((void *)IntuitionBase);
    if (TimerBase) CloseDevice(&timer->tr_node);
    if (timer) DeleteIORequest(&timer->tr_node);
    if (port) DeleteMsgPort(port);
    mark(result == 0 ? "[VM] CPU widget closed\n" : "FAIL: CPU widget initialization or rendering\n");
    return result;
}

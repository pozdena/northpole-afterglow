/* North Pole: the After Dark side. Receives After Dark's messages, keeps the scene
 * in a locked handle, and paints the 160 x 120 frame onto the screen scaled up, only
 * where it changed. Everything is one translation unit (no globals, no relocations).
 */
#include "mac.h"
#include "scene.c"

typedef struct {
    Scene scene;
    unsigned char fb[2][FB_SIZE];
    short colX[FB_W + 1];      /* screen x of each logical column edge */
    short rowY[FB_H + 1];      /* screen y of each logical row edge */
    RGBColor pal[NUM_COLORS];
    long lastTick;
    long startTick;            /* TickCount at msgInitialize */
    unsigned long startClock;  /* the Mac's clock at msgInitialize */
    short waiting;             /* the first scene waits for the clock to tick */
    short cur;                 /* fb[cur] is drawn next; fb[cur ^ 1] is on screen */
    short color;               /* Color QuickDraw present */
    short lastColor;
} Globals;

static void paint_run(void *ctx, short x0, short x1, short y, unsigned char c)
{
    Globals *g = (Globals *)ctx;
    Rect r;
    if (c != g->lastColor) {
        if (g->color) RGBForeColor(&g->pal[c]);
        else ForeColor(c == C_BLACK ? blackColor : whiteColor);
        g->lastColor = c;
    }
    r.left = g->colX[x0];
    r.right = g->colX[x1];
    r.top = g->rowY[y];
    r.bottom = g->rowY[y + 1];
    if (r.left < r.right && r.top < r.bottom) PaintRect(&r);
}

#include "present.c"

static void copy_pstr(unsigned char *dst, const char *src)
{
    unsigned char n = 0;
    while (src[n] && n < 250) { dst[n + 1] = (unsigned char)src[n]; n++; }
    dst[0] = n;
}

static void fit_screen(Globals *g, RgnHandle blankRgn, GMParamBlock *params)
{
    Rect b;
    long w, h;
    short i;
    if (params->monitors && params->monitors->monitorCount > 0)
        b = params->monitors->monitorList[0].bounds;
    else
        b = (*blankRgn)->rgnBBox;
    if (b.right - b.left < FB_W / 4 || b.bottom - b.top < FB_H / 4)
        b = (*blankRgn)->rgnBBox;
    w = b.right - b.left;
    h = b.bottom - b.top;
    for (i = 0; i <= FB_W; i++) g->colX[i] = (short)(b.left + (w * i) / FB_W);
    for (i = 0; i <= FB_H; i++) g->rowY[i] = (short)(b.top + (h * i) / FB_H);
}

static short clamp_slider(short v)
{
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

/* A seed for the first scene. Afterglow starts a fresh emulated Mac each time the screen
   saver comes on, so TickCount alone is the same at every start and every run would open on
   the same scene; the Mac's clock, which Afterglow passes through, tells them apart. But the
   screen saver's host sets that clock only after the module starts, so the first scene is
   built once the clock has ticked (see msgDrawFrame), not at msgInitialize. */
static unsigned long start_seed(void)
{
    return MacClock() * 2654435761UL ^ (unsigned long)TickCount() * 40503UL ^ 1;
}

long ad_main(Handle *storage, RgnHandle blankRgn, long message, GMParamBlock *params)
{
    Globals *g = (storage && *storage) ? (Globals *)**storage : 0;

    switch (message) {
    case msgInitialize: {
        Handle h = NewHandleClear((long)sizeof(Globals));
        short i;
        if (!h) {
            if (params->errorMessage)
                copy_pstr(params->errorMessage, "North Pole needs a little more memory.");
            return -1;
        }
        HLock(h);
        *storage = h;
        g = (Globals *)*h;
        g->color = params->colorQDAvail;
        for (i = 0; i < NUM_COLORS; i++) {
            g->pal[i].red = (unsigned short)(PAL_RGB[i][0] * 257);
            g->pal[i].green = (unsigned short)(PAL_RGB[i][1] * 257);
            g->pal[i].blue = (unsigned short)(PAL_RGB[i][2] * 257);
        }
        fit_screen(g, blankRgn, params);
        g->scene.clock = g->startClock = MacClock();
#ifdef NP_PREVIEW
        /* the third slider is the date, 14..25 December, not the crowd */
        g->scene.previewDay = (short)(1 + (clamp_slider(params->controlValues[2]) * 11 + 50) / 100);
        /* and the checkbox is "show the gift right away", not the sleigh (always on here) */
        g->scene.giftNow = params->controlValues[3] != 0;
        scene_init(&g->scene, start_seed(),
                   clamp_slider(params->controlValues[0]),
                   clamp_slider(params->controlValues[1]),
                   50,
                   1);
#elif defined(NP_MOON)
        /* the moon test: the third slider is the moon's phase, the checkbox runs it
           through its phases */
        g->scene.moonTest = params->controlValues[3] != 0 ? 2 : 1;
        g->scene.moonSet = (short)(((long)clamp_slider(params->controlValues[2]) * 256 + 50) / 100 & 255);
        scene_init(&g->scene, start_seed(),
                   clamp_slider(params->controlValues[0]),
                   clamp_slider(params->controlValues[1]),
                   50,
                   1);
#else
        scene_init(&g->scene, start_seed(),
                   clamp_slider(params->controlValues[0]),
                   clamp_slider(params->controlValues[1]),
                   clamp_slider(params->controlValues[2]),
                   params->controlValues[3] != 0);
#endif
        g->cur = 0;
        fill_bytes(g->fb[1], 0xFF, FB_SIZE);   /* nothing known on screen yet */
        g->lastTick = g->startTick = TickCount();
        g->waiting = 1;
        return 0;
    }

    case msgBlank:
        FillRgn(blankRgn, &params->qdVars->black);
        if (g) fill_bytes(g->fb[g->cur ^ 1], C_BLACK, FB_SIZE);
        return 0;

    case msgDrawFrame: {
        long now, steps;
        if (!g) return 0;
        now = TickCount();
        if (g->waiting) {
            /* the screen stays black until the clock ticks over (at most a second where it
               is live from the start), then the first scene is dealt from the real time */
            unsigned long c = MacClock();
            if (c == g->startClock && now - g->startTick < 180) return 0;
            g->waiting = 0;
            g->scene.clock = c;
            scene_restart(&g->scene, start_seed());
            g->lastTick = now;
            return 0;
        }
        steps = (now - g->lastTick) / 2;      /* 60 ticks a second, the scene runs at 30 */
        if (steps <= 0) return 0;
        if (steps > 4) {
            steps = 4;                         /* after a stall, catch up a little only */
            g->lastTick = now;
        } else {
            g->lastTick += steps * 2;
        }
        g->scene.clock = MacClock();
        while (steps-- > 0) scene_step(&g->scene);
        scene_render(&g->scene, g->fb[g->cur]);
        PenNormal();
        g->lastColor = -1;                     /* the host may have changed the pen */
        present_diff(g, g->fb[g->cur ^ 1], g->fb[g->cur]);
        g->cur ^= 1;
        return 0;
    }

    case msgClose:
        if (storage && *storage) {
            DisposeHandle(*storage);
            *storage = 0;
        }
        return 0;

    default:
        return 0;
    }
}

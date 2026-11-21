/* The little bit of the classic Mac Toolbox this module needs, as inline traps.
 *
 * Toolbox traps use the Pascal convention: arguments pushed left to right, the callee
 * pops them, and a function result is a space the caller reserves first. Traps may
 * trash D0-D2 and A0-A1; D2 is callee-saved for gcc, so every wrapper clobbers it.
 */
#ifndef MAC_H
#define MAC_H

typedef unsigned char Boolean;
typedef char **Handle;
typedef short OSErr;

typedef struct { short top, left, bottom, right; } Rect;
typedef struct { unsigned short red, green, blue; } RGBColor;
typedef struct { short rgnSize; Rect rgnBBox; } Region;
typedef Region **RgnHandle;
typedef struct { unsigned char pat[8]; } Pattern;

/* After Dark's view of QuickDraw's globals: the A5 world, read upward. */
typedef struct {
    void *thePort;
    Pattern white, black, gray, ltGray, dkGray;
} QDVars;

typedef struct {
    Rect bounds;
    short reserved;            /* 10 bytes an entry */
} MonitorData;

typedef struct {
    short monitorCount;
    MonitorData monitorList[1];
} MonitorsInfo;

/* The parameter block After Dark hands every message. Offsets were read off
   Berkeley's own example module ("Bouncing Ball") and are asserted below. */
typedef struct {
    short controlValues[4];    /*  0: sliders/checkboxes, resource IDs 1000..1003 */
    MonitorsInfo *monitors;    /*  8 */
    Boolean colorQDAvail;      /* 12 */
    unsigned char pad;
    short systemConfig;        /* 14 */
    QDVars *qdVars;            /* 16 */
    short brightness;          /* 20 */
    Rect demoRect;             /* 22 */
    unsigned char *errorMessage; /* 30: a Str255 the module may fill */
} GMParamBlock;

typedef char gm_check0[__builtin_offsetof(GMParamBlock, monitors) == 8 ? 1 : -1];
typedef char gm_check1[__builtin_offsetof(GMParamBlock, colorQDAvail) == 12 ? 1 : -1];
typedef char gm_check2[__builtin_offsetof(GMParamBlock, qdVars) == 16 ? 1 : -1];
typedef char gm_check3[__builtin_offsetof(GMParamBlock, errorMessage) == 30 ? 1 : -1];
typedef char md_check[sizeof(MonitorData) == 10 ? 1 : -1];

enum { msgInitialize = 0, msgClose = 1, msgBlank = 2, msgDrawFrame = 3 };
enum { blackColor = 33, whiteColor = 30 };

#define TRAP_CLOBBERS "d0", "d1", "d2", "a0", "a1", "memory", "cc"

static inline void PaintRect(const Rect *r)
{
    __asm__ volatile("move.l %0,-(%%sp)\n\t.short 0xA8A2" : : "r"(r) : TRAP_CLOBBERS);
}

static inline void RGBForeColor(const RGBColor *c)
{
    __asm__ volatile("move.l %0,-(%%sp)\n\t.short 0xAA14" : : "r"(c) : TRAP_CLOBBERS);
}

static inline void ForeColor(long color)
{
    __asm__ volatile("move.l %0,-(%%sp)\n\t.short 0xA862" : : "r"(color) : TRAP_CLOBBERS);
}

static inline void PenNormal(void)
{
    __asm__ volatile(".short 0xA89E" : : : TRAP_CLOBBERS);
}

static inline void FillRgn(RgnHandle rgn, const Pattern *pat)
{
    __asm__ volatile("move.l %0,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t.short 0xA8D6"
                     : : "r"(rgn), "r"(pat) : TRAP_CLOBBERS);
}

/* The low-memory global Time: the clock, as local seconds since 1 Jan 1904. */
static inline unsigned long MacClock(void)
{
    unsigned long t;
    __asm__ volatile("move.l 0x20C.w,%0" : "=d"(t));
    return t;
}

static inline long TickCount(void)
{
    long t;
    __asm__ volatile("clr.l -(%%sp)\n\t.short 0xA975\n\tmove.l (%%sp)+,%0"
                     : "=r"(t) : : TRAP_CLOBBERS);
    return t;
}

/* OS traps take registers: size in D0, handle back in A0. 0xA322 = NewHandle + clear. */
static inline Handle NewHandleClear(long size)
{
    Handle h;
    __asm__ volatile("move.l %1,%%d0\n\t.short 0xA322\n\tmove.l %%a0,%0"
                     : "=r"(h) : "r"(size) : TRAP_CLOBBERS);
    return h;
}

static inline void HLock(Handle h)
{
    __asm__ volatile("move.l %0,%%a0\n\t.short 0xA029" : : "r"(h) : TRAP_CLOBBERS);
}

static inline void DisposeHandle(Handle h)
{
    __asm__ volatile("move.l %0,%%a0\n\t.short 0xA023" : : "r"(h) : TRAP_CLOBBERS);
}

#endif

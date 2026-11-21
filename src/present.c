/* Turn the difference between two frames into horizontal runs of one color.
 *
 * The host defines, before including this file:
 *     static void paint_run(void *ctx, short x0, short x1, short y, unsigned char color);
 * (x1 exclusive). On the Mac that is one PaintRect; in the preview it writes pixels.
 * Only pixels that changed are painted, so a still scene costs nothing per frame.
 */
#include "scene.h"

static void paint_run(void *ctx, short x0, short x1, short y, unsigned char color);

static void present_diff(void *ctx, const unsigned char *prev, const unsigned char *cur)
{
    short y;
    for (y = 0; y < FB_H; y++) {
        const unsigned char *p = prev + y * FB_W;
        const unsigned char *c = cur + y * FB_W;
        short x = 0;
        while (x < FB_W) {
            short x0, last;
            unsigned char col;
            /* skip unchanged pixels four at a time (rows are 4-byte aligned) */
            if ((x & 3) == 0 && x + 4 <= FB_W &&
                *(const u32 *)(p + x) == *(const u32 *)(c + x)) {
                x += 4;
                continue;
            }
            if (p[x] == c[x]) { x++; continue; }
            /* a changed pixel: grow the run over same-colored pixels, changed or not,
               and paint up to the last changed one */
            col = c[x];
            x0 = x;
            last = x;
            x++;
            while (x < FB_W && c[x] == col) {
                if (p[x] != col) last = x;
                x++;
            }
            paint_run(ctx, x0, (short)(last + 1), y, col);
            x = (short)(last + 1);
        }
    }
}

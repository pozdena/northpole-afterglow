/* Desktop preview: run the real scene code natively and stream RGB frames to stdout.
 *
 *   preview SECONDS SEED SCALE [snow change crowd sleigh] | ffmpeg -f rawvideo ...
 *
 * Every frame goes through present_diff into a fake "screen", and the run checks that
 * the screen matches the frame buffer exactly, so the diff painter is tested too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/scene.c"
#include "../src/present.c"

static unsigned char screen[FB_SIZE];
static long runs;

static void paint_run(void *ctx, short x0, short x1, short y, unsigned char color)
{
    (void)ctx;
    if (x0 < 0 || x1 > FB_W || x0 >= x1 || y < 0 || y >= FB_H) {
        fprintf(stderr, "bad run %d..%d y %d\n", x0, x1, y);
        exit(2);
    }
    memset(screen + y * FB_W + x0, color, (size_t)(x1 - x0));
    runs++;
}

int main(int argc, char **argv)
{
    static Scene scene;
    static unsigned char fb[2][FB_SIZE];
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    unsigned long seed = argc > 2 ? strtoul(argv[2], 0, 10) : 1;
    int scale = argc > 3 ? atoi(argv[3]) : 4;
    short snow = argc > 4 ? (short)atoi(argv[4]) : 50;
    short change = argc > 5 ? (short)atoi(argv[5]) : 22;
    short crowd = argc > 6 ? (short)atoi(argv[6]) : 50;
    short sleigh = argc > 7 ? (short)atoi(argv[7]) : 1;
    long frames = (long)seconds * SIM_HZ, f, maxRuns = 0;
    int cur = 0, x, y, k, sleighSeen = 0;
    int flights = 0, lastEnding = 0, cloudy = 0, lastGiftPrint = 0;
    long walkoffAt = 0;
    unsigned char *row = malloc((size_t)FB_W * scale * 3);

    /* the clock: argv[8] as YYYYMMDD, else now; NP_GIFT=1..12 forces a gift */
    {
        time_t now = time(0);
        struct tm tm = *localtime(&now);
        if (argc > 8) {
            long ymd = atol(argv[8]);
            tm.tm_year = (int)(ymd / 10000) - 1900;
            tm.tm_mon = (int)(ymd / 100 % 100) - 1;
            tm.tm_mday = (int)(ymd % 100);
            tm.tm_hour = 21; tm.tm_min = 0; tm.tm_sec = 0;
            tm.tm_isdst = -1;
            now = mktime(&tm);
            tm = *localtime(&now);
        }
        scene.clock = (unsigned long)(now + tm.tm_gmtoff + 2082844800L);
        if (getenv("NP_GIFT")) scene.giftForce = (short)atoi(getenv("NP_GIFT"));
        if (getenv("NP_PREVIEW")) scene.previewDay = (short)atoi(getenv("NP_PREVIEW"));   /* 1..12 */
        if (getenv("NP_NOW")) scene.giftNow = 1;
    }
    scene_init(&scene, seed, snow, change, crowd, sleigh);
    fprintf(stderr, "moon phase %d/256, gift %d\n", scene.moonPhase, scene.gift);
    memset(fb[1], 0xFF, FB_SIZE);      /* first frame paints everything */
    memset(screen, 0xEE, FB_SIZE);

    for (f = 0; f < frames; f++) {
        long before = runs;
        scene_step(&scene);
        if (scene.sleighActive && !sleighSeen) {
            fprintf(stderr, "  sleigh takes off at %.1f s\n", f / (double)SIM_HZ);
            flights++;
        }
        sleighSeen = scene.sleighActive;
        if (scene.cloudIn > 0 && !cloudy) fprintf(stderr, "  clouds roll in at %.1f s\n", f / (double)SIM_HZ);
        if (scene.cloudIn == 0 && cloudy) fprintf(stderr, "  sky clear at %.1f s\n", f / (double)SIM_HZ);
        cloudy = scene.cloudIn > 0;
        if (scene.ending != lastEnding) {
            if (scene.ending == END_WALKOFF) {
                walkoffAt = f;
                fprintf(stderr, "scene time is up at %.1f s: Santa and Rudolph walk off\n", f / (double)SIM_HZ);
                if (scene.sceneTicks < scene.sceneLength) {
                    fprintf(stderr, "frame %ld: the ending started early\n", f);
                    return 1;
                }
            }
            lastEnding = scene.ending;
        }
        if (scene.blackout == SIM_HZ) {
            fprintf(stderr, "  blackout at %.1f s (%.1f s after the walk-off began)\n",
                    f / (double)SIM_HZ, (f - walkoffAt) / (double)SIM_HZ);
            if (scene.sleighOn && flights != 1) {
                fprintf(stderr, "frame %ld: scene ended after %d sleigh flights, not 1\n", f, flights);
                return 1;
            }
            flights = 0;
        }
        if (scene.blackout == 1) lastGiftPrint = 1;
        if (lastGiftPrint && scene.blackout == 0) {
            fprintf(stderr, "new scene: gift %d\n", scene.gift);
            lastGiftPrint = 0;
        }
        if (!scene.ending && scene.sleighActive) {
            fprintf(stderr, "frame %ld: the sleigh flew before the scene's end\n", f);
            return 1;
        }
        if (scene.sleighActive || scene.ending >= END_TAKEOFF) {
            /* one Santa and one Rudolph: both gone from the ground once they have left */
            int i;
            for (i = 0; i < scene.nactors; i++) {
                Actor *a = &scene.actors[i];
                if ((a->kind == K_SANTA || i == scene.rudolph) && a->state != ST_AWAY) {
                    fprintf(stderr, "frame %ld: %s is on the ground while the sleigh is out\n", f,
                            a->kind == K_SANTA ? "Santa" : "Rudolph");
                    return 1;
                }
            }
        }
        if (scene.rudolph < 0 && scene.blackout == 0) {
            fprintf(stderr, "frame %ld: a scene with no Rudolph\n", f);
            return 1;
        }
        scene_render(&scene, fb[cur]);
        present_diff(0, fb[cur ^ 1], fb[cur]);
        if (memcmp(screen, fb[cur], FB_SIZE) != 0) {
            { int i = 0; while (i < FB_SIZE && screen[i] == fb[cur][i]) i++; fprintf(stderr, "frame %ld: screen differs at x %d y %d: screen %d fb %d prev %d\n", f, i % FB_W, i / FB_W, screen[i], fb[cur][i], fb[cur^1][i]); }
            return 1;
        }
        if (f > 0 && runs - before > maxRuns) maxRuns = runs - before;
        for (y = 0; y < FB_H; y++) {
            for (x = 0; x < FB_W; x++) {
                const unsigned char *rgb = PAL_RGB[screen[y * FB_W + x]];
                for (k = 0; k < scale; k++) memcpy(row + ((size_t)x * scale + k) * 3, rgb, 3);
            }
            for (k = 0; k < scale; k++) fwrite(row, 1, (size_t)FB_W * scale * 3, stdout);
        }
        cur ^= 1;
    }
    fprintf(stderr, "%ld frames ok, %ld runs total, at most %ld runs in one frame (after the first)\n",
            frames, runs, maxRuns);
    return 0;
}

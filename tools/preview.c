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
    long bunnyFrames = 0, bunnySeen = 0, visitorFrames = 0, scenes = 1, mrsScenes = 0, visitorScenes = 0;
    long trips = 0, pops = 0;
    unsigned char lastState[MAX_ACTORS];
    memset(lastState, 0xFF, sizeof lastState);
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
        /* NP_FORCE: 1 Mrs Claus, 2 the nutcracker, 4 the jack-in-the-box, 8 frequent
           trips into the workshop (bits add) */
        if (getenv("NP_FORCE")) scene.force = (short)atoi(getenv("NP_FORCE"));
        /* NP_MOON=0..255: the moon test module at that phase; NP_MOONRUN=1 runs it */
        if (getenv("NP_MOON")) {
            scene.moonTest = getenv("NP_MOONRUN") ? 2 : 1;
            scene.moonSet = (short)(atoi(getenv("NP_MOON")) & 255);
        }
    }
    scene_init(&scene, seed, snow, change, crowd, sleigh);
#define COUNT_SCENE() do { int q, m = 0, v = 0; \
        for (q = 0; q < scene.nactors; q++) { \
            if (scene.actors[q].kind == K_MRSCLAUS) m = 1; \
            if (scene.actors[q].kind == K_NUTCRACKER || scene.actors[q].kind == K_JACK) v = 1; } \
        mrsScenes += m; visitorScenes += v; } while (0)
    COUNT_SCENE();
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
            scenes++;
            COUNT_SCENE();
            memset(lastState, 0xFF, sizeof lastState);
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
        {
            /* the comings and goings: counted, and kept in bounds */
            int i;
            for (i = 0; i < scene.nactors && !scene.blackout; i++) {
                Actor *a = &scene.actors[i];
                short w = spr_w(actor_sprite(a));
                if (a->kind == K_BUNNY) {
                    bunnyFrames++;
                    if (a->state != ST_AWAY) bunnySeen++;
                }
                if ((a->kind == K_NUTCRACKER || a->kind == K_JACK) && a->state != ST_AWAY) visitorFrames++;
                if (a->state == ST_INSIDE && lastState[i] != ST_INSIDE) {
                    static const char *const WHO[] = { "Santa", "an elf", "an elf", "", "", "Mrs Claus" };
                    fprintf(stderr, "  %s goes into the workshop at %.1f s\n",
                            a->kind <= K_MRSCLAUS ? WHO[a->kind] : "someone", f / (double)SIM_HZ);
                    trips++;
                }
                if (a->state == ST_POP && lastState[i] != ST_POP) pops++;
                lastState[i] = a->state;
                if ((a->state == ST_WALK || a->state == ST_REST) && a->kind != K_PARTRIDGE &&
                    !is_flyer(a) && (a->x16 < 0 || a->x16 > ((FB_W - w) << 4))) {
                    fprintf(stderr, "frame %ld: actor %d (kind %d, state %d, x16 %d, w %d) is off the screen while walking\n",
                            f, i, a->kind, a->state, a->x16, w);
                    return 1;
                }
                if (a->state != ST_AWAY && !is_flyer(a) && a->kind != K_PARTRIDGE &&
                    (a->base < GROUND_TOP || a->base >= FB_H)) {
                    fprintf(stderr, "frame %ld: actor %d (kind %d) stands on row %d\n", f, i, a->kind, a->base);
                    return 1;
                }
            }
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
    fprintf(stderr, "bunnies in sight %.0f%% of the time; %ld scenes, Mrs Claus in %ld, a visitor in %ld "
            "(in sight %.0f s); %ld trips into the workshop; the jack popped %ld times\n",
            bunnyFrames ? 100.0 * bunnySeen / bunnyFrames : 0.0, scenes, mrsScenes, visitorScenes,
            visitorFrames / (double)SIM_HZ, trips, pops);
    return 0;
}

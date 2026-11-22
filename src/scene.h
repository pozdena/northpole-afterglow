/* North Pole: the scene, in plain portable C.
 *
 * Nothing in here knows about the Mac. The scene lives in a 160 x 120 "Atari" frame
 * buffer of palette indices (see sprites_gen.h); the module scales it up to the screen
 * and the desktop preview writes it out as images. Both include scene.c directly.
 *
 * Rules that keep this runnable as a 68k code resource:
 *   - no globals (everything lives in Scene, which the host allocates),
 *   - no pointers in constant tables (they would need relocating),
 *   - no libc.
 */
#ifndef SCENE_H
#define SCENE_H

typedef unsigned int u32;      /* 32 bits on the 68k (gcc, no -mshort) and on the host */

#define FB_W 160
#define FB_H 120
#define FB_SIZE (FB_W * FB_H)

#define SIM_HZ 30              /* scene_step() is called this many times a second */

#define SKY_BOTTOM 44          /* stars, moon, sleigh live above this */
#define GROUND_TOP 50          /* the snowy ground band runs from here ... */
#define GROUND_BOTTOM 118      /* ... to here (last row a thing can stand on) */

#define MAX_PROPS 18
#define MAX_ACTORS 26            /* the crowd plus up to twelve of one gift */
#define MAX_EGGS 10
#define MAX_AIR 90             /* the most flakes a storm keeps in the air */
#define MAX_FLAKES 110         /* falling plus the ones resting on the ground */
#define MAX_STARS 36
#define MAX_PARTICLES 44
#define MAX_DRAW (MAX_PROPS + MAX_ACTORS)

typedef struct {
    unsigned char spr;
    short x, y;                /* top left */
} Prop;

typedef struct {
    unsigned char kind, dir, state, frame;   /* dir: 0 = facing right, 1 = left */
    short x16;                 /* x of the left edge, 1/16 px */
    short base;                /* the row its feet stand on */
    short speed;               /* 1/16 px per tick */
    short timer, anim, hop;
    short key;                 /* depth to sort by when not its feet's row (perched up on
                                  something sorts with that thing); 0 = base */
    short tx, ty, sy;          /* a bird: where it is flying to (tx < 0: away off an edge),
                                  and the row it took off from; someone leaving the
                                  workshop: where they are walking to */
    short stay;                /* a bunny or a visitor: ticks left in its visit, or away;
                                  someone standing about: ticks till the next fidget */
    unsigned char slots[4];
} Actor;

typedef struct {
    short x16, y16, vy;
    short land;                /* row where it melts */
    short rest;                /* ticks left lying on the ground */
    unsigned char phase, live; /* live: 0 none, 1 falling, 2 landed */
} Flake;

typedef struct {
    short x16, y16, vx, vy;
    short life, kind;
} Particle;

typedef struct {
    unsigned char x, y, phase, bright;
} Star;

typedef struct {
    short x, y, life;          /* a goose's egg, lying in the snow for a while */
} Egg;

typedef struct Scene {
    unsigned long rng;
    unsigned long tick;        /* ticks since the screen saver started */

    unsigned long clock;       /* the Mac's clock: local seconds since 1 Jan 1904, 0 = unknown.
                                  The host keeps it current; each new scene reads it. */
    short moonPhase;           /* 0 new .. 128 full .. 255, from the clock */
    short gift;                /* which of the twelve days' gifts is out, 0 = none */
    long paradeTimer;          /* ticks until a gift that crosses the screen sets off */
    short pond;                /* the pond prop's index, -1 if none */
    short pearTree;            /* the pear tree prop's index, -1 if none */
    short flockState;          /* the doves' or calling birds': away, flying, perched */
    short flockCruise;         /* the row they fly at */
    long flockTimer;
    short giftForce;           /* testing only: always show this gift (0 = by the date) */
    short giftNow;             /* the preview module's switch: the gift is out from the
                                  scene's start, and a parade keeps coming back */
    short paradeGap;           /* px between the members of a parade */
    short previewDay;          /* the preview module: 1..12, the day of Christmas it pretends
                                  it is (its date slider); 0 = the real date */
    unsigned long sceneClock;  /* the clock this scene was built for */
    Egg eggs[MAX_EGGS];

    /* settings, 0..100 sliders as After Dark hands them over */
    short snowSetting, crowdSetting, sleighOn;
    long sceneLength;          /* ticks between scene changes */

    long sceneTicks;           /* ticks into the current scene */
    short ending;              /* the scene's last act, see step_ending; 0 = not yet */
    long endTimer;
    short blackout;            /* ticks of empty screen left between scenes */

    Prop props[MAX_PROPS];
    short nprops;
    Actor actors[MAX_ACTORS];
    short nactors;

    Star stars[MAX_STARS];
    short nstars;
    short moonX, moonY;

    short snowing;             /* how many flakes the sky wants in the air right now */
    long snowRate, snowRateStep;   /* new flakes per tick, 1/4096ths, eased toward snowing */
    long snowRateGoal, snowAcc;
    short wind;                /* 1/16 px per tick */
    short cloudIn, cloudOut;   /* the cloud cover's leading and trailing edges, 1/16 px */
    short cloudFrom;           /* 0: clouds come from the left, 1: from the right */
    short treeSnow;            /* snow settled on the trees, 0 (none yet) .. 4096 (all of
                                  it); it builds while it snows and stays till the scene ends */
    long weatherTimer;
    Flake flakes[MAX_FLAKES];

    short chimneyX, chimneyY;  /* -1 when there is no workshop */
    short workshop;            /* the workshop prop's index, -1 if none */
    short doorUser;            /* who is going in or out of the workshop, -1 if no one */
    short doorOpen;            /* ticks the door stays open */
    long doorTimer;            /* ticks till someone may next go in */
    short force;               /* testing only: 1 Mrs Claus, 2 the nutcracker, 4 the
                                  jack-in-the-box, 8 frequent trips to the workshop */
    short smokeTimer;
    Particle parts[MAX_PARTICLES];

    short rudolph;             /* the actor with the red nose; he leads the sleigh */
    short sleighActive, sleighDir, sleighY;
    short sleighX16, sleighSpeed;

    short shootActive, shootLife;
    short shootX16, shootY16, shootVX, shootVY;
    long shootTimer;

    /* scratch for depth sorting */
    short drawKey[MAX_DRAW];
    short drawIdx[MAX_DRAW];
} Scene;

/* Fresh scene. seed: anything that differs per run. Settings are the slider values. */
static void scene_init(Scene *s, unsigned long seed, short snow, short changeSetting,
                       short crowd, short sleigh);
/* Advance one tick (1 / SIM_HZ s). */
static void scene_step(Scene *s);
/* Draw the current state into fb (FB_SIZE palette indices). */
static void scene_render(Scene *s, unsigned char *fb);

#endif

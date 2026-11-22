/* North Pole: scene simulation and rendering. See scene.h for the ground rules. */
#include "scene.h"
#include "sprites_gen.h"

enum { K_SANTA, K_ELF, K_ELFGIFT, K_DEER, K_BUNNY, K_MRSCLAUS,
       K_NUTCRACKER, K_JACK,     /* the visitors: one or the other drops by in some scenes */
       /* the twelve days' gifts (the partridge sits in its pear tree, a prop) */
       K_PARTRIDGE, K_BIRD, K_HEN, K_RING, K_GOOSE, K_SWAN, K_MAID, K_LADY, K_LORD, K_PIPER, K_DRUMMER };
/* At the end of a scene Santa and Rudolph EXIT (walk off one edge, at their own pace) and
   are AWAY (flying the sleigh): there is one Santa and one Rudolph. */
enum { ST_WALK, ST_REST, ST_EXIT, ST_AWAY,
       ST_WAIT, ST_PARADE,       /* a gift that crosses the screen once: waiting, crossing */
       ST_ENTER, ST_LEAVE,       /* a bunny or a visitor coming on from an edge, going off one */
       ST_POP,                   /* the jack is out of his box */
       /* a trip into the workshop: to the door, at it, inside, and out again */
       ST_TODOOR, ST_ATDOOR, ST_INSIDE, ST_FROMDOOR };

/* the workshop's door, in its sprite: columns 19..25, rows 15..24 */
#define DOOR_LEFT 19
#define DOOR_RIGHT 25
#define DOOR_TOP 15
#define DOOR_BOTTOM 24
/* the scene's last act: Santa and Rudolph walk off, a beat, the sleigh flies over, a few
   seconds of quiet, then the blackout and a new scene */
enum { END_NONE, END_WALKOFF, END_TAKEOFF, END_FLYING, END_QUIET };
enum { P_SMOKE, P_SPARKLE };

#define SECONDS(n) ((long)(n) * SIM_HZ)
#define FLAKE_LIFE 220      /* ticks a flake takes to fall, on average */

/* Clouds roll across the sky from the upwind side as the snow starts, and roll on the
   same way when it stops. Each pixel of the sky fades over CLOUD_EDGE px of the front. */
#define CLOUD_EDGE 64
#define CLOUD_FULL ((FB_W + CLOUD_EDGE) << 4)
#define CLOUD_SPEED (CLOUD_FULL / SECONDS(8))

/* ------------------------------------------------------------------ small helpers */

static void fill_bytes(unsigned char *p, unsigned char v, unsigned short n)
{
    u32 v4 = v * 0x01010101U;
    u32 *q;
    while (n && ((unsigned long)p & 3)) { *p++ = v; n--; }
    q = (u32 *)p;
    while (n >= 16) { q[0] = v4; q[1] = v4; q[2] = v4; q[3] = v4; q += 4; n -= 16; }
    p = (unsigned char *)q;
    while (n--) *p++ = v;
}

static unsigned short rnd16(Scene *s)
{
    unsigned long x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return (unsigned short)(x >> 8);
}

/* 0 .. n-1 */
static short rnd(Scene *s, unsigned short n)
{
    return (short)(((unsigned long)rnd16(s) * n) >> 16);
}

/* lo .. hi inclusive */
static short rnd_range(Scene *s, short lo, short hi)
{
    return (short)(lo + rnd(s, (unsigned short)(hi - lo + 1)));
}

/* ------------------------------------------------------------------ the calendar */

/* The Mac's clock (local seconds since 1 Jan 1904) as a month 1..12 and day 1..31.
   Every fourth year is a leap year, which holds from 1904 to 2099. */
static const unsigned char MONTH_LEN[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static void clock_date(unsigned long clock, short *year, short *month, short *day)
{
    unsigned long days = clock / 86400UL;
    short y = 1904, m;
    for (;;) {
        unsigned short n = (y & 3) ? 365 : 366;
        if (days < n) break;
        days -= n;
        y++;
    }
    for (m = 0; m < 11; m++) {
        unsigned short n = (unsigned short)(MONTH_LEN[m] + (m == 1 && (y & 3) == 0));
        if (days < n) break;
        days -= n;
    }
    *year = y;
    *month = (short)(m + 1);
    *day = (short)(days + 1);
}

/* The other way: 9 pm on a date, as the clock would read it. */
static unsigned long date_clock(short year, short month, short day)
{
    unsigned long days = 0;
    short y, m;
    for (y = 1904; y < year; y++) days += (y & 3) ? 365 : 366;
    for (m = 0; m < month - 1; m++) days += MONTH_LEN[m] + (m == 1 && (year & 3) == 0);
    days += (unsigned long)(day - 1);
    return days * 86400UL + 21UL * 3600UL;
}

/* Which of the twelve days of Christmas it is, counting down to Christmas: the
   partridge on 14 December, the twelve drummers on Christmas Day. 0 outside them. */
static short twelve_days_day(unsigned long clock)
{
    short year, month, day;
    if (!clock) return 0;
    clock_date(clock, &year, &month, &day);
    return (month == 12 && day >= 14 && day <= 25) ? (short)(day - 13) : 0;
}

/* The moon's phase, 0 new .. 128 full .. 255, from the mean length of a lunar month and
   a known new moon (6 Jan 2000, 18:14). Good to within a day or so, plenty for 9 px. */
#define SYNODIC_SECS 2551443UL
#define NEW_MOON_2000 3030113640UL
static short moon_phase(unsigned long clock)
{
    unsigned long d;
    if (clock >= NEW_MOON_2000) {
        d = (clock - NEW_MOON_2000) % SYNODIC_SECS;
    } else {
        d = (NEW_MOON_2000 - clock) % SYNODIC_SECS;
        if (d) d = SYNODIC_SECS - d;
    }
    return (short)(((d * 256UL) / SYNODIC_SECS) & 255);
}

static short spr_w(short spr) { return SPR_DIM[spr][0]; }
static short spr_h(short spr) { return SPR_DIM[spr][1]; }

static const signed char WIGGLE[16] = { 0, 2, 3, 4, 4, 4, 3, 2, 0, -2, -3, -4, -4, -4, -3, -2 };
static const unsigned char LIGHTS[6] = { C_RED, C_YELLOW, C_BLUE, C_GREEN, C_MAGENTA, C_CYAN };

static void plot(unsigned char *fb, short x, short y, unsigned char c)
{
    if ((unsigned short)x < FB_W && (unsigned short)y < FB_H)
        fb[y * FB_W + x] = c;
}

/* ------------------------------------------------------------------ drawing sprites */

static void blit(Scene *s, unsigned char *fb, short spr, short x, short y, short flip,
                 const unsigned char *slots)
{
    short w = spr_w(spr), h = spr_h(spr);
    unsigned short off = (unsigned short)((SPR_DIM[spr][2] << 8) | SPR_DIM[spr][3]);
    const unsigned char *src = SPR_PX + off;
    short r, c;
    unsigned short slowTick = (unsigned short)(s->tick >> 4);

    for (r = 0; r < h; r++, src += w) {
        short yy = y + r;
        unsigned char *row;
        if ((unsigned short)yy >= FB_H) continue;
        row = fb + yy * FB_W;
        for (c = 0; c < w; c++) {
            unsigned char px = src[c];
            short xx;
            if (!px) continue;
            xx = flip ? (short)(x + w - 1 - c) : (short)(x + c);
            if ((unsigned short)xx >= FB_W) continue;
            if (px < PX_SLOT) {
                row[xx] = (unsigned char)(px - 1);
            } else if (px < PX_TWINKLE) {
                row[xx] = slots ? slots[px - PX_SLOT] : C_WHITE;
            } else if (px == PX_SETTLED || px >= PX_SETTLED_ON) {
                /* snow settles on a tree a pixel at a time, in an order fixed per tree,
                   over the branch tips (or nothing) it covers */
                unsigned short k = (unsigned short)(c * 181 + r * 97 + x * 59);
                k ^= (unsigned short)(k >> 5);
                k = (unsigned short)(k * 0x2F);
                k ^= (unsigned short)(k >> 7);
                if ((short)(k & 4095) < s->treeSnow) row[xx] = C_WHITE;
                else if (px >= PX_SETTLED_ON) row[xx] = (unsigned char)(px - PX_SETTLED_ON);
            } else if (px == PX_TWINKLE) {
                /* each bulb steps through the colors on its own phase */
                unsigned short k = (unsigned short)(xx * 3 + yy * 5 + slowTick);
                row[xx] = LIGHTS[k % 6];
            } else {
                /* a lit window: warm, with the odd flicker */
                unsigned short k = (unsigned short)((s->tick + (unsigned long)xx * 37) & 255);
                row[xx] = (k < 6) ? C_ORANGE : C_YELLOW;
            }
        }
    }
}

/* ------------------------------------------------------------------ building a scene */

static short overlaps(Scene *s, short x, short y, short w, short h)
{
    short i;
    for (i = 0; i < s->nprops; i++) {
        Prop *p = &s->props[i];
        short pw = spr_w(p->spr), ph = spr_h(p->spr);
        if (x < p->x + pw + 2 && p->x < x + w + 2 && y < p->y + ph && p->y < y + h)
            return 1;
    }
    return 0;
}

/* Try to stand a prop somewhere in the ground band. Returns its index or -1. */
static short place_prop(Scene *s, short spr, short xlo, short xhi)
{
    short w = spr_w(spr), h = spr_h(spr), tries;
    if (s->nprops >= MAX_PROPS) return -1;
    if (xhi > FB_W - w) xhi = FB_W - w;
    for (tries = 0; tries < 60; tries++) {
        short x = rnd_range(s, xlo, xhi);
        short base = rnd_range(s, GROUND_TOP + 10, GROUND_BOTTOM);
        short y = base - h + 1;
        if (y < 18) continue;
        if (overlaps(s, x, y, w, h)) continue;
        s->props[s->nprops].spr = (unsigned char)spr;
        s->props[s->nprops].x = x;
        s->props[s->nprops].y = y;
        return s->nprops++;
    }
    return -1;
}

/* Stand a prop with its feet on row base and its left edge at x, if the spot is free. */
static short place_prop_at(Scene *s, short spr, short x, short base)
{
    short w = spr_w(spr), h = spr_h(spr), y = (short)(base - h + 1);
    if (s->nprops >= MAX_PROPS || x < 0 || x > FB_W - w || y < 18 || base > GROUND_BOTTOM)
        return -1;
    if (overlaps(s, x, y, w, h)) return -1;
    s->props[s->nprops].spr = (unsigned char)spr;
    s->props[s->nprops].x = x;
    s->props[s->nprops].y = y;
    return s->nprops++;
}

/* Lamp posts: one anywhere, or two that line up along the same row, well apart, as
   lamps along a lane would. */
static void place_lamps(Scene *s, short n)
{
    short w = spr_w(SPR_LAMP), tries;
    if (n >= 2) {
        for (tries = 0; tries < 40; tries++) {
            short gap = rnd_range(s, 56, 100);
            short x = rnd_range(s, 2, (short)(FB_W - 2 - w - gap));
            short base = rnd_range(s, GROUND_TOP + 14, GROUND_BOTTOM);
            if (place_prop_at(s, SPR_LAMP, x, base) < 0) continue;
            if (place_prop_at(s, SPR_LAMP, (short)(x + gap), base) >= 0) return;
            s->nprops--;                    /* the second did not fit: try another row */
        }
    }
    if (n >= 1) place_prop(s, SPR_LAMP, 0, FB_W);
}

static short actor_sprite(const Actor *a)
{
    switch (a->kind) {
    case K_SANTA:   return a->frame ? SPR_SANTA1 : SPR_SANTA0;
    case K_ELF:     return a->frame ? SPR_ELF1 : SPR_ELF0;
    case K_ELFGIFT: return a->frame ? SPR_ELFGIFT1 : SPR_ELFGIFT0;
    case K_DEER:
        if (a->state == ST_REST) return SPR_DEERGRAZE;
        return a->frame ? SPR_DEER1 : SPR_DEER0;
    case K_BUNNY:   return a->frame ? SPR_BUNNY1 : SPR_BUNNY0;
    case K_MRSCLAUS: return a->frame ? SPR_MRSCLAUS1 : SPR_MRSCLAUS0;
    case K_NUTCRACKER: return a->frame ? SPR_NUT1 : SPR_NUT0;
    case K_JACK:
        if (a->state != ST_POP) return SPR_JACKBOX;
        return a->frame ? SPR_JACKUP1 : SPR_JACKUP0;
    case K_BIRD:
        if (a->state == ST_REST) return a->frame ? SPR_BIRDSIT1 : SPR_BIRDSIT0;
        return a->frame ? SPR_BIRD1 : SPR_BIRD0;
    case K_HEN:     return a->state == ST_REST ? SPR_HENPECK : SPR_HEN0;
    case K_RING:    return a->frame == 0 ? SPR_RING0 : (a->frame == 1 ? SPR_RING1 : SPR_RING2);
    case K_GOOSE:   return a->frame ? SPR_GOOSE1 : SPR_GOOSE0;
    case K_SWAN:    return a->frame ? SPR_SWAN1 : SPR_SWAN0;
    case K_MAID:    return a->frame ? SPR_MAID1 : SPR_MAID0;
    case K_LADY:    return a->frame ? SPR_LADY1 : SPR_LADY0;
    case K_LORD:    return a->frame ? SPR_LORD1 : SPR_LORD0;
    case K_PIPER:   return a->frame ? SPR_PIPER1 : SPR_PIPER0;
    case K_PARTRIDGE: {
        static const unsigned char POSE[4] = { SPR_PARTRIDGE, SPR_PARTRIDGEBOB,
                                               SPR_PARTRIDGEFLAP0, SPR_PARTRIDGEFLAP1 };
        return POSE[a->frame & 3];
    }
    default:        return a->frame ? SPR_DRUMMER1 : SPR_DRUMMER0;
    }
}

static void add_actor(Scene *s, short kind)
{
    static const unsigned char SUITS[4][2] = {
        { C_GREEN, C_RED }, { C_RED, C_GREEN }, { C_DKGREEN, C_YELLOW }, { C_GREEN, C_WHITE }
    };
    Actor *a;
    short w, pick;
    if (s->nactors >= MAX_ACTORS) return;
    a = &s->actors[s->nactors++];
    a->kind = (unsigned char)kind;
    a->dir = (unsigned char)rnd(s, 2);
    a->state = ST_WALK;
    a->frame = 0;
    a->anim = rnd(s, 16);
    a->hop = 0;
    a->base = rnd_range(s, GROUND_TOP + 14, GROUND_BOTTOM);
    switch (kind) {
    case K_SANTA:   a->speed = 5; break;
    case K_ELF:     a->speed = 8; break;
    case K_ELFGIFT: a->speed = 7; break;
    case K_DEER:    a->speed = 6; break;
    case K_BUNNY:   a->speed = 10; break;
    case K_MRSCLAUS: a->speed = 4; break;
    case K_NUTCRACKER: a->speed = 5; break;
    case K_JACK:    a->speed = 6; break;
    case K_BIRD:    a->speed = 9; break;
    case K_HEN:     a->speed = 4; break;
    case K_RING:    a->speed = 6; break;
    case K_GOOSE:   a->speed = 4; break;
    case K_SWAN:    a->speed = 3; break;
    case K_LORD:    a->speed = 7; break;
    default:        a->speed = 5; break;     /* maids, ladies, pipers, drummers */
    }
    a->timer = rnd_range(s, 40, 200);
    pick = rnd(s, 4);
    a->slots[0] = SUITS[pick][0];
    a->slots[1] = SUITS[pick][1];
    a->key = 0;
    a->tx = -1;
    a->stay = rnd_range(s, 20, 80);
    a->slots[2] = C_DKBROWN;    /* a reindeer's nose ... */
    a->slots[3] = C_WHITE;
    w = spr_w(actor_sprite(a));
    a->x16 = (short)(rnd_range(s, 0, FB_W - w) << 4);
}

/* Line up a gift that crosses the screen once: they wait out of sight, then set off
   together (step_parade). Birds and rings fly; the rest walk in a line. */
static void add_parade(Scene *s, short kind, short count, short gap)
{
    short i, dir = rnd(s, 2);
    short row = rnd_range(s, GROUND_TOP + 18, GROUND_BOTTOM - 2);
    s->paradeGap = gap;
    for (i = 0; i < count && s->nactors < MAX_ACTORS; i++) {
        Actor *a = &s->actors[s->nactors];
        short w;
        add_actor(s, kind);
        a->state = ST_WAIT;
        a->dir = (unsigned char)dir;
        a->anim = 0;
        a->timer = (short)(i * 5);              /* each one's own phase */
        a->base = row;
        w = spr_w(actor_sprite(a));
        a->x16 = (short)((dir ? FB_W + i * gap : -(w + i * gap)) << 4);
    }
}

/* ------------------------------------------------------------------ the flock
   The doves and the calling birds behave like birds: they fly in, land on the ground to
   peck or perch on top of something (anything wide enough: a big tree, the workshop's roof),
   sit a while, then take off, to land somewhere else or fly off and come back later.
   The golden rings do the same in the sky: they float in, hang in an arc over a spot,
   spinning and sparkling, then drift on. */

enum { FL_AWAY, FL_FLYING, FL_PERCHED };

static short is_flyer(const Actor *a) { return a->kind == K_BIRD || a->kind == K_RING; }

static void add_flock(Scene *s, short count, short kind)
{
    short i;
    for (i = 0; i < count && s->nactors < MAX_ACTORS; i++) {
        Actor *a = &s->actors[s->nactors];
        add_actor(s, kind);
        a->state = ST_AWAY;
        a->timer = (short)(i * 5);
    }
    s->flockState = FL_AWAY;
    s->flockTimer = s->giftNow ? 1 : SECONDS(rnd_range(s, 1, 4));
}

static Particle *new_particle(Scene *s);

static void ring_sparkle(Scene *s, const Actor *a)
{
    Particle *p = new_particle(s);
    if (!p) return;
    p->kind = P_SPARKLE;
    p->x16 = (short)(a->x16 + (2 << 4));
    p->y16 = (short)((a->base - 2 - a->hop) << 4);
    p->vx = rnd_range(s, -2, 2);
    p->vy = rnd_range(s, 1, 3);
    p->life = rnd_range(s, 12, 22);
}

/* The topmost opaque row of a sprite in column c, or -1. */
static short top_row(short spr, short c)
{
    short w = spr_w(spr), h = spr_h(spr), r;
    const unsigned char *src = SPR_PX + ((SPR_DIM[spr][2] << 8) | SPR_DIM[spr][3]);
    for (r = 0; r < h; r++)
        if (src[r * w + c] && src[r * w + c] != PX_SETTLED) return r;
    return -1;
}

/* Pick where each bird of the flock will settle: side by side on top of a prop, or on
   the ground. Sets each bird's tx, ty and key. */
static void choose_spots(Scene *s)
{
    short i, n = 0, ground = rnd(s, 10) < 4, prop = -1;
    Actor *birds[MAX_ACTORS];
    for (i = 0; i < s->nactors; i++)
        if (is_flyer(&s->actors[i])) birds[n++] = &s->actors[i];
    if (n && birds[0]->kind == K_RING) {
        /* rings hang in the sky in a shallow arc, its ends a little lower */
        short cx = rnd_range(s, 8 + 4 * n, FB_W - 8 - 4 * n), cy = rnd_range(s, 8, SKY_BOTTOM - 16);
        for (i = 0; i < n; i++) {
            short o = (short)(2 * i - (n - 1));
            birds[i]->tx = (short)(cx + o * 4 - 2);
            birds[i]->ty = (short)(cy + (o < 0 ? -o : o));
            birds[i]->key = 0;
        }
        return;
    }
    if (!ground && s->nprops > 0) {
        short tries;
        for (tries = 0; tries < 8 && prop < 0; tries++) {
            short c = rnd(s, (unsigned short)s->nprops);
            /* wide enough for the whole flock to sit side by side */
            if (c != s->pond && spr_w(s->props[c].spr) >= 4 * n + 1) prop = c;
        }
    }
    if (prop >= 0) {
        Prop *p = &s->props[prop];
        short w = spr_w(p->spr), spacing = (short)(w / (n + 1)), hi = 999, lo = -999;
        if (spacing > 7) spacing = 7;
        if (spacing < 3) spacing = 3;
        for (i = 0; i < n; i++) {
            short c = (short)(w / 2 + ((2 * i - (n - 1)) * spacing) / 2), r;
            if (c < 0) c = 0;
            if (c > w - 1) c = (short)(w - 1);
            r = top_row(p->spr, c);
            if (r < 0) { prop = -1; break; }
            birds[i]->tx = (short)(p->x + c - 3);   /* the bird's middle over that column */
            birds[i]->ty = (short)(p->y + r - 1);   /* its feet on that top pixel */
            birds[i]->key = (short)(p->y + spr_h(p->spr) + 1);
            if (birds[i]->ty < 6) { prop = -1; break; }
            if (birds[i]->ty < hi) hi = birds[i]->ty;
            if (birds[i]->ty > lo) lo = birds[i]->ty;
        }
        if (lo - hi > 5) prop = -1;     /* a steep top: they would sit stacked, not side by side */
    }
    if (prop < 0) {
        short gx = rnd_range(s, 20, FB_W - 26), gy = rnd_range(s, GROUND_TOP + 10, GROUND_BOTTOM - 3);
        for (i = 0; i < n; i++) {
            birds[i]->tx = (short)(gx + (2 * i - (n - 1)) * 4 + rnd_range(s, -1, 1));
            birds[i]->ty = (short)(gy + rnd_range(s, -2, 2));
            birds[i]->key = 0;
        }
    }
}

/* the flock flies at a row above every spot it is heading for or leaving from */
static short pick_cruise(Scene *s)
{
    short i, lo = SKY_BOTTOM - 6;
    for (i = 0; i < s->nactors; i++) {
        Actor *a = &s->actors[i];
        if (!is_flyer(a)) continue;
        if (a->kind == K_RING) return rnd_range(s, 6, SKY_BOTTOM - 14);
        if (a->tx >= 0 && a->ty - 8 < lo) lo = (short)(a->ty - 8);
        if (a->state == ST_REST && a->base - 8 < lo) lo = (short)(a->base - 8);
    }
    if (lo < 6) lo = 6;
    return rnd_range(s, 4, lo);
}

static void step_flock(Scene *s)
{
    short i, n = 0, away = 0, resting = 0;
    for (i = 0; i < s->nactors; i++) {
        Actor *a = &s->actors[i];
        if (!is_flyer(a)) continue;
        n++;
        if (a->state == ST_AWAY) away++;
        if (a->state == ST_REST) resting++;
    }
    if (!n) return;

    if (s->flockState == FL_AWAY) {
        if (--s->flockTimer > 0) return;
        /* fly in from one side: mostly to land, sometimes just passing over */
        {
            short land = rnd(s, 4) != 0, side = rnd(s, 2), j = 0;
            if (land) choose_spots(s);
            else for (i = 0; i < s->nactors; i++) if (is_flyer(&s->actors[i])) s->actors[i].tx = -1;
            s->flockCruise = pick_cruise(s);
            for (i = 0; i < s->nactors; i++) {
                Actor *a = &s->actors[i];
                short w;
                if (!is_flyer(a)) continue;
                w = spr_w(actor_sprite(a));
                a->state = ST_PARADE;
                a->dir = (unsigned char)side;
                a->x16 = (short)((side ? FB_W + j * 10 : -(w + j * 10)) << 4);
                a->base = a->sy = (short)(s->flockCruise + ((j & 1) ? 4 : 0));
                a->anim = 0;
                j++;
            }
        }
        s->flockState = FL_FLYING;
    } else if (s->flockState == FL_FLYING) {
        if (away == n) {
            s->flockState = FL_AWAY;
            s->flockTimer = s->giftNow ? SECONDS(2) : SECONDS(rnd_range(s, 5, 15));
        } else if (resting == n) {
            s->flockState = FL_PERCHED;
            s->flockTimer = SECONDS(rnd_range(s, 8, 25));
        }
    } else {
        if (--s->flockTimer > 0) return;
        /* up and away: to somewhere else here, or off an edge */
        {
            short move = rnd(s, 5) < 2, dir = rnd(s, 2), j = 0;   /* more often off */
            if (move) choose_spots(s);
            s->flockCruise = pick_cruise(s);
            for (i = 0; i < s->nactors; i++) {
                Actor *a = &s->actors[i];
                if (!is_flyer(a)) continue;
                if (!move) {
                    a->tx = -1;
                    a->dir = (unsigned char)dir;
                }
                a->state = ST_PARADE;
                a->sy = a->base;
                a->anim = (short)(-j * 3);          /* not all at once */
                j++;
            }
        }
        s->flockState = FL_FLYING;
    }
}

/* one of the flock: flying (ST_PARADE) toward its spot or off an edge, or sitting
   (ST_REST); a ring spins and bobs and sparkles all the while */
static void step_bird(Scene *s, Actor *a)
{
    static const unsigned char SPIN[4] = { 0, 1, 2, 1 };
    short w = spr_w(actor_sprite(a));
    if (a->state == ST_AWAY) return;
    if (a->kind == K_RING && a->state == ST_REST) {
        short t = (short)(s->tick & 0x7FFF);
        a->frame = SPIN[((t >> 3) + a->timer) & 3];
        a->hop = (short)(WIGGLE[((t >> 2) + a->timer) & 15] / 2);
        if ((t & 15) == 0 && rnd(s, 4) == 0) ring_sparkle(s, a);
        return;
    }
    if (a->state == ST_REST) {
        if (a->anim > 0) {                          /* pecking, or looking about */
            a->anim--;
            a->frame = (unsigned char)((a->anim & 7) >= 4);
            return;
        }
        a->frame = 0;
        if (--a->timer <= 0) {
            short roll = rnd(s, 10);
            if (roll < 6) a->anim = rnd(s, 2) ? 8 : 16;
            else a->dir ^= 1;
            a->timer = rnd_range(s, 20, 90);
        }
        return;
    }
    if (a->anim < 0) {                              /* waiting its turn to take off */
        a->anim++;
        return;
    }
    {
        short t = ++a->anim, cruise = s->flockCruise, climbBase, glideBase;
        long gone = ((long)t * a->speed) >> 4;      /* px flown since setting off */
        if (a->kind == K_RING) {
            a->frame = SPIN[((t >> 3) + a->timer) & 3];
            if ((t & 15) == 0 && rnd(s, 3) == 0) ring_sparkle(s, a);
        } else {
            a->frame = (unsigned char)(((t + a->timer) >> 2) & 1);
        }
        if (a->tx >= 0) {
            short dx16 = (short)((a->tx << 4) - a->x16);
            short d = (short)((dx16 < 0 ? -dx16 : dx16) >> 4);
            if ((dx16 < 0 ? -dx16 : dx16) <= a->speed) {
                /* landed */
                a->x16 = (short)(a->tx << 4);
                a->base = a->ty;
                a->state = ST_REST;
                a->frame = 0;
                a->hop = 0;
                a->anim = 0;
                if (a->kind != K_RING) a->timer = rnd_range(s, 10, 60);   /* a ring keeps its phase */
                return;
            }
            a->dir = (unsigned char)(dx16 < 0);
            a->x16 += dx16 < 0 ? -a->speed : a->speed;
            /* glide down over the last 30 px */
            glideBase = d < 30 ? (short)(a->ty + ((cruise - a->ty) * d) / 30) : cruise;
        } else {
            a->x16 += a->dir ? -a->speed : a->speed;
            glideBase = cruise;
            if (a->dir ? a->x16 < -(w << 4) : a->x16 > (FB_W << 4)) {
                a->state = ST_AWAY;
                return;
            }
        }
        /* climb up off the perch over the first 30 px */
        climbBase = gone < 30 ? (short)(a->sy + ((cruise - a->sy) * gone) / 30) : cruise;
        a->base = climbBase > glideBase ? climbBase : glideBase;
        a->hop = (a->base == cruise || a->kind == K_RING)
            ? (short)(WIGGLE[((t >> 2) + a->timer) & 15] / 2) : 0;
    }
}

/* The twelve days of Christmas, in a countdown from 14 December: three scenes in four on
   day d have one of the gifts up to d, the day's own two times in three. */
static short pick_gift(Scene *s)
{
    short day = twelve_days_day(s->sceneClock);
    if (s->giftForce) return s->giftForce;
    if (s->previewDay) return day;              /* the preview shows each day's own gift */
    if (!day || rnd(s, 4) == 0) return 0;
    if (day == 1 || rnd(s, 3) != 0) return day;
    return (short)(1 + rnd(s, (unsigned short)(day - 1)));
}

static void add_gift(Scene *s)
{
    short i;
    static const unsigned char GOWNS[6] = { C_RED, C_MAGENTA, C_CYAN, C_YELLOW, C_PINK, C_LTBLUE };
    static const unsigned char HAIR[4] = { C_YELLOW, C_BROWN, C_DKBROWN, C_ORANGE };
    static const unsigned char CAPES[4] = { C_RED, C_MAGENTA, C_BLUE, C_DKRED };
    static const unsigned char TUNICS[4] = { C_YELLOW, C_WHITE, C_CYAN, C_GOLD };
    short first = s->nactors;

    switch (s->gift) {
    case 1:                                                        /* the partridge, perched */
        if (s->pearTree >= 0 && s->nactors < MAX_ACTORS) {
            Prop *p = &s->props[s->pearTree];
            Actor *a = &s->actors[s->nactors];
            add_actor(s, K_PARTRIDGE);
            a->state = ST_REST;
            a->frame = 0;
            a->anim = 0;
            a->hop = 0;
            a->timer = rnd_range(s, 30, 90);
            a->x16 = (short)((p->x + 4) << 4);
            a->base = (short)p->y;                 /* on the leaves; snow settles round its feet */
            a->key = (short)(p->y + spr_h(SPR_PEARTREE));   /* drawn with its tree */
        }
        break;
    case 2:  add_flock(s, 2, K_BIRD); break;                               /* turtle doves */
    case 3:  for (i = 0; i < 3; i++) add_actor(s, K_HEN); break;   /* French hens */
    case 4:  add_flock(s, 4, K_BIRD); break;                               /* calling birds */
    case 5:  add_flock(s, 5, K_RING); break;                       /* golden rings */
    case 6:  for (i = 0; i < 6; i++) add_actor(s, K_GOOSE); break;
    case 7:                                                        /* swans, on the pond */
        for (i = 0; i < 7 && s->pond >= 0 && s->nactors < MAX_ACTORS; i++) {
            Prop *p = &s->props[s->pond];
            Actor *a = &s->actors[s->nactors];
            add_actor(s, K_SWAN);
            a->x16 = (short)((p->x + 4 + rnd(s, (unsigned short)(spr_w(SPR_POND) - 15))) << 4);
            a->base = (short)(p->y + 4 + rnd(s, 6));
        }
        break;
    case 8:  add_parade(s, K_MAID, 8, 11); break;
    case 9:  add_parade(s, K_LADY, 9, 11); break;
    case 10: add_parade(s, K_LORD, 10, 11); break;
    case 11: add_parade(s, K_PIPER, 11, 11); break;
    case 12: add_parade(s, K_DRUMMER, 12, 11); break;
    default: break;
    }
    for (i = first; i < s->nactors; i++) {
        Actor *a = &s->actors[i];
        if (a->kind == K_BIRD) {
            /* doves are white with pink feet; calling birds, blue with a yellow beak */
            a->slots[0] = s->gift == 2 ? C_WHITE : C_BLUE;
            a->slots[1] = s->gift == 2 ? C_SNOWSHADE : C_LTBLUE;
            a->slots[2] = s->gift == 2 ? C_PINK : C_YELLOW;
        } else if (a->kind == K_LORD) {
            a->slots[0] = CAPES[rnd(s, 4)];
            a->slots[1] = TUNICS[rnd(s, 4)];
        }
        if (a->kind >= K_MAID) {
            /* the people: about one in three darker skinned, one in six a little, one in
               six more */
            static const unsigned char SKINS[6] = { C_SKIN, C_SKIN, C_SKIN, C_SKIN, C_SKIN2, C_SKIN3 };
            a->slots[2] = SKINS[rnd(s, 6)];
        }
        if (a->kind == K_LADY) {
            a->slots[0] = GOWNS[rnd(s, 6)];
            /* blonde, red or brown hair; a darker skinned dancer's is brown or dark brown */
            a->slots[1] = a->slots[2] == C_SKIN ? HAIR[rnd(s, 4)] : HAIR[1 + rnd(s, 2)];
        }
    }
    /* a parade sets off some seconds into the scene (and comes back now and then) */
    s->paradeTimer = s->giftNow ? 1 : SECONDS(rnd_range(s, 5, 30));
}

static void new_scene(Scene *s)
{
    short i, n, crowd = s->crowdSetting;
    short ws;

    s->nprops = 0;
    s->nactors = 0;
    s->sceneTicks = 0;
    s->chimneyX = s->chimneyY = -1;
    for (i = 0; i < MAX_PARTICLES; i++) s->parts[i].life = 0;
    for (i = 0; i < MAX_FLAKES; i++) s->flakes[i].live = 0;
    s->sleighActive = 0;
    s->shootActive = 0;
    s->snowRate = s->snowRateGoal = s->snowRateStep = s->snowAcc = 0;
    s->ending = END_NONE;
    s->endTimer = 0;
    s->rudolph = -1;
    s->pond = -1;
    s->pearTree = -1;
    s->paradeTimer = 0;
    for (i = 0; i < MAX_EGGS; i++) s->eggs[i].life = 0;
    s->sceneClock = s->clock;
    if (s->previewDay && s->clock) {
        /* the preview module: the day of the twelve its slider picks, this year */
        short year, month, day;
        clock_date(s->clock, &year, &month, &day);
        s->sceneClock = date_clock(year, 12, (short)(13 + s->previewDay));
    }
    s->gift = pick_gift(s);
    s->moonPhase = s->sceneClock ? moon_phase(s->sceneClock) : rnd(s, 256);

    /* the sky */
    s->moonX = rnd_range(s, 6, FB_W - 14);
    s->moonY = rnd_range(s, 2, 14);
    s->nstars = rnd_range(s, 22, MAX_STARS);
    for (i = 0; i < s->nstars; i++) {
        Star *st = &s->stars[i];
        st->x = (unsigned char)rnd(s, FB_W);
        st->y = (unsigned char)rnd_range(s, 1, SKY_BOTTOM - 4);
        st->phase = (unsigned char)rnd16(s);
        st->bright = (unsigned char)(rnd(s, 3) == 0);
    }

    /* a gift that needs its own scenery goes first, so it surely fits */
    s->pearTree = s->gift == 1 ? place_prop(s, SPR_PEARTREE, 0, FB_W) : -1;
    if (s->gift == 1 && s->pearTree < 0) s->gift = 0;
    if (s->gift == 7) {
        s->pond = place_prop(s, SPR_POND, 0, FB_W);
        if (s->pond < 0) s->gift = 0;
    }

    /* the workshop next: it is the biggest thing to fit */
    s->workshop = -1;
    s->doorUser = -1;
    s->doorOpen = 0;
    s->doorTimer = SECONDS(rnd_range(s, 15, 60));
    if (s->force & 8) s->doorTimer = SECONDS(3);
    if (rnd(s, 5) != 0) {
        ws = place_prop(s, SPR_WORKSHOP, 2, FB_W - 2);
        if (ws >= 0) {
            s->workshop = ws;
            s->chimneyX = s->props[ws].x + 25;
            s->chimneyY = s->props[ws].y;
        }
    }
    /* the lamps before the smaller things, so a pair has room to line up */
    place_lamps(s, rnd(s, 3));
    if (rnd(s, 2)) place_prop(s, SPR_TREE_BIG, 0, FB_W);
    n = rnd_range(s, 1, 3);
    for (i = 0; i < n; i++) place_prop(s, SPR_TREE_MID, 0, FB_W);
    n = rnd_range(s, 1, 4);
    for (i = 0; i < n; i++) place_prop(s, SPR_TREE_SMALL, 0, FB_W);
    if (rnd(s, 5) < 3) place_prop(s, SPR_SNOWMAN, 0, FB_W);
    if (rnd(s, 2)) place_prop(s, SPR_POLE, 0, FB_W);
    n = rnd(s, 3);
    for (i = 0; i < n; i++) place_prop(s, SPR_GIFTS, 0, FB_W);

    /* the folks */
    add_actor(s, K_SANTA);
    if (rnd(s, 4) == 0 || (s->force & 1)) add_actor(s, K_MRSCLAUS);
    n = 1 + crowd / 34;
    for (i = 0; i < n; i++) add_actor(s, rnd(s, 3) ? K_ELF : K_ELFGIFT);
    n = 1 + crowd / 50;
    for (i = 0; i < n; i++) add_actor(s, K_DEER);
    /* the first reindeer is Rudolph: he leaves with Santa at the end */
    for (i = 0; i < s->nactors; i++)
        if (s->actors[i].kind == K_DEER) {
            s->actors[i].slots[2] = C_RED;
            s->rudolph = i;
            break;
        }
    /* the bunnies come and go: each is about a third of the time in sight */
    n = 1 + crowd / 34;
    for (i = 0; i < n && s->nactors < MAX_ACTORS; i++) {
        Actor *a = &s->actors[s->nactors];
        add_actor(s, K_BUNNY);
        if (rnd(s, 3) == 0) {
            a->stay = (short)SECONDS(rnd_range(s, 10, 40));
        } else {
            a->state = ST_AWAY;
            a->stay = (short)SECONDS(rnd_range(s, 2, 80));
        }
    }
    /* now and then a visitor: a nutcracker, or a jack-in-the-box */
    if (rnd(s, 8) == 0 || (s->force & 6)) {
        short kind = (s->force & 6) ? ((s->force & 2) ? K_NUTCRACKER : K_JACK)
                                    : (rnd(s, 2) ? K_NUTCRACKER : K_JACK);
        if (s->nactors < MAX_ACTORS) {
            Actor *a = &s->actors[s->nactors];
            add_actor(s, kind);
            a->state = ST_AWAY;
            a->stay = (short)SECONDS((s->force & 6) ? 2 : rnd_range(s, 5, 45));
        }
    }
    add_gift(s);

    /* weather: maybe it is already snowing */
    s->wind = rnd_range(s, -3, 3);
    if (s->snowSetting > 0 && rnd(s, 100) < s->snowSetting) {
        s->snowing = 20 + (short)(((long)s->snowSetting * (MAX_AIR - 20)) / 100);
        s->weatherTimer = SECONDS(rnd_range(s, 30, 90));
    } else {
        s->snowing = 0;
        s->weatherTimer = SECONDS(rnd_range(s, 10, 120 - s->snowSetting));
    }
    /* already snowing: the clouds are already in, and the trees already white */
    s->cloudIn = s->snowing ? CLOUD_FULL : 0;
    s->treeSnow = s->snowing ? 4096 : 0;
    s->cloudOut = 0;
    s->cloudFrom = rnd(s, 2);

    s->shootTimer = SECONDS(rnd_range(s, 8, 30));
    s->smokeTimer = 0;
}

static void scene_init(Scene *s, unsigned long seed, short snow, short changeSetting,
                       short crowd, short sleigh)
{
    short minutes;
    s->rng = seed ? seed : 0x1234567UL;
    rnd16(s); rnd16(s);
    s->tick = 0;
    s->snowSetting = snow < 0 ? 0 : (snow > 100 ? 100 : snow);
    s->crowdSetting = crowd < 0 ? 0 : (crowd > 100 ? 100 : crowd);
    s->sleighOn = sleigh;
    minutes = 1 + changeSetting / 11;          /* slider 0..100 -> 1..10 minutes */
    if (minutes < 1) minutes = 1;
    if (minutes > 10) minutes = 10;
    s->sceneLength = SECONDS(60L * minutes);
    s->blackout = 0;
    new_scene(s);
}

/* ------------------------------------------------------------------ simulation */

static Particle *new_particle(Scene *s)
{
    short i;
    for (i = 0; i < MAX_PARTICLES; i++)
        if (s->parts[i].life <= 0) return &s->parts[i];
    return 0;
}

static void lay_egg(Scene *s, const Actor *a, short w)
{
    short i;
    for (i = 0; i < MAX_EGGS; i++) {
        Egg *e = &s->eggs[i];
        if (e->life > 0) continue;
        e->x = (short)((a->x16 >> 4) + (a->dir ? w - 3 : 1));   /* under the tail */
        e->y = a->base;
        e->life = (short)SECONDS(15);
        return;
    }
}

/* a gift crossing the screen once, then gone */
static void step_parade(Scene *s, Actor *a, short w)
{
    short t;
    a->x16 += a->dir ? -a->speed : a->speed;
    t = ++a->anim;
    switch (a->kind) {
    case K_LORD: {
        /* a-leaping: a bound every 20 ticks, legs apart in the air */
        short k = (short)((t + a->timer) % 20);
        a->hop = (short)((k * (20 - k)) / 16);
        a->frame = (unsigned char)(k > 3 && k < 17);
        break;
    }
    case K_LADY:
        /* a-dancing: arms up and down, a little bounce */
        a->frame = (unsigned char)(((t + a->timer) / 10) & 1);
        a->hop = a->frame;
        break;
    default:
        /* marching in step, all of them on the same beat */
        a->frame = (unsigned char)((s->tick >> 3) & 1);
        break;
    }
    if (a->dir ? a->x16 < -(w << 4) : a->x16 > (FB_W << 4)) a->state = ST_AWAY;
}

/* The partridge stays on its tree: it bobs its head, turns about, and now and then
   flutters up off its perch and settles back down, sometimes facing the other way. */
static void step_partridge(Scene *s, Actor *a)
{
    if (a->state == ST_WALK) {                  /* in the air */
        short t = ++a->anim;
        a->hop = (short)((t * (24 - t)) / 20);  /* up to 7 px */
        a->frame = (unsigned char)(2 + ((t >> 1) & 1));
        if (t >= 24) {
            a->state = ST_REST;
            a->hop = 0;
            a->frame = 0;
            a->anim = 0;
            if (rnd(s, 3) == 0) a->dir ^= 1;
            a->timer = rnd_range(s, 60, 180);
        }
        return;
    }
    if (a->anim > 0) {                          /* bobbing: down for 4 ticks, up for 4 */
        a->anim--;
        a->frame = (unsigned char)((a->anim & 7) >= 4);
        return;
    }
    a->frame = 0;
    if (--a->timer > 0) return;
    {
        short roll = rnd(s, 10);
        if (roll < 7) a->anim = rnd(s, 2) ? 8 : 16;      /* a bob or two */
        else if (roll < 9) a->dir ^= 1;                   /* look the other way */
        else { a->state = ST_WALK; a->anim = 0; }         /* now and then, a flutter */
    }
    a->timer = rnd_range(s, 25, 110);
}

/* Santa, the elves and Mrs Claus: they stand about, and go in and out of the workshop */
static short is_person(const Actor *a)
{
    return a->kind == K_SANTA || a->kind == K_ELF || a->kind == K_ELFGIFT || a->kind == K_MRSCLAUS;
}

/* ticks per step of a walker's legs */
static short walk_rate(const Actor *a)
{
    if (a->kind == K_ELF || a->kind == K_ELFGIFT) return 5;
    if (a->kind == K_SWAN) return 12;
    if (a->kind == K_NUTCRACKER) return 6;
    return 8;
}

/* Walk toward column tx (the left edge) and row ty, across and up or down the ground at
   once. Returns 1 on arrival. */
static short walk_to(Scene *s, Actor *a, short tx, short ty)
{
    short dx16 = (short)((tx << 4) - a->x16), moving = 0;
    if (dx16 > a->speed || dx16 < -a->speed) {
        a->dir = (unsigned char)(dx16 < 0);
        a->x16 += dx16 < 0 ? -a->speed : a->speed;
        moving = 1;
    } else {
        a->x16 = (short)(tx << 4);
    }
    if (a->base != ty) {
        if ((s->tick & 3) == 0) a->base += a->base < ty ? 1 : -1;
        moving = 1;
    }
    if (!moving) return 1;
    a->anim++;
    a->frame = (unsigned char)((a->anim / walk_rate(a)) & 1);
    return 0;
}

/* where someone stands to go in at the workshop door: in front of it, on the row just
   below the workshop (one row up from there and the workshop is drawn over them) */
static short door_x(const Scene *s, const Actor *a)
{
    return (short)(s->props[s->workshop].x + (DOOR_LEFT + DOOR_RIGHT) / 2 - spr_w(actor_sprite(a)) / 2);
}

static short door_row(const Scene *s)
{
    return (short)(s->props[s->workshop].y + spr_h(SPR_WORKSHOP));
}

/* someone fancies a trip into the workshop: not too often, one at a time, and Santa not
   when it is nearly time for the sleigh */
static short may_go_in(const Scene *s, const Actor *a)
{
    if (s->workshop < 0 || s->doorUser >= 0 || s->doorTimer > 0 || s->ending) return 0;
    if (a->kind == K_SANTA && s->sceneTicks > s->sceneLength - SECONDS(40)) return 0;
    return 1;
}

/* a trip into the workshop and back out, a few seconds later */
static void step_door(Scene *s, Actor *a, short w)
{
    short front = door_row(s);
    switch (a->state) {
    case ST_TODOOR:
        if (walk_to(s, a, door_x(s, a), front)) {
            a->state = ST_ATDOOR;
            a->frame = 0;
            a->timer = 12;
            s->doorOpen = 30;
        }
        break;
    case ST_ATDOOR:
        if (--a->timer > 0) break;
        /* in they go. An elf leaves a present inside, or comes out with one */
        a->state = ST_INSIDE;
        a->anim = 0;
        a->timer = (short)SECONDS(rnd_range(s, 3, 8));
        if (a->kind == K_ELFGIFT) a->kind = K_ELF;
        else if (a->kind == K_ELF && rnd(s, 2)) a->kind = K_ELFGIFT;
        break;
    case ST_INSIDE:
        if (--a->timer > 0) break;
        if (!a->anim) {                     /* the door opens ... */
            a->anim = 1;
            a->timer = 10;
            s->doorOpen = 36;
            break;
        }
        /* ... and out they come, to somewhere in front of the workshop */
        w = spr_w(actor_sprite(a));
        a->x16 = (short)(door_x(s, a) << 4);
        a->base = front;
        a->tx = (short)(door_x(s, a) + rnd_range(s, -50, 50));
        if (a->tx < 0) a->tx = 0;
        if (a->tx > FB_W - w) a->tx = (short)(FB_W - w);
        a->ty = front + 4 <= GROUND_BOTTOM ? rnd_range(s, (short)(front + 4), GROUND_BOTTOM) : front;
        a->state = ST_FROMDOOR;
        a->anim = 0;
        break;
    default:                                /* ST_FROMDOOR */
        if (walk_to(s, a, a->tx, a->ty)) {
            a->state = ST_REST;
            a->frame = 0;
            a->anim = 0;
            a->timer = rnd_range(s, 30, 120);
            a->stay = rnd_range(s, 15, 50);
            s->doorUser = -1;
            s->doorTimer = (s->force & 8) ? SECONDS(3) : SECONDS(rnd_range(s, 30, 90));
        }
        break;
    }
}

/* standing about: now and then a look the other way, an elf's little hop, a chuckle
   from Santa */
static void fidget(Scene *s, Actor *a)
{
    if (a->anim > 0) {
        a->anim--;
        a->hop = a->kind == K_SANTA ? (short)((a->anim >> 2) & 1) : (short)(a->anim >= 4);
        return;
    }
    a->hop = 0;
    if (--a->stay > 0) return;
    {
        short roll = rnd(s, 10);
        if (roll < 4) a->dir ^= 1;
        else if (roll < 6 && a->kind != K_MRSCLAUS) a->anim = a->kind == K_SANTA ? 24 : 8;
    }
    a->stay = rnd_range(s, 40, 140);
}

/* off an edge, out of sight, for its time away; then back in from an edge */
static short step_away(Scene *s, Actor *a, short w, short visitLo, short visitHi)
{
    if (--a->stay > 0) return 1;
    a->dir = (unsigned char)rnd(s, 2);
    a->base = rnd_range(s, GROUND_TOP + 14, GROUND_BOTTOM);
    a->x16 = (short)((a->dir ? FB_W : -w) << 4);
    a->state = ST_ENTER;
    a->anim = 0;
    a->hop = 0;
    a->frame = 0;
    a->stay = (short)SECONDS(rnd_range(s, visitLo, visitHi));
    return 0;
}

/* coming on, going off: free of the screen's edges. Returns 1 once that is done. */
static short cross_edge(Scene *s, Actor *a, short w, short awayLo, short awayHi)
{
    if (a->state == ST_ENTER) {
        if (a->x16 >= 0 && a->x16 <= ((FB_W - w) << 4)) {
            a->state = ST_WALK;
            a->timer = rnd_range(s, 60, 200);
            return 1;
        }
    } else if (a->state == ST_LEAVE) {
        if (a->x16 < -(w << 4) || a->x16 > (FB_W << 4)) {
            a->state = ST_AWAY;
            a->hop = 0;
            a->stay = (short)SECONDS(rnd_range(s, awayLo, awayHi));
            return 1;
        }
    }
    return 0;
}

/* its visit is up: off by the nearer edge */
static void head_off(Actor *a, short w)
{
    a->state = ST_LEAVE;
    a->dir = (unsigned char)(((a->x16 >> 4) + w / 2) < FB_W / 2);
}

/* Bunnies sit, then hop: 14 ticks in the air, a little arc. They stay a while, hop off,
   and come back some time later, so any one is in sight about a third of the time. */
static void step_bunny(Scene *s, Actor *a, short w)
{
    if (a->state == ST_AWAY) {
        step_away(s, a, w, 15, 40);
        return;
    }
    if (a->state == ST_WALK || a->state == ST_REST) a->stay--;
    if (a->state == ST_REST) {
        a->frame = 0;
        a->hop = 0;
        if (a->stay <= 0) { head_off(a, w); a->anim = 0; return; }
        if (--a->timer <= 0) {
            a->state = ST_WALK;
            a->anim = 0;
            if (rnd(s, 5) == 0) a->dir ^= 1;
        }
        return;
    }
    {
        short t = ++a->anim;
        a->frame = 1;
        a->hop = (short)((t * (14 - t)) / 12);
        a->x16 += a->dir ? -a->speed : a->speed;
        if (t >= 14) {
            a->hop = 0;
            a->frame = 0;
            a->anim = 0;
            if (cross_edge(s, a, w, 40, 90)) return;
            if (a->state == ST_WALK) {
                if (a->stay <= 0) head_off(a, w);
                else if (rnd(s, 3) == 0) {          /* usually another hop or two, then a rest */
                    a->state = ST_REST;
                    a->timer = rnd_range(s, 20, 120);
                }
            }
        }
    }
    if (a->state == ST_WALK || a->state == ST_REST) {
        if (a->x16 <= 0) { a->x16 = 0; a->dir = 0; }
        if (a->x16 >= ((FB_W - w) << 4)) { a->x16 = (short)((FB_W - w) << 4); a->dir = 1; }
    }
}

/* The visitors. The nutcracker marches, halts at attention, about-faces. The jack-in-the-
   box hops along in his box, and now and then springs out, wobbles, and goes back in.
   Each comes on from an edge, stays a while, goes off, and may come back later. */
static void step_visitor(Scene *s, Actor *a, short w)
{
    short jack = a->kind == K_JACK;
    if (a->state == ST_AWAY) {
        step_away(s, a, w, 25, 50);
        return;
    }
    if (a->state == ST_POP) {
        short t = ++a->anim;
        /* boing: a quick wobble, then a slow sway */
        a->frame = (unsigned char)(t < 24 ? (t / 3) & 1 : (t / 12) & 1);
        if (--a->timer <= 0) {
            a->state = ST_REST;
            a->frame = 0;
            a->anim = 0;
            a->timer = rnd_range(s, 15, 40);
        }
        return;
    }
    if (a->state == ST_REST) {
        a->frame = 0;
        a->hop = 0;
        a->stay--;
        if (--a->timer > 0) return;
        if (a->stay <= 0) { head_off(a, w); return; }
        a->state = ST_WALK;
        a->anim = 0;
        a->timer = rnd_range(s, 60, 200);
        if (rnd(s, 3) == 0) a->dir ^= 1;
        return;
    }
    /* walking (or hopping), coming on or going off */
    if (a->state == ST_WALK) a->stay--;
    if (jack) {
        short t = ++a->anim;
        if (t <= 16) {                          /* a hop */
            a->hop = (short)((t * (16 - t)) / 22);
            a->x16 += a->dir ? -a->speed : a->speed;
        } else {
            a->hop = 0;
            if (t >= 16 + 10) {                 /* a breath between hops */
                a->anim = 0;
                if (cross_edge(s, a, w, 40, 120)) return;
                if (a->state == ST_WALK) {
                    if (a->stay <= 0) head_off(a, w);
                    else if (rnd(s, 7) == 0) {
                        a->state = ST_POP;
                        a->anim = 0;
                        a->timer = (short)SECONDS(rnd_range(s, 2, 4));
                    } else if (rnd(s, 8) == 0) {
                        a->state = ST_REST;
                        a->timer = rnd_range(s, 30, 90);
                    } else if (rnd(s, 6) == 0) {
                        a->dir ^= 1;
                    }
                }
            }
        }
    } else {
        a->x16 += a->dir ? -a->speed : a->speed;
        a->anim++;
        a->frame = (unsigned char)((a->anim / walk_rate(a)) & 1);
        if (cross_edge(s, a, w, 40, 120)) return;
        if (a->state == ST_WALK && --a->timer <= 0) {
            short roll = rnd(s, 10);
            a->frame = 0;
            if (a->stay <= 0) head_off(a, w);
            else if (roll < 4) {                /* halt, at attention */
                a->state = ST_REST;
                a->timer = rnd_range(s, 45, 150);
                if (rnd(s, 2)) a->dir ^= 1;     /* and about face */
            } else {
                a->timer = rnd_range(s, 60, 200);
            }
        }
    }
    if (a->state == ST_WALK || a->state == ST_REST) {
        if (a->x16 <= 0) { a->x16 = 0; a->dir = 0; }
        if (a->x16 >= ((FB_W - w) << 4)) { a->x16 = (short)((FB_W - w) << 4); a->dir = 1; }
    }
}

static void step_actor(Scene *s, Actor *a)
{
    short w = spr_w(actor_sprite(a));
    short minX16 = 0, maxX16 = (short)((FB_W - w) << 4);

    if (a->state == ST_WAIT) return;
    if (a->kind == K_BUNNY) {
        step_bunny(s, a, w);
        return;
    }
    if (a->kind == K_NUTCRACKER || a->kind == K_JACK) {
        step_visitor(s, a, w);
        return;
    }
    if (a->state >= ST_TODOOR) {
        step_door(s, a, w);
        return;
    }
    if (a->kind == K_PARTRIDGE) {
        step_partridge(s, a);
        return;
    }
    if (is_flyer(a)) {
        step_bird(s, a);
        return;
    }
    if (a->state == ST_PARADE) {
        step_parade(s, a, w);
        return;
    }
    if (a->kind == K_SWAN && s->pond >= 0) {
        /* swans keep to the pond */
        Prop *p = &s->props[s->pond];
        minX16 = (short)((p->x + 3) << 4);
        maxX16 = (short)((p->x + spr_w(SPR_POND) - w - 3) << 4);
    }

    if (a->state == ST_AWAY) {
        return;                     /* up in the sky */
    } else if (a->state == ST_EXIT) {
        a->x16 += a->dir ? -a->speed : a->speed;
        a->anim++;
        a->frame = (unsigned char)((a->anim / 8) & 1);
        if (a->x16 >= (FB_W << 4) || a->x16 <= -(w << 4)) a->state = ST_AWAY;
        return;                     /* free of the screen edges while leaving */
    } else if (a->state == ST_REST) {
        a->frame = 0;
        if (is_person(a)) fidget(s, a);
        if (--a->timer <= 0) {
            a->state = ST_WALK;
            a->hop = 0;
            a->anim = 0;
            a->timer = rnd_range(s, 60, 300);
            if (rnd(s, 3) == 0) a->dir ^= 1;
        }
    } else {
        a->x16 += a->dir ? -a->speed : a->speed;
        a->anim++;
        a->frame = (unsigned char)((a->anim / walk_rate(a)) & 1);
        if (a->kind == K_GOOSE && rnd(s, 400) == 0) lay_egg(s, a, w);
        if (--a->timer <= 0) {
            short roll = rnd(s, 10);
            if (is_person(a) && may_go_in(s, a) && rnd(s, 3) == 0) {
                /* off to the workshop for a bit */
                s->doorUser = (short)(a - s->actors);
                a->state = ST_TODOOR;
                a->anim = 0;
                return;
            }
            if (is_person(a) && roll < 5) {
                /* a while standing about */
                a->state = ST_REST;
                a->anim = 0;
                a->frame = 0;
                a->stay = rnd_range(s, 15, 60);
                a->timer = (short)SECONDS(rnd_range(s, 2, 12));
            } else if (roll < 4) {
                a->state = ST_REST;
                a->timer = rnd_range(s, a->kind == K_DEER ? 90 : 30, a->kind == K_DEER ? 300 : 120);
            } else if (roll < 6) {
                a->dir ^= 1;
                a->timer = rnd_range(s, 60, 240);
            } else {
                a->timer = rnd_range(s, 60, 240);
            }
        }
    }

    if (a->x16 <= minX16) { a->x16 = minX16; a->dir = 0; }
    if (a->x16 >= maxX16) { a->x16 = maxX16; a->dir = 1; }
}

static void step_weather(Scene *s)
{
    short i;

    if (s->snowSetting > 0 && --s->weatherTimer <= 0) {
        if (s->snowing && s->snowSetting < 95) {
            s->snowing = 0;
            s->weatherTimer = SECONDS(rnd_range(s, 20, 20 + (110 - s->snowSetting) * 2));
        } else {
            s->snowing = 15 + (short)(((long)s->snowSetting * (MAX_AIR - 15)) / 100)
                         - rnd(s, 15);
            s->wind = rnd_range(s, -3, 3);
            s->weatherTimer = SECONDS(rnd_range(s, 30, 90));
        }
    }

    /* ease the rate new flakes appear at toward the weather's goal over about six
       seconds, so snow starts as a few flakes and builds, and tapers off the same way */
    {
        long goal = (long)s->snowing * 4096 / FLAKE_LIFE;
        if (goal != s->snowRateGoal) {
            long d = goal > s->snowRate ? goal - s->snowRate : s->snowRate - goal;
            s->snowRateGoal = goal;
            s->snowRateStep = d / (SIM_HZ * 6);
            if (s->snowRateStep < 1) s->snowRateStep = 1;
        }
        if (s->snowRate < goal) {
            s->snowRate += s->snowRateStep;
            if (s->snowRate > goal) s->snowRate = goal;
        } else if (s->snowRate > goal) {
            s->snowRate -= s->snowRateStep;
            if (s->snowRate < goal) s->snowRate = goal;
        }
    }

    /* snow settles on the trees while it falls, faster the heavier it is */
    if (s->snowRate > 0 && s->treeSnow < 4096) {
        long most = (long)MAX_AIR * 4096 / FLAKE_LIFE;
        s->treeSnow += (short)(3 + (s->snowRate * 5) / most);
        if (s->treeSnow > 4096) s->treeSnow = 4096;
    }

    /* the cloud cover: in while it snows, on its way when it stops. A change of mind
       half way sends the front back the way it came. */
    if (s->snowing) {
        if (s->cloudOut > 0) {
            s->cloudOut -= CLOUD_SPEED;
            if (s->cloudOut < 0) s->cloudOut = 0;
        } else if (s->cloudIn < CLOUD_FULL) {
            if (s->cloudIn == 0)
                s->cloudFrom = s->wind > 0 ? 0 : (s->wind < 0 ? 1 : rnd(s, 2));
            s->cloudIn += CLOUD_SPEED;
            if (s->cloudIn > CLOUD_FULL) s->cloudIn = CLOUD_FULL;
        }
    } else if (s->cloudIn > 0) {
        if (s->cloudIn < CLOUD_FULL && s->cloudOut == 0) {
            s->cloudIn -= CLOUD_SPEED;
            if (s->cloudIn < 0) s->cloudIn = 0;
        } else {
            s->cloudOut += CLOUD_SPEED;
            if (s->cloudOut >= CLOUD_FULL) s->cloudIn = s->cloudOut = 0;
        }
    }

    for (i = 0; i < MAX_FLAKES; i++) {
        Flake *f = &s->flakes[i];
        if (!f->live) continue;
        if (f->live == 2) {
            /* lying on the ground: a second, then gone */
            if (--f->rest <= 0) f->live = 0;
            continue;
        }
        f->y16 += f->vy;
        f->phase++;
        f->x16 += s->wind;
        if ((f->phase & 3) == 0) f->x16 += WIGGLE[(f->phase >> 2) & 15] / 2;
        if ((f->y16 >> 4) >= f->land) {
            f->y16 = (short)(f->land << 4);
            f->live = 2;
            f->rest = SIM_HZ;
        }
    }

    s->snowAcc += s->snowRate;
    for (i = 0; i < MAX_FLAKES && s->snowAcc >= 4096; i++) {
        Flake *f = &s->flakes[i];
        if (f->live) continue;
        s->snowAcc -= 4096;
        f->live = 1;
        f->x16 = (short)((rnd(s, FB_W + 40) - 20) << 4);
        f->y16 = (short)(-16 * rnd(s, 8));
        f->vy = rnd_range(s, 4, 9);
        f->phase = (unsigned char)rnd16(s);
        f->land = rnd_range(s, GROUND_TOP, FB_H - 1);
    }
    if (s->snowAcc > 2 * 4096) s->snowAcc = 2 * 4096;   /* no burst if the sky was full */
}

static void step_sky(Scene *s)
{
    short i;

    /* chimney smoke */
    if (s->chimneyX >= 0 && --s->smokeTimer <= 0) {
        Particle *p = new_particle(s);
        s->smokeTimer = rnd_range(s, 5, 9);
        if (p) {
            p->kind = P_SMOKE;
            p->x16 = (short)((s->chimneyX + rnd(s, 2)) << 4);
            p->y16 = (short)((s->chimneyY - 1) << 4);
            p->vx = (short)(s->wind + rnd_range(s, -1, 2));
            p->vy = -rnd_range(s, 3, 5);
            p->life = rnd_range(s, 70, 100);
        }
    }

    /* the sleigh in flight (step_ending launches it) */
    if (s->sleighActive) {
        s->sleighX16 += s->sleighDir ? -s->sleighSpeed : s->sleighSpeed;
        if ((s->tick & 1) == 0) {
            Particle *p = new_particle(s);
            if (p) {
                /* stardust off the back of the sleigh */
                p->kind = P_SPARKLE;
                p->x16 = (short)(s->sleighX16 + ((s->sleighDir ? 60 : 1) << 4));
                p->y16 = (short)((s->sleighY + 6 + rnd(s, 3)) << 4);
                p->vx = rnd_range(s, -2, 2);
                p->vy = rnd_range(s, 1, 4);
                p->life = rnd_range(s, 16, 30);
            }
        }
        if ((s->sleighDir && s->sleighX16 < -(64 << 4)) ||
            (!s->sleighDir && s->sleighX16 > ((FB_W + 2) << 4)))
            s->sleighActive = 0;
    }

    /* shooting stars */
    if (!s->shootActive && --s->shootTimer <= 0) {
        s->shootActive = 1;
        s->shootLife = rnd_range(s, 10, 18);
        s->shootX16 = (short)(rnd_range(s, 20, FB_W - 20) << 4);
        s->shootY16 = (short)(rnd_range(s, 2, 18) << 4);
        s->shootVX = (short)(rnd(s, 2) ? rnd_range(s, 36, 52) : -rnd_range(s, 36, 52));
        s->shootVY = rnd_range(s, 12, 22);
    }
    if (s->shootActive) {
        s->shootX16 += s->shootVX;
        s->shootY16 += s->shootVY;
        if (--s->shootLife <= 0) {
            s->shootActive = 0;
            s->shootTimer = SECONDS(rnd_range(s, 12, 45));
        }
    }

    /* the workshop door */
    if (s->doorOpen > 0) s->doorOpen--;
    if (s->doorTimer > 0 && s->doorUser < 0) s->doorTimer--;

    /* the day's gift sets off across the screen */
    if (s->paradeTimer > 0 && --s->paradeTimer == 0)
        for (i = 0; i < s->nactors; i++)
            if (s->actors[i].state == ST_WAIT) s->actors[i].state = ST_PARADE;
    for (i = 0; i < MAX_EGGS; i++)
        if (s->eggs[i].life > 0) s->eggs[i].life--;
    if (s->gift == 2 || s->gift == 4 || s->gift == 5) step_flock(s);
    if (s->gift >= 8 && s->paradeTimer == 0) {
        /* once the parade is all gone, it turns round and comes back: after a while, or
           at once with the preview's switch */
        short j = 0, any = 0, out = 0;
        for (i = 0; i < s->nactors; i++) {
            Actor *a = &s->actors[i];
            if (a->kind < K_MAID) continue;
            any = 1;
            if (a->state != ST_AWAY) out = 1;
        }
        if (any && !out)
            for (i = 0; i < s->nactors; i++) {
                Actor *a = &s->actors[i];
                short w;
                if (a->kind < K_MAID) continue;
                w = spr_w(actor_sprite(a));
                a->dir ^= 1;
                a->x16 = (short)((a->dir ? FB_W + j * s->paradeGap : -(w + j * s->paradeGap)) << 4);
                a->state = ST_WAIT;
                a->anim = 0;
                j++;
            }
        if (any && !out)
            s->paradeTimer = s->giftNow ? 1 : SECONDS(rnd_range(s, 10, 30));
    }

    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s->parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x16 += p->vx;
        p->y16 += p->vy;
        if (p->kind == P_SMOKE && (p->life & 7) == 0) p->vx += WIGGLE[(p->life >> 3) & 15] / 3;
    }
}

/* ticks for a walker to get wholly off the left (dir 1) or right (dir 0) edge */
static long ticks_to_edge(const Actor *a, short dir)
{
    long w16 = (long)spr_w(actor_sprite(a)) << 4;
    long d = dir ? a->x16 + w16 : ((long)FB_W << 4) - a->x16;
    return d / a->speed;
}

/* The scene's last act. Its time is up: Santa and Rudolph walk off the same edge at
   their own pace (the one that gets the slower of them off sooner), the sleigh takes off
   from beyond that edge and flies back across the sky, and a few seconds after it has
   gone the screen goes black. The scene runs over its length by however long that takes. */
static void step_ending(Scene *s)
{
    Actor *walkers[2];
    short n = 0, i, gone = 1;

    for (i = 0; i < s->nactors; i++)
        if (s->actors[i].kind == K_SANTA || i == s->rudolph) walkers[n++] = &s->actors[i];

    switch (s->ending) {
    case END_NONE:
        if (s->sceneTicks < s->sceneLength) return;
        for (i = 0; i < n; i++)
            if (walkers[i]->state >= ST_TODOOR) return;     /* Santa is out of the workshop first */
        {
            long left = 0, right = 0;
            short dir;
            for (i = 0; i < n; i++) {
                long l = ticks_to_edge(walkers[i], 1), r = ticks_to_edge(walkers[i], 0);
                if (l > left) left = l;
                if (r > right) right = r;
            }
            dir = (short)(left <= right);
            for (i = 0; i < n; i++) {
                walkers[i]->state = ST_EXIT;
                walkers[i]->dir = (unsigned char)dir;
                walkers[i]->hop = 0;
            }
        }
        s->ending = END_WALKOFF;
        break;
    case END_WALKOFF:
        for (i = 0; i < n; i++)
            if (walkers[i]->state != ST_AWAY) gone = 0;
        if (gone) {
            s->ending = END_TAKEOFF;
            s->endTimer = SECONDS(2);       /* harnessing up */
        }
        break;
    case END_TAKEOFF:
        if (--s->endTimer > 0) break;
        if (s->sleighOn) {
            /* from beyond the edge they left by, back across the sky */
            s->sleighActive = 1;
            s->sleighDir = n ? (short)(walkers[0]->dir == 0) : rnd(s, 2);
            s->sleighY = rnd_range(s, 4, SKY_BOTTOM - 16);
            s->sleighSpeed = rnd_range(s, 14, 22);
            s->sleighX16 = (short)(s->sleighDir ? (FB_W + 2) << 4 : -(62 << 4));
            s->ending = END_FLYING;
        } else {
            s->ending = END_QUIET;
            s->endTimer = SECONDS(2);
        }
        break;
    case END_FLYING:
        if (!s->sleighActive) {
            s->ending = END_QUIET;
            s->endTimer = SECONDS(4);
        }
        break;
    default:
        if (--s->endTimer <= 0) s->blackout = SIM_HZ;   /* a second of black, then somewhere new */
        break;
    }
}

static void scene_step(Scene *s)
{
    short i;
    s->tick++;

    if (s->blackout > 0) {
        if (--s->blackout == 0) new_scene(s);
        return;
    }
    s->sceneTicks++;

    for (i = 0; i < s->nactors; i++) step_actor(s, &s->actors[i]);
    step_weather(s);
    step_sky(s);
    step_ending(s);
}

/* ------------------------------------------------------------------ rendering */

static void draw_sleigh(Scene *s, unsigned char *fb)
{
    /* the team, right-facing layout: sleigh at 0, reindeer at 18, 30, 42 (the lead) */
    static const signed char DEER_X[3] = { 18, 30, 42 };
    const short groupW = 54;
    short gx = s->sleighX16 >> 4, gy = s->sleighY, i;
    short flip = s->sleighDir;
    short bob = (short)((s->tick >> 3) & 1);
    unsigned char slots[4];
    short x0, x1;

    /* reins */
    x0 = flip ? gx + groupW - 13 : gx + 13;
    x1 = flip ? gx + groupW - 50 : gx + 50;
    if (x0 > x1) { short t = x0; x0 = x1; x1 = t; }
    for (i = x0; i <= x1; i++) plot(fb, i, gy + 4 + bob, C_DKGRAY);

    blit(s, fb, SPR_SLEIGH, flip ? gx + groupW - spr_w(SPR_SLEIGH) : gx, gy + bob, flip, 0);
    slots[0] = slots[1] = slots[3] = C_WHITE;
    for (i = 0; i < 3; i++) {
        short spr = ((s->tick >> 2) + i) & 1 ? SPR_FLY1 : SPR_FLY0;
        short rx = DEER_X[i];
        short dx = flip ? gx + groupW - (rx + spr_w(spr)) : gx + rx;
        short dbob = (short)(((s->tick >> 3) + i) & 1);
        slots[2] = i == 2 ? C_RED : C_DKBROWN;     /* Rudolph leads */
        blit(s, fb, spr, dx, gy + 1 + dbob, flip, slots);
    }
}

/* How much cloud is over column x: 0 clear .. 256 covered. */
static short sky_cover(const Scene *s, short x)
{
    long d16 = (long)(s->cloudFrom ? FB_W - 1 - x : x) << 4;
    long a = (s->cloudIn - d16) >> 2;      /* 256 per CLOUD_EDGE px: (16 * 64) / 256 = 4 */
    long b = (s->cloudOut - d16) >> 2;
    if (a < 0) a = 0;
    if (a > 256) a = 256;
    if (b < 0) b = 0;
    if (b > 256) b = 256;
    return (short)(a - b);
}

/* Plot a sky pixel (moon, star, shooting star) as seen through the clouds: dimmed down a
   ramp of grays, the in-between steps dithered in the old way. */
static void sky_plot(const Scene *s, unsigned char *fb, short x, short y, unsigned char c)
{
    static const unsigned char RAMP[9] = {
        C_BLACK, C_FADE1, C_FADE2, C_DKGRAY, C_DIMWHITE, C_FADE5, C_SNOWSHADE, C_FADE7, C_WHITE
    };
    static const unsigned char BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    short cover, level, v;
    if ((unsigned short)x >= FB_W || (unsigned short)y >= FB_H) return;
    cover = sky_cover(s, x);
    if (cover == 0) { fb[y * FB_W + x] = c; return; }
    switch (c) {
    case C_BLACK:     level = 0; break;
    case C_FADE1:     level = 1; break;
    case C_DKGRAY:    level = 3; break;
    case C_DIMWHITE:  level = 4; break;
    case C_SNOWSHADE: level = 6; break;
    case C_LTBLUE:    level = 7; break;
    default:          level = 8; break;
    }
    v = (short)(level * (256 - cover));    /* 0 .. 8 * 256 */
    level = (short)(v >> 8);
    if (((v >> 4) & 15) > BAYER[((y & 3) << 2) | (x & 3)]) level++;
    fb[y * FB_W + x] = RAMP[level];
}

/* The moon in tonight's phase: a 9 px disc lit from the right while it waxes and from
   the left while it wanes (as seen from the north), the dark part a faint disc. */
static void draw_moon(const Scene *s, unsigned char *fb)
{
    static const short COS32[32] = { 256, 251, 237, 213, 181, 142, 98, 50, 0, -50, -98, -142, -181, -213, -237, -251, -256, -251, -237, -213, -181, -142, -98, -50, 0, 50, 98, 142, 181, 213, 237, 251 };
    static const short HALF_ROW[5] = { 1152, 1123, 1032, 859, 528 };   /* the disc's half width per row, 1/256 px */
    long c = COS32[(s->moonPhase >> 3) & 31];
    short dx, dy;
    for (dy = -4; dy <= 4; dy++) {
        long th = ((long)HALF_ROW[dy < 0 ? -dy : dy] * c) >> 8;
        for (dx = -4; dx <= 4; dx++) {
            short lit;
            unsigned char col;
            if (dx * dx + dy * dy > 20) continue;
            lit = s->moonPhase < 128 ? (long)dx * 256 > th : (long)dx * 256 < -th;
            if (!lit) col = C_FADE1;
            else if ((dx == -2 && dy == -1) || (dx == -1 && dy == -1) || (dx == 1 && dy == 1) ||
                     (dx == 2 && dy == 0) || (dx == 0 && dy == 2)) col = C_SNOWSHADE;   /* the seas */
            else col = C_WHITE;
            sky_plot(s, fb, (short)(s->moonX + 4 + dx), (short)(s->moonY + 4 + dy), col);
        }
    }
}

/* The preview module's date, top left: "DEC 14" in a 3 x 5 font. */
static void draw_preview_date(const Scene *s, unsigned char *fb)
{
    /* rows of 3 bits, top first: 0..9, then D, E, C */
    static const unsigned char FONT[13][5] = {
        { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
        { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
        { 6, 5, 5, 5, 6 }, { 7, 4, 6, 4, 7 }, { 3, 4, 4, 4, 3 }
    };
    short year, month, day, g[5], i, r, c;
    clock_date(s->sceneClock, &year, &month, &day);
    g[0] = 10; g[1] = 11; g[2] = 12;          /* DEC: the preview only ever shows December */
    g[3] = (short)(day / 10);
    g[4] = (short)(day % 10);
    for (i = 0; i < 5; i++)
        for (r = 0; r < 5; r++)
            for (c = 0; c < 3; c++)
                if (FONT[g[i]][r] & (4 >> c))
                    plot(fb, (short)(3 + i * 4 + (i >= 3) * 3 + c), (short)(3 + r), C_YELLOW);
}

static void scene_render(Scene *s, unsigned char *fb)
{
    short i, n;

    fill_bytes(fb, C_BLACK, FB_SIZE);
    if (s->blackout > 0) return;

    /* stars: most steady, a few twinkle */
    for (i = 0; i < s->nstars; i++) {
        Star *st = &s->stars[i];
        unsigned char k = (unsigned char)((s->tick >> 1) + st->phase);
        unsigned char c = st->bright ? C_WHITE : C_DIMWHITE;
        if (k < 6) c = st->bright ? C_LTBLUE : C_BLACK;
        sky_plot(s, fb, st->x, st->y, c);
    }
    draw_moon(s, fb);

    if (s->shootActive) {
        short x = s->shootX16 >> 4, y = s->shootY16 >> 4;
        short dx = s->shootVX > 0 ? -1 : 1;
        sky_plot(s, fb, x, y, C_WHITE);
        sky_plot(s, fb, (short)(x + dx * 2), (short)(y - 1), C_LTBLUE);
        sky_plot(s, fb, (short)(x + dx * 4), (short)(y - 2), C_SNOWSHADE);
        sky_plot(s, fb, (short)(x + dx * 6), (short)(y - 3), C_DKGRAY);
    }

    if (s->sleighActive) draw_sleigh(s, fb);

    /* things lying flat on the ground go under everything: the pond, then eggs */
    if (s->pond >= 0) blit(s, fb, SPR_POND, s->props[s->pond].x, s->props[s->pond].y, 0, 0);
    for (i = 0; i < MAX_EGGS; i++) {
        Egg *e = &s->eggs[i];
        if (e->life <= 0) continue;
        plot(fb, e->x, e->y - 1, C_WHITE);
        plot(fb, e->x + 1, e->y - 1, C_WHITE);
        plot(fb, e->x, e->y, C_SNOWSHADE);
        plot(fb, e->x + 1, e->y, C_WHITE);
    }

    /* props and folks, back to front by the row they stand on */
    n = 0;
    for (i = 0; i < s->nprops; i++) {
        if (i == s->pond) continue;
        s->drawKey[n] = (short)(s->props[i].y + spr_h(s->props[i].spr));
        s->drawIdx[n++] = i;
    }
    for (i = 0; i < s->nactors; i++) {
        if (s->actors[i].state == ST_AWAY || s->actors[i].state == ST_WAIT ||
            s->actors[i].state == ST_INSIDE) continue;
        /* perched up on something, it sorts with that thing; in flight, by its own row */
        s->drawKey[n] = s->actors[i].key && s->actors[i].state != ST_PARADE
            ? s->actors[i].key : s->actors[i].base;
        s->drawIdx[n++] = (short)(i + 100);
    }
    for (i = 1; i < n; i++) {
        short k = s->drawKey[i], d = s->drawIdx[i], j = i - 1;
        while (j >= 0 && s->drawKey[j] > k) {
            s->drawKey[j + 1] = s->drawKey[j];
            s->drawIdx[j + 1] = s->drawIdx[j];
            j--;
        }
        s->drawKey[j + 1] = k;
        s->drawIdx[j + 1] = d;
    }
    for (i = 0; i < n; i++) {
        short d = s->drawIdx[i];
        if (d < 100) {
            Prop *p = &s->props[d];
            blit(s, fb, p->spr, p->x, p->y, 0, 0);
            if (d == s->workshop && s->doorOpen > 0) {
                /* the door swung in, the lit room beyond */
                short r, c;
                for (r = DOOR_TOP; r <= DOOR_BOTTOM; r++)
                    for (c = DOOR_LEFT + 2; c <= DOOR_RIGHT; c++)
                        plot(fb, (short)(p->x + c), (short)(p->y + r), r == DOOR_BOTTOM ? C_ORANGE : C_YELLOW);
            }
        } else {
            Actor *a = &s->actors[d - 100];
            short spr = actor_sprite(a);
            blit(s, fb, spr, a->x16 >> 4, a->base - spr_h(spr) + 1 - a->hop, a->dir, a->slots);
        }
    }

    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s->parts[i];
        short x, y;
        if (p->life <= 0) continue;
        x = p->x16 >> 4;
        y = p->y16 >> 4;
        if (p->kind == P_SMOKE) {
            /* a puff starts as a 2x2 wisp and thins out as it rises */
            unsigned char c = p->life > 40 ? C_SNOWSHADE : (p->life > 12 ? C_DKGRAY : C_BLACK);
            if (c == C_BLACK) continue;
            plot(fb, x, y, c);
            if (p->life > 55) { plot(fb, x + 1, y, c); plot(fb, x, y - 1, c); plot(fb, x + 1, y - 1, c); }
            else if (p->life > 30) plot(fb, x + 1, y, c);
        } else {
            unsigned char c = p->life > 18 ? C_YELLOW : (p->life > 8 ? C_GOLD : C_DKGRAY);
            plot(fb, x, y, c);
        }
    }

    for (i = 0; i < MAX_FLAKES; i++) {
        Flake *f = &s->flakes[i];
        if (f->live && f->y16 >= 0) plot(fb, f->x16 >> 4, f->y16 >> 4, C_WHITE);
    }
    if (s->previewDay) draw_preview_date(s, fb);
}

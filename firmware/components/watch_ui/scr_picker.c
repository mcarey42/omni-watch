/* SCR_PICKER: the face picker, design/mockups/faces-setup.js `nav-map` (frame(), pointer(), drawThumb()).
 *
 * The bezel ring holds every face (O2, spec 8.2): the carousel's in its order, then the theme's others at 60 % with no
 * numeral, then Settings. Turning the rim (a drag that starts outside the r 146 centre) follows the finger, coasts on
 * release and settles on a detent; the item under the 12 o'clock brass window is the selection, and once it has settled
 * it is opened (UI_SHOW_FACE with the face id: omni_nav_show_id picks a carousel face and shows any other transiently,
 * B4; or Settings).
 * A tap on a thumbnail brings it round and opens it; "Open" opens the selection at once; a swipe across the
 * centre steps the selection without opening. BOOT backs out (nav).
 *
 * The thumbnails, their ring and the window are thumbring.c's. Faces without a view yet carry the mockup's "planned"
 * dimming and a "Planned" key; picking one in the carousel still moves the carousel there (the digital face stands
 * in), and one outside it is not opened (it has nothing to show transiently). Turning
 * repaints only what moves: a full-screen frame costs ~110 ms on this panel, most of it the brass background. */
#include <math.h>
#include <string.h>
#include "shell.h"
#include "thumbring.h"
#include "theme.h"
#include "store.h"
#include "board.h"
#include "esp_log.h"
#include "watch_theme.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define CENTRE_R 146        /* drags starting outside this turn the bezel */
#define OPEN_X 173          /* NAV.open */
#define OPEN_Y 238
#define OPEN_W 120
#define OPEN_H 44
#define ANIM_MS 16
#define SETTLE_MS 150       /* the landed selection shows this long before it opens */
#define MAX_ITEMS (OMNI_CAROUSEL_MAX + 1)

/* Titles, groups and thumbnails are the theme's (watch_theme()->faces[]); Settings is the picker's own item. */
static const watch_face_info_t SETTINGS_ITEM = {NULL, "Settings", "Setup", NULL};

typedef struct {
    int16_t id;              /* the face id; -1 = Settings */
    bool shown;              /* in the carousel */
} item_t;

static struct {
    lv_obj_t *root, *draw;
    lv_timer_t *anim;
    item_t items[MAX_ITEMS];
    int n;
    thumbring_turn_t m;      /* rot: radians, rot = -index * step puts that item under the window */
    bool commit;             /* open the selection once it has settled */
    uint32_t settled_t;      /* when it settled (0 = moving) */
    bool done;               /* opened: ignore input until the screen goes */
    int sel_shown;
    uint32_t sel_t;          /* selection change, for the 220 ms title fade */
} S;

#define clampf thumbring_clampf
#define smooth thumbring_smooth
#define wrapf thumbring_wrapf
static const watch_face_info_t *info(const item_t *it) { return it->id < 0 ? &SETTINGS_ITEM : &watch_theme()->faces[it->id]; }
/* It has a view to show: Settings, or a registered face (the ring's own live flag, without needing its table, which may
 * have failed to allocate: Task 13 carry). */
static bool live(const item_t *it) { return it->id < 0 || shell_face_ready((uint8_t)it->id); }
static float step(void) { return TAU / (float)S.n; }
static int sel(void)
{
    int i = (int)lroundf(-S.m.rot / step());
    return ((i % S.n) + S.n) % S.n;
}

/* ------------------------------------------------------------ drawing (frame()) */
static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    uint32_t now = lv_tick_get();
    /* the engraved scale turning with the bezel: 8 marks per item, majors at the items */
    thumbring_scale(layer, S.m.rot, 8, S.n, THEME_OPA(0.7));
    int cur = sel();
    thumbring_draw(layer, S.m.rot, S.m.drag ? -1 : cur);
    thumbring_index(layer);

    /* centre: group, title (fading in 220 ms on a new selection), Open / Planned, the help lines */
    const watch_face_info_t *fi = info(&S.items[cur]);
    const bool on = live(&S.items[cur]);
    lv_opa_t fade = (lv_opa_t)(255 * smooth((float)(now - S.sel_t) / 220));
    char group[16];
    size_t gl = strlen(fi->group);
    for (size_t k = 0; k <= gl && k < sizeof group; k++)
        group[k] = (char)(fi->group[k] >= 'a' && fi->group[k] <= 'z' ? fi->group[k] - 32 : fi->group[k]);
    group[sizeof group - 1] = 0;
    theme_draw_text(layer, group, &font_plex_sans_600_10, THEME_BRASS, fade, 3, THEME_CENTER, CX, 150);
    size_t tl = strlen(fi->title);
    theme_draw_text(layer, fi->title, tl > 13 ? &font_cormorant_700_26 : &font_cormorant_700_32, THEME_SETUP_HEAD,
                    fade, 0, THEME_CENTER, CX, 194);
    theme_draw_key(layer, OPEN_X, OPEN_Y, OPEN_W, OPEN_H, on ? THEME_KEY_PRIMARY : THEME_KEY_DARK);
    if (on)
        theme_draw_text(layer, "Open", &font_cormorant_700_20, 0x1f1606, LV_OPA_COVER, 0, THEME_CENTER, CX, OPEN_Y + 29);
    else
        theme_draw_text(layer, "Planned", &font_plex_sans_500_15, THEME_SETUP_DIM, LV_OPA_COVER, 0, THEME_CENTER, CX, OPEN_Y + 29);
    static const char *const ICON[2] = {"\xe2\x86\xbb", "\xe2\x87\x86"}; /* U+21BB, U+21C6 */
    static const char *const LINE[3] = {"turn the bezel \xc2\xb7 choose", "swipe centre \xc2\xb7 next / prev",
                                        "BOOT \xc2\xb7 back"};
    for (int i = 0; i < 3; i++) {
        int32_t y = 314 + i * 18;
        if (i < 2) theme_draw_text(layer, ICON[i], &font_plex_sans_500_12, THEME_BRASS, LV_OPA_COVER, 0, THEME_CENTER, CX - 86, y);
        else { /* U+25C9 (not in Plex): a ring with a dot */
            theme_draw_arc(layer, CX - 86, y - 4, 5, 1, 0, 360, THEME_BRASS, LV_OPA_COVER, false);
            theme_draw_box(layer, CX - 88, y - 6, CX - 84, y - 2, LV_RADIUS_CIRCLE, THEME_BRASS, LV_OPA_COVER, 0, 0);
        }
        theme_draw_text(layer, LINE[i], &font_plex_sans_400_12, THEME_SETUP_DIM, LV_OPA_COVER, 0, THEME_LEFT, CX - 74, y);
    }
}

/* ------------------------------------------------------------ motion */
/* If the queue was full the post is lost: stay live and try again after the next settle (anim_cb). */
static void open_selection(void)
{
    const item_t *it = &S.items[sel()];
    if (!live(it) && !it->shown) { /* nothing to show it with: stays on the picker */
        S.commit = false;
        return;
    }
    bool sent = it->id < 0 ? shell_post(UI_OPEN_SETTINGS, 0) : shell_post(UI_SHOW_FACE, it->id);
    S.done = sent;
    S.commit = !sent;
    S.settled_t = 0;
}

static void inval_centre(void)
{
    lv_area_t a = {CX - 150, 132, CX + 150, 372}; /* group, title, the key, the help lines */
    lv_obj_invalidate_area(S.draw, &a);
}

static void inval_item(int i)
{
    lv_area_t a;
    thumbring_item_box(i, S.m.rot, &a);
    lv_obj_invalidate_area(S.draw, &a);
}

static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    if (thumbring_spring(&S.m, now)) S.settled_t = 0; /* toward the detent */
    thumbring_inval(S.draw, &S.m);
    int cur = sel();
    if (cur != S.sel_shown) {
        S.sel_shown = cur;
        S.sel_t = now;
    }
    if (now - S.sel_t < 260) inval_centre();
    if (!S.m.drag && S.m.rot == S.m.target && S.commit && !S.done) {
        if (S.settled_t == 0) {
            S.settled_t = now;
            inval_item(cur); /* the landed ring */
        } else if (now - S.settled_t >= SETTLE_MS) {
            open_selection();
        }
    }
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    if (S.done || S.n == 0) return;
    const float st = step();
    switch (g->kind) {
    case G_DRAG_START:
        S.commit = false;
        thumbring_drag(&S.m, g, t);
        inval_item(sel()); /* the landed ring goes while turning */
        break;
    case G_DRAG_MOVE:
        thumbring_drag(&S.m, g, t);
        break;
    case G_DRAG_END: {
        if (!thumbring_drag(&S.m, g, t)) break;
        S.m.target = roundf(thumbring_coast(&S.m) / st) * st;
        S.m.vel = 0;
        S.commit = true; /* release to go */
        S.settled_t = 0;
        break;
    }
    case G_TAP: {
        if (g->x >= OPEN_X && g->x < OPEN_X + OPEN_W && g->y >= OPEN_Y && g->y < OPEN_Y + OPEN_H) {
            S.m.target = S.m.rot = roundf(S.m.rot / st) * st;
            open_selection();
            break;
        }
        int best = thumbring_hit(S.m.rot, g->x, g->y);   /* a thumbnail: brought round to the window, then opened */
        if (best >= 0) {
            S.m.target = S.m.rot + wrapf(-best * st - S.m.rot);
            S.commit = true;
            S.settled_t = 0;
        }
        break;
    }
    case G_SWIPE_L: /* across the centre: next / previous, without opening */
    case G_SWIPE_R:
        S.m.target = roundf(S.m.target / st) * st + (g->kind == G_SWIPE_L ? -st : st);
        S.commit = false;
        break;
    default: break;
    }
}

/* Input task, no LVGL lock: fixed geometry only. */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)ctx;
    int dx = x0 - CX, dy = y0 - CY;
    return dx * dx + dy * dy > CENTRE_R * CENTRE_R;
}

/* ------------------------------------------------------------ screen interface */
static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    const omni_settings_t *st = settings();
    const watch_theme_t *th = watch_theme();
    thumbring_item_t ti[MAX_ITEMS];
    bool in[SHELL_FACE_MAX] = {0};
    for (int i = 0; i < st->carousel_len && S.n < MAX_ITEMS - 1; i++) {
        uint8_t f = st->carousel[i];
        if (f >= th->face_count || f >= SHELL_FACE_MAX || in[f]) continue;
        in[f] = true;
        S.items[S.n++] = (item_t){.id = f, .shown = true};
    }
    for (uint8_t f = 0; f < th->face_count && f < SHELL_FACE_MAX && S.n < MAX_ITEMS - 1; f++)   /* O2: then the others */
        if (!in[f]) S.items[S.n++] = (item_t){.id = f};
    S.items[S.n++] = (item_t){.id = -1, .shown = true};
    for (int i = 0; i < S.n; i++)
        ti[i] = (thumbring_item_t){.id = (uint8_t)(S.items[i].id < 0 ? 0 : S.items[i].id), .settings = S.items[i].id < 0,
                                   .opa = S.items[i].shown ? LV_OPA_COVER : LV_OPA_60, .numeral = -1};

    thumbring_screen(parent, draw_cb, &S.root, &S.draw);
    thumbring_build(ti, S.n);

    const omni_nav_t *nav = shell_nav();
    const int start = nav ? omni_nav_current_face(nav, st) : 0;   /* the face shown, transient or not */
    for (int i = 0; i < S.n; i++)
        if (S.items[i].id == start) S.m.rot = S.m.target = S.m.drawn_rot = -i * step();
    S.sel_shown = sel();
    S.sel_t = lv_tick_get() - 1000; /* no fade on opening */
    S.m.t_prev = lv_tick_get();
    S.anim = lv_timer_create(anim_cb, ANIM_MS, NULL);
    board_set_touch_claim(claim, NULL);
}

static void destroy(void)
{
    board_set_touch_claim(NULL, NULL);
    if (S.anim) lv_timer_delete(S.anim);
    if (S.root) lv_obj_delete(S.root); /* the images before their buffers */
    thumbring_free();
    memset(&S, 0, sizeof S);
}

void scr_picker_open(void) { shell_post(UI_OPEN_PICKER, 0); }

const omni_screen_vt_t SCR_PICKER_VT = {
    .id = SCR_PICKER, .create = create, .destroy = destroy, .gesture = gesture,
};

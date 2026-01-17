/*
 * WebOS touchscreen input driver with on-screen controls overlay
 * For HP TouchPad (1024x768)
 *
 * APPROACH 4: SyncPoll (Synchronized Polling)
 * Events mark buttons as "pending". When polled, pending buttons are returned
 * and held for a fixed duration. This synchronizes with the menu's polling model.
 * Uses edge detection: only first press registers, must release to press again.
 */
#ifdef WEBOS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "libpicofe/input.h"
#include "plugin_lib.h"
#include "in_webos_touch.h"
#include "main.h"  /* for hud_msg */

#define TOUCH_SCREEN_W 1024
#define TOUCH_SCREEN_H 768

/* Touch zone definitions */
typedef struct {
    int x, y, w, h;
    int key;           /* DKEY_* value */
    const char *label;
} touch_zone_t;

/* Special key values for menu buttons (negative to distinguish from DKEY_*) */
#define MENU_KEY_UP    -10
#define MENU_KEY_DOWN  -11
#define MENU_KEY_MOK   -12
#define MENU_KEY_MBACK -13

/* Layout for 1024x768 landscape - game controls */
static const touch_zone_t game_touch_zones[] = {
    /* D-Pad - left side */
    { 80,  456, 80, 80,  DKEY_UP,       "UP" },
    { 80,  616, 80, 80,  DKEY_DOWN,     "DN" },
    { 0,   536, 80, 80,  DKEY_LEFT,     "LT" },
    { 160, 536, 80, 80,  DKEY_RIGHT,    "RT" },

    /* Action buttons - right side */
    { 864, 456, 80, 80,  DKEY_TRIANGLE, "/\\" },
    { 864, 616, 80, 80,  DKEY_CROSS,    "X" },
    { 784, 536, 80, 80,  DKEY_SQUARE,   "[]" },
    { 944, 536, 80, 80,  DKEY_CIRCLE,   "O" },

    /* Shoulder buttons - shifted down 20% (154px) */
    { 0,   154, 120, 60, DKEY_L1,       "L1" },
    { 0,   214, 120, 60, DKEY_L2,       "L2" },
    { 904, 154, 120, 60, DKEY_R1,       "R1" },
    { 904, 214, 120, 60, DKEY_R2,       "R2" },

    /* Start/Select - bottom center (at screen edge) */
    { 400, 708, 100, 60, DKEY_SELECT,   "SEL" },
    { 524, 708, 100, 60, DKEY_START,    "STA" },

    /* Menu button - top center */
    { 462, 0,   100, 50, -1,            "MENU" },
};

/* Simpler menu controls - positioned to not block UI text */
static const touch_zone_t menu_touch_zones[] = {
    /* Navigation - bottom left */
    { 50,  570, 120, 90, MENU_KEY_UP,    "UP" },
    { 190, 570, 120, 90, MENU_KEY_DOWN,  "DOWN" },

    /* Actions - bottom right */
    { 714, 570, 120, 90, MENU_KEY_MBACK, "BACK" },
    { 854, 570, 120, 90, MENU_KEY_MOK,   "OK" },
};

#define NUM_GAME_ZONES (sizeof(game_touch_zones) / sizeof(game_touch_zones[0]))
#define NUM_MENU_ZONES (sizeof(menu_touch_zones) / sizeof(menu_touch_zones[0]))

static const touch_zone_t *touch_zones = game_touch_zones;
#define NUM_TOUCH_ZONES NUM_GAME_ZONES

/* Track which fingers are pressing which zones */
#define MAX_FINGERS 10
static int finger_zones[MAX_FINGERS];
static int current_buttons = 0;

/* SyncPoll approach state */
static int pending_buttons = 0;     /* Buttons waiting to be returned */
static int active_buttons = 0;      /* Buttons currently "held" after polling */
static Uint32 active_until = 0;     /* Time when active buttons expire */
static int finger_pressing[4] = {0}; /* Track if finger is currently on each button */
static int needs_release[4] = {0};   /* Track if button needs release before re-press */

#define ACTIVE_DURATION_MS 120  /* How long buttons stay "active" after being polled */

static int overlay_visible = 1;
static int initialized = 0;
static int menu_mode = 0;

/* Track current screen dimensions for coordinate scaling */
static int current_screen_w = TOUCH_SCREEN_W;
static int current_screen_h = TOUCH_SCREEN_H;

/* Colors for overlay (RGB565) */
#define COLOR_BUTTON_NORMAL   0x4208
#define COLOR_BUTTON_PRESSED  0x841F
#define COLOR_BUTTON_BORDER   0xFFFF

static void draw_rect_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(surface, &rect, color);
}

static void draw_rect_outline_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color, int thickness)
{
    draw_rect_sdl(surface, x, y, w, thickness, color);
    draw_rect_sdl(surface, x, y + h - thickness, w, thickness, color);
    draw_rect_sdl(surface, x, y, thickness, h, color);
    draw_rect_sdl(surface, x + w - thickness, y, thickness, h, color);
}

void webos_touch_draw_overlay(void) {}

static int prev_screen_w = 0;
static int prev_screen_h = 0;
static int resolution_stable_frames = 0;
#define RESOLUTION_STABLE_THRESHOLD 5

void webos_touch_draw_overlay_sdl(SDL_Surface *screen)
{
    int i;
    float scale_x, scale_y;
    int screen_w, screen_h;
    const touch_zone_t *zones;
    int num_zones;

    if (!overlay_visible || !initialized || !screen)
        return;

    screen_w = screen->w;
    screen_h = screen->h;

    if (!menu_mode) {
        if (screen_w != prev_screen_w || screen_h != prev_screen_h) {
            printf("WebOS Touch: Resolution changed from %dx%d to %dx%d\n",
                   prev_screen_w, prev_screen_h, screen_w, screen_h);
            prev_screen_w = screen_w;
            prev_screen_h = screen_h;
            resolution_stable_frames = 0;
            hud_msg[0] = 0;
            hud_new_msg = 0;
            return;
        }

        if (resolution_stable_frames < RESOLUTION_STABLE_THRESHOLD) {
            resolution_stable_frames++;
            return;
        }
    }

    current_screen_w = screen_w;
    current_screen_h = screen_h;

    if (menu_mode) {
        zones = menu_touch_zones;
        num_zones = NUM_MENU_ZONES;
    } else {
        zones = game_touch_zones;
        num_zones = NUM_GAME_ZONES;
    }

    scale_x = (float)screen_w / TOUCH_SCREEN_W;
    scale_y = (float)screen_h / TOUCH_SCREEN_H;

    if (SDL_MUSTLOCK(screen))
        SDL_LockSurface(screen);

    for (i = 0; i < num_zones; i++) {
        const touch_zone_t *zone = &zones[i];
        int pressed = 0;
        int j;
        int draw_x, draw_y, draw_w, draw_h;

        for (j = 0; j < MAX_FINGERS; j++) {
            if (finger_zones[j] == i) {
                pressed = 1;
                break;
            }
        }

        draw_x = (int)(zone->x * scale_x);
        draw_y = (int)(zone->y * scale_y);
        draw_w = (int)(zone->w * scale_x);
        draw_h = (int)(zone->h * scale_y);

        if (pressed) {
            draw_rect_sdl(screen, draw_x, draw_y, draw_w, draw_h, COLOR_BUTTON_PRESSED);
        }

        draw_rect_outline_sdl(screen, draw_x, draw_y, draw_w, draw_h, COLOR_BUTTON_BORDER, 2);
    }

    if (SDL_MUSTLOCK(screen))
        SDL_UnlockSurface(screen);
}

static int find_zone(int x, int y)
{
    int i;
    int scaled_x, scaled_y;
    const touch_zone_t *zones;
    int num_zones;

    if (menu_mode) {
        zones = menu_touch_zones;
        num_zones = NUM_MENU_ZONES;
    } else {
        zones = game_touch_zones;
        num_zones = NUM_GAME_ZONES;
    }

    scaled_x = (x * TOUCH_SCREEN_W) / current_screen_w;
    scaled_y = (y * TOUCH_SCREEN_H) / current_screen_h;

    for (i = 0; i < num_zones; i++) {
        const touch_zone_t *zone = &zones[i];
        if (scaled_x >= zone->x && scaled_x < zone->x + zone->w &&
            scaled_y >= zone->y && scaled_y < zone->y + zone->h) {
            return i;
        }
    }
    return -1;
}

static int key_to_index(int key)
{
    switch (key) {
    case MENU_KEY_UP:    return 0;
    case MENU_KEY_DOWN:  return 1;
    case MENU_KEY_MBACK: return 2;
    case MENU_KEY_MOK:   return 3;
    }
    return -1;
}

static int index_to_pbtn(int idx)
{
    switch (idx) {
    case 0: return PBTN_UP;
    case 1: return PBTN_DOWN;
    case 2: return PBTN_MBACK;
    case 3: return PBTN_MOK;
    }
    return 0;
}

static void update_buttons(void)
{
    int i;
    const touch_zone_t *zones;
    int num_zones;

    current_buttons = 0;

    /* Reset finger_pressing state */
    for (i = 0; i < 4; i++) {
        finger_pressing[i] = 0;
    }

    if (menu_mode) {
        zones = menu_touch_zones;
        num_zones = NUM_MENU_ZONES;
    } else {
        zones = game_touch_zones;
        num_zones = NUM_GAME_ZONES;
    }

    for (i = 0; i < MAX_FINGERS; i++) {
        if (finger_zones[i] >= 0 && finger_zones[i] < num_zones) {
            int key = zones[finger_zones[i]].key;
            if (menu_mode) {
                int idx = key_to_index(key);
                if (idx >= 0) {
                    finger_pressing[idx] = 1;

                    /* If not waiting for release and finger just pressed, add to pending */
                    if (!needs_release[idx]) {
                        pending_buttons |= index_to_pbtn(idx);
                        needs_release[idx] = 1;  /* Must release before next press */
                    }
                }
            } else if (key >= 0) {
                current_buttons |= (1 << key);
            }
        }
    }

    /* Clear needs_release when finger is lifted */
    for (i = 0; i < 4; i++) {
        if (!finger_pressing[i]) {
            needs_release[i] = 0;
        }
    }
}

int webos_touch_event(const SDL_Event *event)
{
    int finger_id;
    int x, y;
    int zone;
    const touch_zone_t *zones;

    if (!initialized)
        return 0;

    zones = menu_mode ? menu_touch_zones : game_touch_zones;

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        finger_id = event->button.which;
        if (finger_id >= MAX_FINGERS)
            finger_id = 0;
        x = event->button.x;
        y = event->button.y;
        zone = find_zone(x, y);

        if (zone >= 0) {
            if (!menu_mode && zones[zone].key == -1) {
                return 2;
            }
            finger_zones[finger_id] = zone;
            update_buttons();
        }
        return 1;

    case SDL_MOUSEBUTTONUP:
        finger_id = event->button.which;
        if (finger_id >= MAX_FINGERS)
            finger_id = 0;
        finger_zones[finger_id] = -1;
        update_buttons();
        return 1;

    case SDL_MOUSEMOTION:
        if (event->motion.state & SDL_BUTTON(1)) {
            finger_id = event->motion.which;
            if (finger_id >= MAX_FINGERS)
                finger_id = 0;
            x = event->motion.x;
            y = event->motion.y;
            zone = find_zone(x, y);
            finger_zones[finger_id] = zone;
            update_buttons();
        }
        return 1;

    default:
        break;
    }

    return 0;
}

int webos_touch_get_buttons(void)
{
    return current_buttons;
}

void webos_touch_set_overlay_visible(int visible)
{
    overlay_visible = visible;
}

int webos_touch_init(void)
{
    int i;

    printf("WebOS Touch [SyncPoll]: Initializing on-screen controls\n");
    printf("WebOS Touch [SyncPoll]: Active duration: %d ms\n", ACTIVE_DURATION_MS);

    for (i = 0; i < MAX_FINGERS; i++) {
        finger_zones[i] = -1;
    }

    for (i = 0; i < 4; i++) {
        finger_pressing[i] = 0;
        needs_release[i] = 0;
    }

    current_buttons = 0;
    pending_buttons = 0;
    active_buttons = 0;
    active_until = 0;
    overlay_visible = 1;
    initialized = 1;

    printf("WebOS Touch [SyncPoll]: %d touch zones defined\n", (int)NUM_TOUCH_ZONES);
    return 0;
}

void webos_touch_finish(void)
{
    initialized = 0;
}

void webos_touch_set_menu_mode(int in_menu)
{
    int i;

    if (menu_mode != in_menu) {
        menu_mode = in_menu;
        for (i = 0; i < MAX_FINGERS; i++) {
            finger_zones[i] = -1;
        }
        for (i = 0; i < 4; i++) {
            finger_pressing[i] = 0;
            needs_release[i] = 0;
        }
        current_buttons = 0;
        pending_buttons = 0;
        active_buttons = 0;
        active_until = 0;
        resolution_stable_frames = RESOLUTION_STABLE_THRESHOLD;
        printf("WebOS Touch [SyncPoll]: Switched to %s mode\n", menu_mode ? "menu" : "game");
    }
}

int webos_touch_get_menu_buttons(void)
{
    Uint32 now = SDL_GetTicks();

    /* Check if active buttons have expired */
    if (active_buttons && now >= active_until) {
        active_buttons = 0;
    }

    /* If we have pending buttons, activate them */
    if (pending_buttons) {
        active_buttons = pending_buttons;
        active_until = now + ACTIVE_DURATION_MS;
        pending_buttons = 0;
    }

    return active_buttons;
}

#endif /* WEBOS */

/*
 * WebOS touchscreen input driver with on-screen controls overlay
 * For HP TouchPad (1024x768)
 * Uses SDL 2D surface drawing (works without GL)
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
    { 80,  280, 80, 80,  DKEY_UP,       "UP" },
    { 80,  440, 80, 80,  DKEY_DOWN,     "DN" },
    { 0,   360, 80, 80,  DKEY_LEFT,     "LT" },
    { 160, 360, 80, 80,  DKEY_RIGHT,    "RT" },

    /* Action buttons - right side (PlayStation layout) */
    { 864, 280, 80, 80,  DKEY_TRIANGLE, "/\\" },
    { 864, 440, 80, 80,  DKEY_CROSS,    "X" },
    { 784, 360, 80, 80,  DKEY_SQUARE,   "[]" },
    { 944, 360, 80, 80,  DKEY_CIRCLE,   "O" },

    /* Shoulder buttons - top corners */
    { 0,   0,   120, 60, DKEY_L1,       "L1" },
    { 0,   60,  120, 60, DKEY_L2,       "L2" },
    { 904, 0,   120, 60, DKEY_R1,       "R1" },
    { 904, 60,  120, 60, DKEY_R2,       "R2" },

    /* Start/Select - bottom center */
    { 400, 700, 100, 60, DKEY_SELECT,   "SEL" },
    { 524, 700, 100, 60, DKEY_START,    "STA" },

    /* Menu button - top center */
    { 462, 0,   100, 50, -1,            "MENU" },  /* -1 = special: open menu */
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

/* Alias for backward compatibility */
static const touch_zone_t *touch_zones = game_touch_zones;
#define NUM_TOUCH_ZONES NUM_GAME_ZONES

/* Track which fingers are pressing which zones */
#define MAX_FINGERS 10
static int finger_zones[MAX_FINGERS];
static int current_buttons = 0;
static int current_menu_buttons = 0;
static int pending_menu_buttons = 0;   /* One-shot: set on press, cleared after read */
static int overlay_visible = 1;
static int initialized = 0;
static int menu_mode = 0;  /* 0 = game mode, 1 = menu mode */

/* Track current screen dimensions for coordinate scaling */
static int current_screen_w = TOUCH_SCREEN_W;
static int current_screen_h = TOUCH_SCREEN_H;

/* Colors for overlay (RGB565) */
#define COLOR_BUTTON_NORMAL   0x4208  /* Dark gray */
#define COLOR_BUTTON_PRESSED  0x841F  /* Blue-ish */
#define COLOR_BUTTON_BORDER   0xFFFF  /* White */

/* Draw a filled rectangle to an SDL surface */
static void draw_rect_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(surface, &rect, color);
}

/* Draw a rectangle outline to an SDL surface */
static void draw_rect_outline_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color, int thickness)
{
    /* Top */
    draw_rect_sdl(surface, x, y, w, thickness, color);
    /* Bottom */
    draw_rect_sdl(surface, x, y + h - thickness, w, thickness, color);
    /* Left */
    draw_rect_sdl(surface, x, y, thickness, h, color);
    /* Right */
    draw_rect_sdl(surface, x + w - thickness, y, thickness, h, color);
}

void webos_touch_draw_overlay(void)
{
    /* This function is no longer used for drawing - kept for API compatibility */
    /* Drawing is now done via webos_touch_draw_overlay_sdl */
}

/* Track previous screen dimensions to detect resolution changes */
static int prev_screen_w = 0;
static int prev_screen_h = 0;
static int resolution_stable_frames = 0;
#define RESOLUTION_STABLE_THRESHOLD 5  /* Wait for resolution to stabilize */

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

    /* Resolution stability check only for game mode (prevents artifacts during transitions) */
    if (!menu_mode) {
        /* Detect resolution changes */
        if (screen_w != prev_screen_w || screen_h != prev_screen_h) {
            printf("WebOS Touch: Resolution changed from %dx%d to %dx%d\n",
                   prev_screen_w, prev_screen_h, screen_w, screen_h);
            prev_screen_w = screen_w;
            prev_screen_h = screen_h;
            resolution_stable_frames = 0;
            /* Clear any HUD message to prevent artifacts */
            hud_msg[0] = 0;
            hud_new_msg = 0;
            /* Don't draw during resolution transition to avoid artifacts */
            return;
        }

        /* Wait for resolution to stabilize before drawing overlay */
        if (resolution_stable_frames < RESOLUTION_STABLE_THRESHOLD) {
            resolution_stable_frames++;
            return;
        }
    }

    /* Store screen dimensions for touch coordinate scaling */
    current_screen_w = screen_w;
    current_screen_h = screen_h;

    /* Select zone array based on mode */
    if (menu_mode) {
        zones = menu_touch_zones;
        num_zones = NUM_MENU_ZONES;
    } else {
        zones = game_touch_zones;
        num_zones = NUM_GAME_ZONES;
    }

    /* Calculate scaling factors from touch coordinates (1024x768) to screen surface */
    scale_x = (float)screen_w / TOUCH_SCREEN_W;
    scale_y = (float)screen_h / TOUCH_SCREEN_H;

    if (SDL_MUSTLOCK(screen))
        SDL_LockSurface(screen);

    for (i = 0; i < num_zones; i++) {
        const touch_zone_t *zone = &zones[i];
        int pressed = 0;
        int j;
        int draw_x, draw_y, draw_w, draw_h;

        /* Check if any finger is on this zone */
        for (j = 0; j < MAX_FINGERS; j++) {
            if (finger_zones[j] == i) {
                pressed = 1;
                break;
            }
        }

        /* Scale touch zone coordinates to screen surface */
        draw_x = (int)(zone->x * scale_x);
        draw_y = (int)(zone->y * scale_y);
        draw_w = (int)(zone->w * scale_x);
        draw_h = (int)(zone->h * scale_y);

        /* Only draw filled background when pressed, otherwise just outline */
        if (pressed) {
            draw_rect_sdl(screen, draw_x, draw_y, draw_w, draw_h, COLOR_BUTTON_PRESSED);
        }

        /* Draw border */
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

    /* Select zone array based on mode */
    if (menu_mode) {
        zones = menu_touch_zones;
        num_zones = NUM_MENU_ZONES;
    } else {
        zones = game_touch_zones;
        num_zones = NUM_GAME_ZONES;
    }

    /* Scale touch coordinates from screen surface to touch zone coordinate space (1024x768) */
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

static void update_buttons(void)
{
    int i;
    const touch_zone_t *zones;
    int num_zones;

    current_buttons = 0;
    current_menu_buttons = 0;

    /* Select zone array based on mode */
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
                /* Map menu keys to PBTN_* values */
                switch (key) {
                case MENU_KEY_UP:    current_menu_buttons |= PBTN_UP;    break;
                case MENU_KEY_DOWN:  current_menu_buttons |= PBTN_DOWN;  break;
                case MENU_KEY_MOK:   current_menu_buttons |= PBTN_MOK;   break;
                case MENU_KEY_MBACK: current_menu_buttons |= PBTN_MBACK; break;
                }
            } else if (key >= 0) {
                current_buttons |= (1 << key);
            }
        }
    }

    /* Set pending buttons on press (one-shot mechanism) */
    if (menu_mode && current_menu_buttons) {
        pending_menu_buttons |= current_menu_buttons;
    }
}

/* Process SDL touch/mouse events */
int webos_touch_event(const SDL_Event *event)
{
    int finger_id;
    int x, y;
    int zone;
    const touch_zone_t *zones;

    if (!initialized)
        return 0;

    /* Select correct zone array based on mode */
    zones = menu_mode ? menu_touch_zones : game_touch_zones;

    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        finger_id = 0;  /* Mouse is finger 0 */
        x = event->button.x;
        y = event->button.y;
        zone = find_zone(x, y);

        if (zone >= 0) {
            /* Check for menu button (only in game mode) */
            if (!menu_mode && zones[zone].key == -1) {
                /* Return special value to indicate menu request */
                return 2;
            }
            finger_zones[finger_id] = zone;
            update_buttons();
        }
        return 1;

    case SDL_MOUSEBUTTONUP:
        finger_id = 0;
        finger_zones[finger_id] = -1;
        update_buttons();
        return 1;

    case SDL_MOUSEMOTION:
        if (event->motion.state & SDL_BUTTON(1)) {
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

    printf("WebOS Touch: Initializing on-screen controls\n");

    for (i = 0; i < MAX_FINGERS; i++) {
        finger_zones[i] = -1;
    }

    current_buttons = 0;
    overlay_visible = 1;
    initialized = 1;

    printf("WebOS Touch: %d touch zones defined\n", (int)NUM_TOUCH_ZONES);
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
        /* Clear finger zones when switching modes */
        for (i = 0; i < MAX_FINGERS; i++) {
            finger_zones[i] = -1;
        }
        current_buttons = 0;
        current_menu_buttons = 0;
        pending_menu_buttons = 0;
        /* Mark resolution as stable so overlay draws immediately in new mode */
        resolution_stable_frames = RESOLUTION_STABLE_THRESHOLD;
        printf("WebOS Touch: Switched to %s mode\n", menu_mode ? "menu" : "game");
    }
}

int webos_touch_get_menu_buttons(void)
{
    int buttons = pending_menu_buttons;

    /* Only clear pending if we're returning something - avoids losing taps */
    if (buttons)
        pending_menu_buttons = 0;

    return buttons;
}

#endif /* WEBOS */

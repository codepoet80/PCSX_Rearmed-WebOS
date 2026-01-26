/*
 * WebOS touchscreen input driver with on-screen controls overlay
 * For HP TouchPad (1024x768)
 *
 * APPROACH 6: TapKey
 * Menu mode: Each tap immediately injects a complete keystroke (KEY_DOWN + KEY_UP).
 * No state tracking needed - one tap = one menu action.
 * Game mode: Standard press/release tracking for continuous input.
 */
#ifdef WEBOS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <SDL.h>

#include "libpicofe/input.h"
#include "plugin_lib.h"
#include "in_webos_touch.h"
#include "main.h"  /* for hud_msg */

/* Button icons */
typedef struct {
    unsigned char *pixels;  /* RGBA data */
    int width;
    int height;
    int loaded;
} icon_t;

/* Menu icons */
static icon_t menu_icons[6];  /* UP, DOWN, LEFT, RIGHT, OK, BACK */
#define ICON_UP    0
#define ICON_DOWN  1
#define ICON_LEFT  2
#define ICON_RIGHT 3
#define ICON_OK    4
#define ICON_BACK  5

/* Game control icons (action buttons) */
static icon_t game_icons[4];  /* TRIANGLE, CIRCLE, CROSS, SQUARE */
#define ICON_TRIANGLE 0
#define ICON_CIRCLE   1
#define ICON_CROSS    2
#define ICON_SQUARE   3

#define TOUCH_SCREEN_W 1024
#define TOUCH_SCREEN_H 768

/* Touch zone definitions */
typedef struct {
    int x, y, w, h;
    int key;           /* DKEY_* value or SDL keycode for menu */
    const char *label;
} touch_zone_t;

/* Special key values for menu buttons (negative to distinguish from DKEY_*) */
#define MENU_KEY_UP    -10
#define MENU_KEY_DOWN  -11
#define MENU_KEY_LEFT  -12
#define MENU_KEY_RIGHT -13
#define MENU_KEY_MOK   -14
#define MENU_KEY_MBACK -15

/* Special key values for D-pad diagonals */
#define DKEY_UP_RIGHT    -20
#define DKEY_UP_LEFT     -21
#define DKEY_DOWN_RIGHT  -22
#define DKEY_DOWN_LEFT   -23

/* Layout for 1024x768 landscape - game controls */
static const touch_zone_t game_touch_zones[] = {
    /* D-Pad - cardinal directions */
    { 80,  525, 80, 80,  DKEY_UP,       "UP" },
    { 80,  685, 80, 80,  DKEY_DOWN,     "DN" },
    { 0,   605, 80, 80,  DKEY_LEFT,     "LT" },
    { 160, 605, 80, 80,  DKEY_RIGHT,    "RT" },

    /* D-Pad - diagonal zones (no outline drawn) */
    { 160, 525, 80, 80,  DKEY_UP_RIGHT,   "" },
    { 0,   525, 80, 80,  DKEY_UP_LEFT,    "" },
    { 160, 685, 80, 80,  DKEY_DOWN_RIGHT, "" },
    { 0,   685, 80, 80,  DKEY_DOWN_LEFT,  "" },

    /* Action buttons - right side */
    { 864, 525, 80, 80,  DKEY_TRIANGLE, "/\\" },
    { 864, 685, 80, 80,  DKEY_CROSS,    "X" },
    { 784, 605, 80, 80,  DKEY_SQUARE,   "[]" },
    { 944, 605, 80, 80,  DKEY_CIRCLE,   "O" },

    /* Shoulder buttons */
    { 0,   338, 120, 60, DKEY_L1,       "L1" },
    { 0,   398, 120, 60, DKEY_L2,       "L2" },
    { 904, 338, 120, 60, DKEY_R1,       "R1" },
    { 904, 398, 120, 60, DKEY_R2,       "R2" },

    /* Start/Select - bottom center (at screen edge) */
    { 400, 708, 100, 60, DKEY_SELECT,   "SEL" },
    { 524, 708, 100, 60, DKEY_START,    "STA" },

    /* Menu button - top center */
    { 462, 0,   100, 50, -1,            "MENU" },
};

/* Menu controls - D-pad style on left, actions on right */
static const touch_zone_t menu_touch_zones[] = {
    /* D-pad navigation - bottom left */
    { 100, 510, 100, 80, MENU_KEY_UP,    "UP" },     /* Top center */
    { 100, 678, 100, 80, MENU_KEY_DOWN,  "DOWN" },   /* Bottom center */
    { 10,  594, 100, 80, MENU_KEY_LEFT,  "LEFT" },   /* Left */
    { 190, 594, 100, 80, MENU_KEY_RIGHT, "RIGHT" },  /* Right */

    /* Actions - bottom right */
    { 734, 594, 120, 80, MENU_KEY_MBACK, "BACK" },
    { 874, 594, 120, 80, MENU_KEY_MOK,   "OK" },
};

#define NUM_GAME_ZONES (sizeof(game_touch_zones) / sizeof(game_touch_zones[0]))
#define NUM_MENU_ZONES (sizeof(menu_touch_zones) / sizeof(menu_touch_zones[0]))

/* Track which fingers are pressing which zones */
#define MAX_FINGERS 10
static int finger_zones[MAX_FINGERS];
static int current_buttons = 0;

static int overlay_visible = 1;
static int initialized = 0;
static int menu_mode = 0;

/* Track current screen dimensions for coordinate scaling */
static int current_screen_w = TOUCH_SCREEN_W;
static int current_screen_h = TOUCH_SCREEN_H;

/* Colors for overlay (RGB565) */
#define COLOR_BUTTON_NORMAL   0x4208
#define COLOR_BUTTON_PRESSED  0x841F
#define COLOR_BUTTON_BORDER   0xCE79  /* #cdcdcd in RGB565 */

/* Border color components for alpha blending (60% opacity) */
#define BORDER_R 205
#define BORDER_G 205
#define BORDER_B 205
#define BORDER_ALPHA 153  /* 60% of 255 */

/* Pressed highlight for alpha blending (60% opacity) */
#define PRESSED_R 130
#define PRESSED_G 8
#define PRESSED_B 248
#define PRESSED_ALPHA 153  /* 60% of 255 */

/* Forward declarations */
static void update_buttons(void);

/* Inject an SDL keyboard event */
static void inject_key_event(SDLKey key, int pressed)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));

    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.mod = KMOD_NONE;
    event.key.keysym.unicode = 0;

    SDL_PushEvent(&event);
}

/* Map menu key to SDL keycode */
static SDLKey menu_key_to_sdlkey(int menu_key)
{
    switch (menu_key) {
    case MENU_KEY_UP:    return SDLK_UP;
    case MENU_KEY_DOWN:  return SDLK_DOWN;
    case MENU_KEY_LEFT:  return SDLK_LEFT;
    case MENU_KEY_RIGHT: return SDLK_RIGHT;
    case MENU_KEY_MOK:   return SDLK_RETURN;
    case MENU_KEY_MBACK: return SDLK_ESCAPE;
    }
    return SDLK_UNKNOWN;
}

static void draw_rect_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(surface, &rect, color);
}

/* Draw a rectangle with alpha blending */
static void draw_rect_alpha(SDL_Surface *surface, int x, int y, int w, int h,
                            int r, int g, int b, int alpha)
{
    int px, py;
    Uint16 *dst;
    int dst_pitch;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surface->w) w = surface->w - x;
    if (y + h > surface->h) h = surface->h - y;
    if (w <= 0 || h <= 0) return;

    dst = (Uint16 *)surface->pixels;
    dst_pitch = surface->pitch / 2;

    for (py = y; py < y + h; py++) {
        for (px = x; px < x + w; px++) {
            Uint16 *pixel = dst + py * dst_pitch + px;
            Uint16 bg = *pixel;

            int bg_r = ((bg >> 11) & 0x1F) << 3;
            int bg_g = ((bg >> 5) & 0x3F) << 2;
            int bg_b = (bg & 0x1F) << 3;

            int out_r = (r * alpha + bg_r * (255 - alpha)) / 255;
            int out_g = (g * alpha + bg_g * (255 - alpha)) / 255;
            int out_b = (b * alpha + bg_b * (255 - alpha)) / 255;

            *pixel = ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
        }
    }
}

static void draw_rect_outline_sdl(SDL_Surface *surface, int x, int y, int w, int h, Uint16 color, int thickness)
{
    draw_rect_sdl(surface, x, y, w, thickness, color);
    draw_rect_sdl(surface, x, y + h - thickness, w, thickness, color);
    draw_rect_sdl(surface, x, y, thickness, h, color);
    draw_rect_sdl(surface, x + w - thickness, y, thickness, h, color);
}

/* Draw outline with alpha blending */
static void draw_rect_outline_alpha(SDL_Surface *surface, int x, int y, int w, int h,
                                    int r, int g, int b, int alpha, int thickness)
{
    draw_rect_alpha(surface, x, y, w, thickness, r, g, b, alpha);
    draw_rect_alpha(surface, x, y + h - thickness, w, thickness, r, g, b, alpha);
    draw_rect_alpha(surface, x, y, thickness, h, r, g, b, alpha);
    draw_rect_alpha(surface, x + w - thickness, y, thickness, h, r, g, b, alpha);
}

/* Load a PNG file with alpha channel */
static int load_icon_png(const char *filename, icon_t *icon)
{
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    int y;

    fp = fopen(filename, "rb");
    if (!fp) {
        printf("WebOS Touch: Could not open icon %s\n", filename);
        return -1;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return -1;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);

    icon->width = png_get_image_width(png_ptr, info_ptr);
    icon->height = png_get_image_height(png_ptr, info_ptr);

    /* Convert to RGBA */
    png_set_expand(png_ptr);
    if (png_get_bit_depth(png_ptr, info_ptr) == 16)
        png_set_strip_16(png_ptr);
    if (png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY ||
        png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);
    if (!(png_get_color_type(png_ptr, info_ptr) & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    /* Allocate memory */
    icon->pixels = malloc(icon->width * icon->height * 4);
    if (!icon->pixels) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return -1;
    }

    row_pointers = malloc(sizeof(png_bytep) * icon->height);
    for (y = 0; y < icon->height; y++) {
        row_pointers[y] = icon->pixels + y * icon->width * 4;
    }

    png_read_image(png_ptr, row_pointers);
    free(row_pointers);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    icon->loaded = 1;
    printf("WebOS Touch: Loaded icon %s (%dx%d)\n", filename, icon->width, icon->height);
    return 0;
}

/* Load all icons (menu and game) */
static void load_icons(void)
{
    /* Try multiple paths */
    const char *paths[] = {
        "/media/cryptofs/apps/usr/palm/applications/com.starkka.pcsxrearmed/",
        "./",
        NULL
    };
    const char *menu_icon_files[6] = {
        "menu-up.png",
        "menu-down.png",
        "menu-backward.png",  /* LEFT - reuse backward arrow */
        "menu-forward.png",   /* RIGHT - reuse forward arrow */
        "control-circle.png", /* OK */
        "control-cross.png"   /* BACK */
    };
    const char *game_icon_files[4] = {
        "control-triangle.png",
        "control-circle.png",
        "control-cross.png",
        "control-square.png"
    };
    int i, p;

    /* Initialize menu icons */
    for (i = 0; i < 6; i++) {
        menu_icons[i].loaded = 0;
        menu_icons[i].pixels = NULL;
    }

    /* Initialize game icons */
    for (i = 0; i < 4; i++) {
        game_icons[i].loaded = 0;
        game_icons[i].pixels = NULL;
    }

    /* Load from available paths */
    for (p = 0; paths[p] != NULL; p++) {
        char path[256];

        /* Load menu icons */
        for (i = 0; i < 6; i++) {
            if (menu_icons[i].loaded)
                continue;
            snprintf(path, sizeof(path), "%s%s", paths[p], menu_icon_files[i]);
            load_icon_png(path, &menu_icons[i]);
        }

        /* Load game icons */
        for (i = 0; i < 4; i++) {
            if (game_icons[i].loaded)
                continue;
            snprintf(path, sizeof(path), "%s%s", paths[p], game_icon_files[i]);
            load_icon_png(path, &game_icons[i]);
        }
    }
}

/* Free all icons */
static void free_icons(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        if (menu_icons[i].pixels) {
            free(menu_icons[i].pixels);
            menu_icons[i].pixels = NULL;
        }
        menu_icons[i].loaded = 0;
    }
    for (i = 0; i < 4; i++) {
        if (game_icons[i].pixels) {
            free(game_icons[i].pixels);
            game_icons[i].pixels = NULL;
        }
        game_icons[i].loaded = 0;
    }
}

/* Blit an RGBA icon to an RGB565 surface with alpha blending */
static void blit_icon(SDL_Surface *surface, icon_t *icon, int dest_x, int dest_y, int dest_w, int dest_h)
{
    int x, y;
    int src_x, src_y;
    Uint16 *dst;
    int dst_pitch;

    if (!icon->loaded || !icon->pixels)
        return;

    dst = (Uint16 *)surface->pixels;
    dst_pitch = surface->pitch / 2;

    for (y = 0; y < dest_h; y++) {
        int screen_y = dest_y + y;
        if (screen_y < 0 || screen_y >= surface->h)
            continue;

        src_y = y * icon->height / dest_h;

        for (x = 0; x < dest_w; x++) {
            int screen_x = dest_x + x;
            if (screen_x < 0 || screen_x >= surface->w)
                continue;

            src_x = x * icon->width / dest_w;

            unsigned char *src_pixel = icon->pixels + (src_y * icon->width + src_x) * 4;
            unsigned char r = src_pixel[0];
            unsigned char g = src_pixel[1];
            unsigned char b = src_pixel[2];
            unsigned char a = src_pixel[3];

            if (a == 0)
                continue;  /* Fully transparent */

            Uint16 *dst_pixel = dst + screen_y * dst_pitch + screen_x;

            if (a == 255) {
                /* Fully opaque */
                *dst_pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                /* Alpha blend */
                Uint16 bg = *dst_pixel;
                int bg_r = ((bg >> 11) & 0x1F) << 3;
                int bg_g = ((bg >> 5) & 0x3F) << 2;
                int bg_b = (bg & 0x1F) << 3;

                int out_r = (r * a + bg_r * (255 - a)) / 255;
                int out_g = (g * a + bg_g * (255 - a)) / 255;
                int out_b = (b * a + bg_b * (255 - a)) / 255;

                *dst_pixel = ((out_r >> 3) << 11) | ((out_g >> 2) << 5) | (out_b >> 3);
            }
        }
    }
}

/* GL version - currently a no-op, touch controls drawn via SDL surface */
void webos_touch_draw_overlay(void) {}

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

        /* Skip drawing for diagonal zones (no outline, no highlight) */
        if (zone->key == DKEY_UP_RIGHT || zone->key == DKEY_UP_LEFT ||
            zone->key == DKEY_DOWN_RIGHT || zone->key == DKEY_DOWN_LEFT) {
            continue;
        }

        if (pressed) {
            /* Alpha blended pressed highlight for game mode, solid for menu */
            if (menu_mode) {
                draw_rect_sdl(screen, draw_x, draw_y, draw_w, draw_h, COLOR_BUTTON_PRESSED);
            } else {
                draw_rect_alpha(screen, draw_x, draw_y, draw_w, draw_h,
                                PRESSED_R, PRESSED_G, PRESSED_B, PRESSED_ALPHA);
            }
        }

        /* Draw button outline - alpha blended for game mode, solid for menu */
        if (menu_mode) {
            draw_rect_outline_sdl(screen, draw_x, draw_y, draw_w, draw_h, COLOR_BUTTON_BORDER, 2);
        } else {
            draw_rect_outline_alpha(screen, draw_x, draw_y, draw_w, draw_h,
                                    BORDER_R, BORDER_G, BORDER_B, BORDER_ALPHA, 2);
        }

        /* Draw icons for menu buttons */
        if (menu_mode) {
            int icon_size = (draw_w < draw_h ? draw_w : draw_h) * 2 / 3;
            int icon_x = draw_x + (draw_w - icon_size) / 2;
            int icon_y = draw_y + (draw_h - icon_size) / 2;
            icon_t *icon = NULL;

            switch (zone->key) {
            case MENU_KEY_UP:
                icon = &menu_icons[ICON_UP];
                break;
            case MENU_KEY_DOWN:
                icon = &menu_icons[ICON_DOWN];
                break;
            case MENU_KEY_LEFT:
                icon = &menu_icons[ICON_LEFT];
                break;
            case MENU_KEY_RIGHT:
                icon = &menu_icons[ICON_RIGHT];
                break;
            case MENU_KEY_MOK:
                icon = &menu_icons[ICON_OK];
                break;
            case MENU_KEY_MBACK:
                icon = &menu_icons[ICON_BACK];
                break;
            }

            if (icon && icon->loaded) {
                blit_icon(screen, icon, icon_x, icon_y, icon_size, icon_size);
            }
        } else {
            /* Draw icons for game action buttons */
            int icon_size = (draw_w < draw_h ? draw_w : draw_h) * 2 / 3;
            int icon_x = draw_x + (draw_w - icon_size) / 2;
            int icon_y = draw_y + (draw_h - icon_size) / 2;
            icon_t *icon = NULL;

            switch (zone->key) {
            case DKEY_TRIANGLE:
                icon = &game_icons[ICON_TRIANGLE];
                break;
            case DKEY_CIRCLE:
                icon = &game_icons[ICON_CIRCLE];
                break;
            case DKEY_CROSS:
                icon = &game_icons[ICON_CROSS];
                break;
            case DKEY_SQUARE:
                icon = &game_icons[ICON_SQUARE];
                break;
            }

            if (icon && icon->loaded) {
                blit_icon(screen, icon, icon_x, icon_y, icon_size, icon_size);
            }
        }
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

static void update_buttons(void)
{
    int i;
    const touch_zone_t *zones;
    int num_zones;

    current_buttons = 0;

    if (menu_mode) {
        /* Menu mode: buttons handled via tap-to-keystroke in event handler */
        return;
    }

    zones = game_touch_zones;
    num_zones = NUM_GAME_ZONES;

    /* Calculate current button states for game mode */
    for (i = 0; i < MAX_FINGERS; i++) {
        if (finger_zones[i] >= 0 && finger_zones[i] < num_zones) {
            int zone_idx = finger_zones[i];
            int key = zones[zone_idx].key;

            if (key >= 0) {
                current_buttons |= (1 << key);
            } else {
                /* Handle D-pad diagonals */
                switch (key) {
                case DKEY_UP_RIGHT:
                    current_buttons |= (1 << DKEY_UP) | (1 << DKEY_RIGHT);
                    break;
                case DKEY_UP_LEFT:
                    current_buttons |= (1 << DKEY_UP) | (1 << DKEY_LEFT);
                    break;
                case DKEY_DOWN_RIGHT:
                    current_buttons |= (1 << DKEY_DOWN) | (1 << DKEY_RIGHT);
                    break;
                case DKEY_DOWN_LEFT:
                    current_buttons |= (1 << DKEY_DOWN) | (1 << DKEY_LEFT);
                    break;
                }
            }
        }
    }
}

int webos_touch_event(const SDL_Event *event)
{
    int finger_id;
    int x, y;
    int zone;
    int i;
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

            if (menu_mode) {
                /* Menu mode: inject a complete keystroke immediately on tap */
                SDLKey sdlkey = menu_key_to_sdlkey(zones[zone].key);
                if (sdlkey != SDLK_UNKNOWN) {
                    inject_key_event(sdlkey, 1);  /* KEY_DOWN */
                    inject_key_event(sdlkey, 0);  /* KEY_UP */
                }
            } else {
                update_buttons();
            }
        }
        return 1;

    case SDL_MOUSEBUTTONUP:
        finger_id = event->button.which;
        if (finger_id >= MAX_FINGERS)
            finger_id = 0;

        if (menu_mode) {
            /* Menu mode: just clear visual state, keystroke already sent on tap */
            for (i = 0; i < MAX_FINGERS; i++) {
                finger_zones[i] = -1;
            }
        } else {
            /* Game mode: simple finger tracking (original behavior) */
            finger_zones[finger_id] = -1;
            update_buttons();
        }
        return 1;

    case SDL_MOUSEMOTION:
        /* In menu mode, don't track motion - only care about press/release */
        if (menu_mode)
            return 1;

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
    SDL_Event event;

    printf("WebOS Touch [TapKey]: Initializing on-screen controls\n");
    printf("WebOS Touch [TapKey]: Menu uses tap-to-keystroke\n");

    for (i = 0; i < MAX_FINGERS; i++) {
        finger_zones[i] = -1;
    }

    current_buttons = 0;
    overlay_visible = 1;
    menu_mode = 0;

    /* Flush any stale mouse/touch events from SDL queue */
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT,
           SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) |
           SDL_EVENTMASK(SDL_MOUSEBUTTONUP) |
           SDL_EVENTMASK(SDL_MOUSEMOTION)) > 0) {
        /* discard */
    }

    initialized = 1;

    /* Load menu button icons */
    load_icons();

    printf("WebOS Touch [TapKey]: %d game zones, %d menu zones\n",
           (int)NUM_GAME_ZONES, (int)NUM_MENU_ZONES);
    return 0;
}

void webos_touch_finish(void)
{
    free_icons();
    initialized = 0;
}

void webos_touch_set_menu_mode(int in_menu)
{
    int i;
    SDL_Event event;

    if (menu_mode != in_menu) {
        menu_mode = in_menu;
        for (i = 0; i < MAX_FINGERS; i++) {
            finger_zones[i] = -1;
        }
        current_buttons = 0;

        /* Flush any stale touch events when switching modes */
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT,
               SDL_EVENTMASK(SDL_MOUSEBUTTONDOWN) |
               SDL_EVENTMASK(SDL_MOUSEBUTTONUP) |
               SDL_EVENTMASK(SDL_MOUSEMOTION)) > 0) {
            /* discard */
        }

        printf("WebOS Touch [TapKey]: Switched to %s mode\n", menu_mode ? "menu" : "game");
    }
}

int webos_touch_get_menu_buttons(void)
{
    /* TapKey approach: return nothing here since we inject keyboard events
     * directly on touch. The keyboard driver handles the actual input. */
    return 0;
}

#endif /* WEBOS */

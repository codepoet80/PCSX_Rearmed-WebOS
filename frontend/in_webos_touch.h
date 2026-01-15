/*
 * WebOS touchscreen input driver header
 */
#ifndef IN_WEBOS_TOUCH_H
#define IN_WEBOS_TOUCH_H

#ifdef WEBOS

#include <SDL.h>

/* Initialize touch input system */
int webos_touch_init(void);

/* Clean up touch input system */
void webos_touch_finish(void);

/* Process an SDL event, returns:
 * 0 = event not handled
 * 1 = event handled (touch input)
 * 2 = menu button pressed
 */
int webos_touch_event(const SDL_Event *event);

/* Get current button state (bitmask of DKEY_* values) */
int webos_touch_get_buttons(void);

/* Get current button state for menu navigation (bitmask of PBTN_* values) */
int webos_touch_get_menu_buttons(void);

/* Set menu mode (uses simpler controls at bottom of screen) */
void webos_touch_set_menu_mode(int in_menu);

/* Draw the on-screen controls overlay (GL version - legacy) */
void webos_touch_draw_overlay(void);

/* Draw the on-screen controls overlay (SDL surface version) */
void webos_touch_draw_overlay_sdl(SDL_Surface *screen);

/* Show/hide the overlay */
void webos_touch_set_overlay_visible(int visible);

#endif /* WEBOS */

#endif /* IN_WEBOS_TOUCH_H */

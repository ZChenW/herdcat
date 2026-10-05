#ifndef BONGOCAT_PLATFORM_FONT_PANEL_H
#define BONGOCAT_PLATFORM_FONT_PANEL_H

#include "config/config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wl_surface;

// The switch card, in cat-surface coordinates. The panel places itself.
typedef struct {
  int x, y, w, h;
} font_panel_anchor_t;

// One panel. Created when the font name is clicked and destroyed on close.
bool font_panel_surface_is_open(void);
void font_panel_surface_close(void);
// toggle opens, or closes when this output already holds the panel.
// timeout_ms is merged with the panel's idle wake. It is never set to 0.
void font_panel_surface_sync(size_t index, const config_t *config,
                             font_panel_anchor_t card, int surface_h,
                             int64_t now_ms, int *timeout_ms, bool toggle);
void font_panel_surface_activity(int64_t now_ms);
// True when the pointer is on this output's panel.
bool font_panel_surface_covers(size_t index);
bool font_panel_surface_armed(void);
void font_panel_surface_wheel(int discrete);
// True when the pointer is on the panel. The event is then consumed.
bool font_panel_surface_button(uint32_t button, uint32_t state);
// One-shot family from a cell click. Empty means the default face.
// The family under the pointer while it is on a cell, otherwise NULL.
const char *font_panel_surface_hover(void);
bool font_panel_surface_take_choice(char *out, size_t cap);
bool font_panel_surface_enter(struct wl_surface *target, double x, double y);
bool font_panel_surface_motion(double x, double y);
// Clears the hover. Does not reset the idle timer.
void font_panel_surface_left(void);
void font_panel_surface_output_gone(size_t index);
void font_panel_surface_select(const char *family);
void font_panel_surface_language(bool english);
void font_panel_surface_margin(int margin_y);

#endif

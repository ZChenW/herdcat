#ifndef HERDCAT_DRAG_H
#define HERDCAT_DRAG_H

#include "config/config.h"

typedef struct {
  int x, y, width, height;
} drag_rect_t;

int drag_default_x(const config_t *config, int surface_width, int cat_width);
void drag_clamp(int *x, int *y, int surface_width, int cat_width,
                int output_height, int overlay_height);
drag_rect_t drag_cat_rect(int x, const config_t *config, int cat_width,
                          int cat_height, int overlay_height);
bool drag_exceeds_threshold(double start_x, double start_y, double x, double y);
// Margin for a drag that began at press_margin. Both pointer values are in the
// surface coordinates fixed at button press.
int drag_margin_follow(int press_margin, double grab_y, double pointer_y,
                       bool top);
// Resolve both margins from press-time coordinates, then clamp in output space.
void drag_follow_position(int press_x, int press_y, double grab_x,
                          double grab_y, double pointer_x, double pointer_y,
                          bool top, int output_width, int cat_width,
                          int output_height, int surface_height, int *x,
                          int *y);
// Load: 0 found, 1 absent, -1 error. Save/reset: 0 success, -1 error.
int drag_position_load(const char *output, int *x, int *y);
int drag_position_save(const char *output, int x, int y);
int drag_position_reset(const char *output);

#endif

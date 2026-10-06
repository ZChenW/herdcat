#ifndef HERDCAT_OVERLAY_GEOMETRY_H
#define HERDCAT_OVERLAY_GEOMETRY_H

#include "config/config.h"
#include "platform/drag.h"

#include <stdint.h>

typedef struct {
  int width, cat_x_in_surface;
} overlay_extent_t;

typedef struct {
  int margin_x, cat_x_in_surface;
} overlay_placement_t;

overlay_extent_t overlay_extent(const config_t *config, int output_width);
// Keep origins on physical pixels so translated rasterization is identical.
int overlay_scaled_width(int width, int output_width, uint32_t scale);
overlay_placement_t overlay_place(int output_x, int cat_width, int width,
                                  int output_width, uint32_t scale);
// Output-space card, independent of the main surface width.
drag_rect_t overlay_card_rect(drag_rect_t card, int margin_x, int margin_y,
                              bool top, int output_height, int surface_height);

#endif

#ifndef HERDCAT_OVERLAY_VERTICAL_H
#define HERDCAT_OVERLAY_VERTICAL_H

#include "config/config.h"
#include "graphics/signs.h"

typedef struct {
  sign_orientation_t orientation;
  int position_y, margin_y, cat_y_in_surface, cat_y_in_output;
} overlay_vertical_t;

// Use the previous orientation only when there is actual placement history.
sign_orientation_t overlay_orientation(const config_t *config, int cat_y,
                                       int output_height, int surface_height,
                                       bool has_history,
                                       sign_orientation_t previous);
// position_y retains the existing anchor-relative saved displacement. Bottom
// records still measure upward from the original resting cat position.
overlay_vertical_t overlay_place_vertical(const config_t *config,
                                          int position_y, int output_height,
                                          int surface_height, int above_y,
                                          bool has_history,
                                          sign_orientation_t previous);

#endif

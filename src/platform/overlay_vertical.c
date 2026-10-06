#include "platform/overlay_vertical.h"

#include <limits.h>
#include <stdint.h>

static int bounded(int64_t value, int high) {
  return (int)(value < 0 ? 0 : value > high ? high : value);
}
sign_orientation_t overlay_orientation(const config_t *config, int cat_y,
                                       int output_height, int surface_height,
                                       bool has_history,
                                       sign_orientation_t previous) {
  if (!config || config->sign_style == SIGN_STYLE_OFF ||
      config->overlay_opacity > 0 || config->cat_height <= 0 ||
      output_height < surface_height)
    return SIGN_ABOVE;
  int clearance = sign_clearance(config->sign_style, config->cat_height);
  int64_t threshold = clearance;
  if (has_history && previous == SIGN_BELOW)
    threshold += 24;
  if (cat_y >= threshold ||
      (int64_t)cat_y + config->cat_height + clearance > output_height)
    return SIGN_ABOVE;
  return SIGN_BELOW;
}
overlay_vertical_t overlay_place_vertical(const config_t *config,
                                          int position_y, int output_height,
                                          int surface_height, int above_y,
                                          bool has_history,
                                          sign_orientation_t previous) {
  overlay_vertical_t out = {.orientation = SIGN_ABOVE};
  if (!config || output_height <= 0 || surface_height <= 0)
    return out;
  bool top = config->overlay_position == POSITION_TOP;
  int max_margin =
      output_height > surface_height ? output_height - surface_height : 0;
  int base =
      bounded((int64_t)output_height - surface_height + above_y, INT_MAX);
  bool legacy = config->overlay_opacity > 0 ||
                config->sign_style == SIGN_STYLE_OFF ||
                output_height < surface_height;
  if (legacy) {
    out.position_y = out.margin_y = bounded(position_y, max_margin);
    out.cat_y_in_surface = above_y;
    out.cat_y_in_output =
        (top ? out.margin_y : max_margin - out.margin_y) + above_y;
    return out;
  }
  int max_cat = output_height > config->cat_height
                    ? output_height - config->cat_height
                    : 0;
  out.position_y = bounded(position_y, top ? max_cat : base);
  int cat_y = top ? out.position_y : base - out.position_y;
  out.orientation = overlay_orientation(config, cat_y, output_height,
                                        surface_height, has_history, previous);
  // Reserve the desk lift except where the output edge leaves less room.
  int local_y = out.orientation == SIGN_BELOW
                    ? (int)(((int64_t)config->cat_height * 8 + 109) / 110)
                    : above_y;
  int origin = bounded((int64_t)cat_y - local_y, max_margin);
  out.margin_y = top ? origin : max_margin - origin;
  out.cat_y_in_surface = cat_y - origin;
  out.cat_y_in_output = cat_y;
  return out;
}

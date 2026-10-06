#include "platform/overlay_geometry.h"

#include "core/herdcat.h"
#include "graphics/signs.h"

#include <limits.h>
#include <stdint.h>

static int clamp_wide(int64_t value, int low, int high) {
  return (int)(value < low ? low : value > high ? high : value);
}
static int pixel_step(uint32_t scale) {
  uint32_t a = 120, b = scale;
  while (b) {
    uint32_t rest = a % b;
    a = b;
    b = rest;
  }
  return (int)(120 / a);
}
overlay_extent_t overlay_extent(const config_t *config, int output_width) {
  if (!config || output_width <= 0 || config->cat_height <= 0)
    return (overlay_extent_t){0};
  int cat = clamp_wide((int64_t)config->cat_height * CAT_IMAGE_WIDTH /
                           CAT_IMAGE_HEIGHT,
                       0, INT_MAX);
  // Post: pole at 150, maximum board, offset and damage/rounding outset.
  // Fan: pivot at 108, five 22-degree slots, orbit and 220px nameplate
  // half-width. 326px about the cat center includes the damage outset.
  int design = config->sign_style == SIGN_STYLE_POST
                   ? 2 * (150 - 99 + POST_BOARD_MAX + 17 + 2)
                   : 652;
  int64_t content = config->sign_style == SIGN_STYLE_OFF
                        ? cat
                        : ((int64_t)config->cat_height * design + 109) / 110;
  // The translucent bar has always covered the output. Keep its pixels.
  int width = config->overlay_opacity > 0
                  ? output_width
                  : clamp_wide(content, 1, output_width);
  return (overlay_extent_t){width, (width - cat) / 2};
}
int overlay_scaled_width(int width, int output_width, uint32_t scale) {
  if (width <= 0 || output_width <= 0)
    return 0;
  width = clamp_wide(width, 1, output_width);
  int step = pixel_step(scale);
  // Match the output's grid remainder so an aligned origin can still cover
  // its last column, even when the output width is not a multiple of step.
  return width + (output_width - width) % step;
}
overlay_placement_t overlay_place(int output_x, int cat_width, int width,
                                  int output_width, uint32_t scale) {
  int max_margin = output_width > width ? output_width - width : 0;
  int64_t desired = (int64_t)output_x - (width - cat_width) / 2;
  int margin = clamp_wide(desired, 0, max_margin);
  margin -= margin % pixel_step(scale);
  return (overlay_placement_t){
      margin, clamp_wide((int64_t)output_x - margin, INT_MIN, INT_MAX)};
}
drag_rect_t overlay_card_rect(drag_rect_t card, int margin_x, int margin_y,
                              bool top, int output_height, int surface_height) {
  int64_t sy =
      top ? margin_y : (int64_t)output_height - margin_y - surface_height;
  card.x = clamp_wide((int64_t)card.x + margin_x, INT_MIN, INT_MAX);
  card.y = clamp_wide((int64_t)card.y + sy, INT_MIN, INT_MAX);
  return card;
}

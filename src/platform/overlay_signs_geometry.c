#include "overlay_signs_internal.h"
#include "platform/drag.h"
#include "platform/overlay_signs.h"

#include <limits.h>

static int desk_hang(int cat_height) {
  if (cat_height <= 0)
    return 0;
  return (cat_height * 4 + 109) / 110;
}

int overlay_signs_height(const config_t *config) {
  if (!config || config->overlay_height <= 0)
    return 0;
  if (config->sign_style == SIGN_STYLE_OFF)
    return config->overlay_height;
  int extra = sign_clearance(config->sign_style, config->cat_height);
  int spare = config->overlay_height > config->cat_height
                  ? config->overlay_height - config->cat_height
                  : 0;
  int hang = desk_hang(config->cat_height);
  if (hang > spare)
    extra += hang - spare;
  if (extra < 0 || config->overlay_height > INT_MAX - extra)
    return INT_MAX;
  return config->overlay_height + extra;
}
int overlay_signs_resting_y(const config_t *config, int surface_height) {
  if (!config || surface_height <= 0)
    return 0;
  if (config->sign_style == SIGN_STYLE_OFF) {
    return drag_cat_rect(0, config, 0, config->cat_height, surface_height).y;
  }
  int cat = config->cat_height > 0 ? config->cat_height : 0;
  int limit = surface_height > cat ? surface_height - cat : 0;
  int64_t y = (int64_t)surface_height - cat - desk_hang(cat);
  // A positive offset would push the pole base below the surface.
  if (config->cat_y_offset < 0)
    y += config->cat_y_offset;
  if (y < 0)
    return 0;
  if (y > limit)
    return limit;
  return (int)y;
}
int overlay_signs_cat_y(const config_t *config, int surface_height) {
  int y = overlay_signs_resting_y(config, surface_height) - (int)published_lift;
  return y > 0 ? y : 0;
}
int overlay_signs_cat_y_at(size_t index, const config_t *config,
                           int surface_height) {
  double lift = index < MAX_OUTPUTS && lanes[index].has_frame
                    ? lanes[index].frame.cat_lift
                    : 0;
  int y = lane_resting_y(index, config, surface_height) - (int)lift;
  return y > 0 ? y : 0;
}
int lane_resting_y(size_t index, const config_t *config, int surface_height) {
  return index < MAX_OUTPUTS && lanes[index].placed
             ? lanes[index].resting_y
             : overlay_signs_resting_y(config, surface_height);
}
void overlay_signs_place(size_t index, sign_orientation_t orientation,
                         int resting_y) {
  if (index >= MAX_OUTPUTS)
    return;
  lane_t *lane = &lanes[index];
  if (lane->orientation != orientation) {
    sign_menu_t menu = lane->model.menu;
    sign_scalar_t lift = lane->model.desk_lift;
    sign_scalar_t fade = lane->model.desk_fade;
    lane->model = (signs_t){.menu = menu, .desk_lift = lift, .desk_fade = fade};
    lane->presented = false;
  }
  lane->orientation = orientation;
  lane->resting_y = resting_y;
  lane->placed = true;
}

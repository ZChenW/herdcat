#include "platform/surface_tiers.h"

#include "config/config.h"
#include "config/sign_options.h"
#include "graphics/signs.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_signs.h"
#include "platform/overlay_vertical.h"

#include <limits.h>
#include <stdint.h>

static bool enabled(const config_t *config) {
  return config != NULL && config->sign_style != SIGN_STYLE_OFF &&
         config->overlay_opacity == 0;
}
static int tail_padding(const config_t *config) {
  return enabled(config) ? (int)(((int64_t)config->cat_height * 8 + 109) / 110)
                         : 0;
}
int surface_tier_model_height(const config_t *config,
                              sign_orientation_t orientation, int height) {
  return orientation == SIGN_BELOW ? height - tail_padding(config) : height;
}
int surface_tier_capacity(const config_t *config, int count, bool expanded) {
  if (!config)
    return 0;
  int maximum = config->sign_max;
  if (!enabled(config))
    return maximum;
  if (count > maximum)
    count = maximum;
  if (count <= 0 && !expanded)
    return 0;
  if (count > 5) {
    return maximum;
  }
  return maximum < 5 ? maximum : 5;
}
surface_size_t surface_tier_size(const config_t *config, int capacity,
                                 int output_width, uint32_t scale) {
  if (!config)
    return (surface_size_t){0};
  config_t local = *config;
  if (enabled(config))
    local.sign_max = capacity;
  int height = overlay_signs_height(&local);
  int width = overlay_extent(&local, output_width).width;
  if (enabled(config) && capacity == 0) {
    // Closed hover has no pad. The quiet pole and the typing desk fit inside
    // the cat's width. Keep 8px of lift plus the existing desk hang reserve.
    int clearance = sign_clearance(local.sign_style, local.cat_height, 0);
    int lift = (int)(((int64_t)local.cat_height * 8 + 109) / 110);
    height = height - clearance + lift;
    int minimum = local.cat_height + lift;
    if (height < minimum)
      height = minimum;
    local.sign_style = SIGN_STYLE_OFF;
    width = overlay_extent(&local, output_width).width;
  }
  // The existing cat placement already reserves the 8px desk lift. Only
  // returning desk ink and fractional pixel phase need only a lower tail.
  int padding = tail_padding(config);
  height = height > INT_MAX - padding ? INT_MAX : height + padding;
  return (surface_size_t){overlay_scaled_width(width, output_width, scale),
                          height};
}
int surface_tier_update(surface_tiers_t *state, int desired, bool blocked,
                        bool transitioning, int64_t now_ms) {
  if (state->pending)
    return -1;
  if (desired >= state->capacity || blocked || transitioning) {
    state->shrink_at = 0;
    if (desired <= state->capacity)
      return -1;
  } else {
    if (!state->shrink_at || state->shrink_capacity != desired) {
      state->shrink_capacity = desired;
      state->shrink_at = now_ms > INT64_MAX - SURFACE_TIER_SHRINK_MS
                             ? INT64_MAX
                             : now_ms + SURFACE_TIER_SHRINK_MS;
    }
    if (now_ms < state->shrink_at)
      return -1;
  }
  state->shrink_at = 0;
  state->requested = desired;
  state->pending = true;
  return desired;
}
void surface_tier_ready(surface_tiers_t *state) {
  if (state->pending) {
    state->capacity = state->requested;
    state->pending = false;
  }
}
overlay_vertical_t surface_tier_vertical(const config_t *config, int position_y,
                                         int output_height, int surface_height,
                                         bool has_history,
                                         sign_orientation_t previous,
                                         uint32_t scale, int capacity) {
  if (!config)
    return (overlay_vertical_t){.orientation = SIGN_ABOVE};
  int full_height = overlay_signs_height(config);
  config_t placement = *config;
  if (enabled(config))
    placement.sign_max = capacity > 5           ? config->sign_max
                         : config->sign_max < 5 ? config->sign_max
                                                : 5;
  overlay_vertical_t reference = overlay_place_vertical(
      &placement, position_y, output_height, full_height,
      overlay_signs_resting_y(config, full_height), has_history, previous);
  if (!enabled(config) || surface_height == full_height)
    return reference;
  // Saved displacement and pixel phase retain the configured maximum.
  int above_y =
      overlay_signs_resting_y(config, surface_height) - tail_padding(config);
  int lift = (int)(((int64_t)config->cat_height * 8 + 109) / 110);
  int local = reference.orientation == SIGN_BELOW ? lift : above_y;
  int limit =
      output_height > surface_height ? output_height - surface_height : 0;
  int origin = reference.cat_y_in_output - local;
  if (origin < 0)
    origin = 0;
  if (origin > limit)
    origin = limit;
  bool top = config->overlay_position == POSITION_TOP;
  overlay_vertical_t phase = overlay_place_vertical(
      config, position_y, output_height, full_height,
      overlay_signs_resting_y(config, full_height), has_history, previous);
  int old_origin =
      top ? phase.margin_y : output_height - full_height - phase.margin_y;
  // Translate by whole physical pixels, retaining the pre-tier pixel phase.
  uint32_t a = 120;
  uint32_t b = scale;
  while (b) {
    uint32_t remainder = a % b;
    a = b;
    b = remainder;
  }
  int step = (int)(120 / a);
  int remainder = (origin - old_origin) % step;
  if (remainder < 0)
    remainder += step;
  origin -= remainder;
  if (origin < 0)
    origin += step;
  reference.margin_y = top ? origin : limit - origin;
  reference.cat_y_in_surface = reference.cat_y_in_output - origin;
  return reference;
}

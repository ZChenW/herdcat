#include "config/config.h"
#include "core/herdcat.h"
#include "overlay_internal.h"
#include "platform/drag.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_signs.h"
#include "platform/overlay_vertical.h"
#include "platform/surface_tiers.h"
#include "zwlr-layer-shell-v1-client-protocol.h"

#include <stddef.h>

void clamp_position(overlay_t *overlay) {
  if (!overlay->has_position) {
    overlay->output_x = drag_default_x(
        &overlay->config, overlay->config.screen_width, cat_width(overlay));
    overlay->position_y = 0;
  } else {
    int ignored = 0;
    drag_clamp(&overlay->output_x, &ignored, overlay->config.screen_width,
               cat_width(overlay), 0, 0);
  }
  overlay_vertical_t vertical = surface_tier_vertical(
      &overlay->config, overlay->position_y, overlay->output_height,
      overlay->height, overlay->has_orientation, overlay->orientation,
      overlay->scale, overlay->tiers.capacity);
  overlay->position_y = vertical.position_y;
  overlay->margin_y = vertical.margin_y;
  overlay->cat_y = vertical.cat_y_in_surface;
  overlay->orientation = vertical.orientation;
  overlay->has_orientation = true;
  overlay_signs_output_geometry((size_t)(overlay - overlays),
                                vertical.cat_y_in_output,
                                overlay->output_height);
  overlay_signs_place((size_t)(overlay - overlays), overlay->orientation,
                      overlay->cat_y);
  overlay_placement_t place =
      overlay_place(overlay->output_x, cat_width(overlay), overlay->width,
                    overlay->config.screen_width, overlay->scale);
  overlay->cat_x = place.cat_x_in_surface;
  overlay->margin_x = place.margin_x;
}
void set_margin(overlay_t *overlay) {
  bool top = overlay->config.overlay_position == POSITION_TOP;
  zwlr_layer_surface_v1_set_margin(overlay->layer, top ? overlay->margin_y : 0,
                                   0, top ? 0 : overlay->margin_y,
                                   overlay->margin_x);
}

int wayland_reset_position(void) {
  finish_drag();
  if (drag_position_reset(NULL) < 0) {
    return 1;
  }
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    overlay_t *overlay = &overlays[i];
    if (overlay->surface) {
      overlay->has_position = false;
      clamp_position(overlay);
      set_margin(overlay);
      overlay->redraw = true;
    }
  }
  return 0;
}

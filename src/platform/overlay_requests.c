#include "config/config.h"
#include "core/herdcat.h"
#include "overlay_internal.h"
#include "platform/scale.h"
#include "platform/wayland.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

int wayland_phys_dim(int logical) {
  return scale_size_120(logical, active ? active->scale : 120);
}
void wayland_request_redraw(void) {
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    overlays[i].redraw = true;
    overlays[i].damage_all = true;
  }
}
void wayland_request_current_redraw(void) {
  if (active) {
    active->redraw = true;
    active->damage_all = true;
  }
}
void wayland_set_hidden(bool value) {
  hidden = value;
  wayland_request_redraw();
}

int cat_width(const overlay_t *overlay) {
  return (int)((int64_t)overlay->config.cat_height * CAT_IMAGE_WIDTH /
               CAT_IMAGE_HEIGHT);
}
// Called with this overlay active; input and pixels share one commit.
bool overlay_hidden(const overlay_t *overlay) {
  return hidden || (overlay->config.layer != LAYER_OVERLAY &&
                    !overlay->config.disable_fullscreen_hide &&
                    atomic_load(&fullscreen_detected));
}

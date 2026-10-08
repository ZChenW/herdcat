#include "../src/platform/overlay_internal.h"
#include "core/herdcat.h"
#include "platform/wayland.h"
#include "test_helpers.h"

#include <stdatomic.h>

overlay_t overlays[MAX_OUTPUTS];
overlay_t *active;
bool hidden;
atomic_bool fullscreen_detected;

int main(void) {
  active = &overlays[1];
  wayland_request_current_redraw();
  TEST_ASSERT(overlays[1].redraw && overlays[1].damage_all);
  TEST_ASSERT(!overlays[0].redraw && !overlays[0].damage_all);
  wayland_request_redraw();
  for (size_t i = 0; i < MAX_OUTPUTS; i++)
    TEST_ASSERT(overlays[i].redraw && overlays[i].damage_all);
  return 0;
}

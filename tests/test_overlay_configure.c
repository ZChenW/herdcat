// Exercise the real listener without a compositor socket. Only protocol
// acknowledgement is faked; geometry and readiness use production code.
#include "../src/platform/wayland.c"
#include "test_helpers.h"

static unsigned acknowledgements;
uint32_t __wrap_wl_proxy_get_version(struct wl_proxy *proxy) {
  (void)proxy;
  return 4;
}
struct wl_proxy *
__wrap_wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                              const struct wl_interface *interface,
                              uint32_t version, uint32_t flags, ...) {
  (void)proxy;
  (void)interface;
  (void)version;
  (void)flags;
  TEST_ASSERT(opcode == ZWLR_LAYER_SURFACE_V1_ACK_CONFIGURE);
  acknowledgements++;
  return NULL;
}

int main(void) {
  for (int smaller = 0; smaller < 4; smaller++) {
    overlay_t *overlay = &overlays[0];
    *overlay = (overlay_t){
        .config = {.cat_height = 110,
                   .overlay_height = 120,
                   .sign_style = SIGN_STYLE_FAN,
                   .sign_max = 10,
                   .screen_width = 800},
        .scale = 120,
        .output_height = 200,
        .width = 198,
        .height = 144,
        .requested_width = 652,
        .requested_height = 409,
        .await_configure = true,
        .tiers = {.pending = true, .requested = 10}
    };
    int w = smaller < 2 ? 600 : 652;
    int h = smaller == 1 ? 409 : 180;
    if (smaller == 3)
      w = h = 0;  // Zero delegates that dimension to the client's request.
    configure(overlay, NULL, 42, (uint32_t)w, (uint32_t)h);
    if (smaller == 3) {
      w = 652;
      h = 409;
    }
    TEST_ASSERT(acknowledgements == (unsigned)smaller + 1);
    TEST_ASSERT(!overlay->await_configure && overlay->configured);
    TEST_ASSERT(overlay->width == w && overlay->height == h);
    TEST_ASSERT(overlay->resize && overlay->redraw);
    // Configuration releases the event-loop gate; allocation still precedes
    // capacity promotion, even when the compositor clamps the dimensions.
    TEST_ASSERT(overlay->tiers.pending && overlay->tiers.capacity == 0);
    surface_tier_ready(&overlay->tiers);
    TEST_ASSERT(!overlay->tiers.pending && overlay->tiers.capacity == 10);
    TEST_ASSERT(surface_tier_update(&overlay->tiers, 10, false, false, 100) ==
                -1);
  }
  puts("Clamped configure accepts actual dimensions and releases readiness.");
  return 0;
}

#include "core/herdcat.h"
#include "platform/overlay_geometry.h"
#include "test_helpers.h"

#include <limits.h>

int main(void) {
  TEST_ASSERT(overlay_extent(NULL, 2560).width == 0);
  config_t config = {.cat_height = 110, .sign_style = SIGN_STYLE_POST};
  overlay_extent_t extent = overlay_extent(&config, 2560);
  TEST_ASSERT(extent.width == 652 && extent.cat_x_in_surface == 227);
  TEST_ASSERT((int64_t)extent.width * 300 * 4 * 2 < 1572864);
  const int heights[] = {10, 40, 110, 200, INT_MAX};
  for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
    config.cat_height = heights[i];
    if (heights[i] == INT_MAX) {
      TEST_ASSERT(overlay_extent(&config, 2560).width == 2560);
      continue;
    }
    for (int style = SIGN_STYLE_OFF; style <= SIGN_STYLE_FAN; style++) {
      config.sign_style = (sign_style_t)style;
      for (int out = 1; out <= 2560; out += 31) {
        extent = overlay_extent(&config, out);
        TEST_ASSERT(extent.width > 0 && extent.width <= out);
        int cat = heights[i] * CAT_IMAGE_WIDTH / CAT_IMAGE_HEIGHT;
        const uint32_t scales[] = {120, 150, 180, 210, 240};
        for (size_t j = 0; j < sizeof(scales) / sizeof(scales[0]); j++) {
          int width = overlay_scaled_width(extent.width, out, scales[j]);
          if (cat <= out) {
            overlay_placement_t edge =
                overlay_place(out - cat, cat, width, out, scales[j]);
            TEST_ASSERT(edge.margin_x + width == out);
            TEST_ASSERT(edge.cat_x_in_surface + cat == width);
          }
          for (int x = 0; x <= out; x += 7) {
            overlay_placement_t p =
                overlay_place(x, cat, width, out, scales[j]);
            TEST_ASSERT(p.margin_x >= 0 && p.margin_x + width <= out);
            TEST_ASSERT(p.margin_x + p.cat_x_in_surface == x);
            TEST_ASSERT((int64_t)p.margin_x * scales[j] % 120 == 0);
          }
        }
      }
    }
  }
  config.cat_height = 110;
  config.sign_style = SIGN_STYLE_FAN;
  extent = overlay_extent(&config, 2560);
  overlay_placement_t left = overlay_place(0, 198, extent.width, 2560, 120);
  overlay_placement_t right = overlay_place(2362, 198, extent.width, 2560, 120);
  TEST_ASSERT(left.margin_x == 0 && left.cat_x_in_surface == 0);
  TEST_ASSERT(right.margin_x == 1908 && right.cat_x_in_surface == 454);
  config.overlay_opacity = 1;
  TEST_ASSERT(overlay_extent(&config, 2560).width == 2560);
  config.overlay_opacity = 0;
  config.sign_style = SIGN_STYLE_OFF;
  TEST_ASSERT(overlay_extent(&config, 2560).width == 198);
  TEST_ASSERT(overlay_extent(&config, 100).width == 100);
  TEST_ASSERT(overlay_extent(&config, 0).width == 0);
  config.sign_style = SIGN_STYLE_POST;
  config.cat_x_offset = 20;
  for (int align = ALIGN_LEFT; align <= ALIGN_RIGHT; align++) {
    config.cat_align = (align_type_t)align;
    int x = drag_default_x(&config, 2560, 198);
    overlay_placement_t p = overlay_place(x, 198, 652, 2560, 150);
    TEST_ASSERT(p.margin_x + p.cat_x_in_surface == x);
  }
  drag_rect_t card = {22, 12, 154, 168};
  drag_rect_t placed = overlay_card_rect(card, 400, 50, true, 1080, 300);
  TEST_ASSERT(placed.x == 422 && placed.y == 62 && placed.width == 154);
  placed = overlay_card_rect(card, 400, 50, false, 1080, 300);
  TEST_ASSERT(placed.x == 422 && placed.y == 742);
  return 0;
}

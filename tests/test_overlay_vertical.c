#define _GNU_SOURCE
#include "graphics/font_panel.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_vertical.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

static config_t config = {.cat_height = 110,
                          .overlay_height = 110,
                          .sign_style = SIGN_STYLE_FAN,
                          .overlay_position = POSITION_BOTTOM};

static void thresholds(void) {
  TEST_ASSERT(overlay_orientation(&config, 179, 1080, 294, false, SIGN_ABOVE) ==
              SIGN_BELOW);
  TEST_ASSERT(overlay_orientation(&config, 180, 1080, 294, false, SIGN_BELOW) ==
              SIGN_ABOVE);
  TEST_ASSERT(overlay_orientation(&config, 203, 1080, 294, true, SIGN_BELOW) ==
              SIGN_BELOW);
  TEST_ASSERT(overlay_orientation(&config, 204, 1080, 294, true, SIGN_BELOW) ==
              SIGN_ABOVE);
  TEST_ASSERT(overlay_orientation(&config, 180, 1080, 294, true, SIGN_ABOVE) ==
              SIGN_ABOVE);
  TEST_ASSERT(overlay_orientation(&config, 0, 200, 294, false, SIGN_BELOW) ==
              SIGN_ABOVE);
  TEST_ASSERT(overlay_orientation(&config, 170, 400, 294, true, SIGN_BELOW) ==
              SIGN_ABOVE);
  config.overlay_opacity = 1;
  TEST_ASSERT(overlay_orientation(&config, 0, 1080, 294, false, SIGN_BELOW) ==
              SIGN_ABOVE);
  config.overlay_opacity = 0;
  config.sign_style = SIGN_STYLE_OFF;
  TEST_ASSERT(overlay_orientation(&config, 0, 1080, 110, false, SIGN_BELOW) ==
              SIGN_ABOVE);
  config.sign_style = SIGN_STYLE_FAN;
  TEST_ASSERT(overlay_orientation(NULL, 0, 1080, 294, false, SIGN_BELOW) ==
              SIGN_ABOVE);
}
static void placement_and_drag(void) {
  char root[] = "/tmp/herdcat-below-XXXXXX";
  TEST_ASSERT(mkdtemp(root));
  TEST_ASSERT(setenv("XDG_STATE_HOME", root, 1) == 0);
  for (int top = 0; top < 2; top++) {
    config.overlay_position = top ? POSITION_TOP : POSITION_BOTTOM;
    int base = 1080 - 294 + 180;
    int press_y = top ? 250 : base - 250;
    sign_orientation_t previous = SIGN_ABOVE;
    int last = 250;
    for (int travel = 0; travel <= 300; travel++) {
      int x, y;
      drag_follow_position(400, press_y, 10, 20, 10 + travel, 20 - travel,
                           top != 0, 1920, 198, 1080, top ? 110 : 294 - 180, &x,
                           &y);
      overlay_vertical_t p =
          overlay_place_vertical(&config, y, 1080, 294, 180, true, previous);
      TEST_ASSERT(p.cat_y_in_output == (travel < 250 ? 250 - travel : 0));
      TEST_ASSERT(abs(p.cat_y_in_output - last) <= 1);
      TEST_ASSERT(p.margin_y >= 0 && p.margin_y + 294 <= 1080);
      int origin = top ? p.margin_y : 1080 - 294 - p.margin_y;
      TEST_ASSERT(origin + p.cat_y_in_surface == p.cat_y_in_output);
      TEST_ASSERT(p.orientation == (travel > 70 ? SIGN_BELOW : SIGN_ABOVE));
      last = p.cat_y_in_output;
      previous = p.orientation;
      // Repeating the same motion uses the press position, not a delta sum.
      int again_x, again_y;
      drag_follow_position(400, press_y, 10, 20, 10 + travel, 20 - travel,
                           top != 0, 1920, 198, 1080, top ? 110 : 114, &again_x,
                           &again_y);
      TEST_ASSERT(x == again_x && y == again_y);
    }
    int saved_y = top ? 0 : base;
    TEST_ASSERT(drag_position_save("TEST-1", 400, saved_y) == 0);
    int x, y;
    TEST_ASSERT(drag_position_load("TEST-1", &x, &y) == 0);
    overlay_vertical_t restart =
        overlay_place_vertical(&config, y, 1080, 294, 180, false, SIGN_ABOVE);
    TEST_ASSERT(x == 400 && restart.cat_y_in_output == 0 &&
                restart.orientation == SIGN_BELOW);
    for (int target = 0; target <= 250; target++) {
      overlay_vertical_t p =
          overlay_place_vertical(&config, top ? target : base - target, 1080,
                                 294, 180, true, previous);
      TEST_ASSERT(p.cat_y_in_output == target);
      TEST_ASSERT(p.orientation == (target < 204 ? SIGN_BELOW : SIGN_ABOVE));
      previous = p.orientation;
    }
    // Old bottom records still put above cats in exactly the same place.
    if (!top) {
      overlay_vertical_t p = overlay_place_vertical(&config, 40, 1080, 294, 180,
                                                    false, SIGN_ABOVE);
      TEST_ASSERT(p.margin_y == 40 && p.cat_y_in_surface == 180);
    }
  }
  TEST_ASSERT(drag_position_reset(NULL) == 0);
  char path[256];
  snprintf(path, sizeof(path), "%s/herdcat", root);
  TEST_ASSERT(rmdir(path) == 0 && rmdir(root) == 0);
  TEST_ASSERT(unsetenv("XDG_STATE_HOME") == 0);
  config.overlay_position = POSITION_TOP;
  const int heights[] = {40, 60, 110, 200};
  for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
    config.cat_height = heights[h];
    int clearance = sign_clearance(config.sign_style, config.cat_height);
    int surface = config.cat_height + clearance + 4;
    for (int y = 0; y <= 1080 - config.cat_height; y++) {
      overlay_vertical_t p = overlay_place_vertical(
          &config, y, 1080, surface, clearance, false, SIGN_ABOVE);
      TEST_ASSERT(p.cat_y_in_output == y);
      TEST_ASSERT(p.cat_y_in_surface >= 0);
      TEST_ASSERT(p.cat_y_in_surface + config.cat_height <= surface);
      TEST_ASSERT(p.margin_y >= 0 && p.margin_y + surface <= 1080);
    }
  }
}
static void panel_anchor(void) {
  // Reflected card beside a cat at the top, for both layer anchors. Its
  // output position is independent of the narrow surface width.
  for (int top = 0; top < 2; top++) {
    drag_rect_t card = {249, 126, 154, 168};
    drag_rect_t absolute =
        overlay_card_rect(card, 400, top ? 0 : 786, top != 0, 1080, 294);
    TEST_ASSERT(absolute.x == 649 && absolute.y == 126);
    font_panel_box_t box = {absolute.x, absolute.y, absolute.width,
                            absolute.height};
    font_panel_size_t size = {380, 300}, screen = {1920, 1080};
    double x, y;
    font_panel_place(&box, &size, &screen, 8, &x, &y);
    TEST_ASSERT(x >= 0 && x + size.w <= screen.w);
    TEST_ASSERT(y >= 0 && y + size.h <= screen.h);
    TEST_ASSERT(y == 0);
  }
}
int main(void) {
  thresholds();
  placement_and_drag();
  panel_anchor();
  return 0;
}

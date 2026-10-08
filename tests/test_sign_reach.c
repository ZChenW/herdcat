#include "graphics/signs.h"
#include "platform/overlay_vertical.h"
#include "test_helpers.h"

static void separate_card(void) {
  config_t config = {
      .cat_height = 110, .sign_style = SIGN_STYLE_FAN, .sign_max = 5};
  TEST_ASSERT(overlay_orientation(&config, 120, 1080, 308, false, SIGN_ABOVE) ==
              SIGN_ABOVE);
  TEST_ASSERT(overlay_card_orientation(&config, 120, 1080, SIGN_ABOVE) ==
              SIGN_BELOW);
  TEST_ASSERT(overlay_card_orientation(&config, 120, 350, SIGN_ABOVE) ==
              SIGN_ABOVE);  // neither side fits: preserve existing behavior
  TEST_ASSERT(overlay_card_orientation(&config, 200, 1080, SIGN_ABOVE) ==
              SIGN_ABOVE);
  signs_t model = {0};
  sign_input_t input = {.style = SIGN_STYLE_FAN,
                        .animations = SIGN_ANIM_OFF,
                        .cat_y = 120,
                        .cat_height = 110,
                        .menu = true,
                        .menu_below = true};
  sign_frame_t frame;
  signs_frame(&model, &input, &frame);
  TEST_ASSERT(frame.menu_card.y == 236 && frame.menu_card.h == 168);
  TEST_ASSERT(frame.menu_font.y > 236 &&
              frame.menu_font.y + frame.menu_font.h < 404);
  TEST_ASSERT(frame.menu_style[0].y > frame.menu_font.y);
  TEST_ASSERT(frame.cat_lift == 0);
  config.sign_font_size = 20;
  TEST_ASSERT(overlay_orientation(&config, 120, 1080, 330, false, SIGN_ABOVE) ==
              SIGN_BELOW);
}
int main(void) {
  const int counts[] = {0, 1, 5, 6, 10};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int maximum = 5; maximum <= 10; maximum += 5)
      for (unsigned i = 0; i < sizeof(counts) / sizeof(*counts); i++) {
        int capacity = counts[i] > 5 && maximum > 5 ? 10 : 5;
        int expected = style == SIGN_STYLE_FAN ? (capacity == 5 ? 114 : 190)
                                               : (capacity == 5 ? 171 : 329);
        TEST_ASSERT(sign_reach((sign_style_t)style, 110, capacity) == expected);
      }
  separate_card();
  return 0;
}

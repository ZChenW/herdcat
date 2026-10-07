#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static sign_frame_t scene(sign_style_t style, sign_theme_t theme,
                          sign_orientation_t orientation, double height,
                          int offset, double surface_height) {
  agent_session_view_t session = {.key = 1,
                                  .state = AGENT_STATE_WAITING,
                                  .agent = "claude",
                                  .name = "repo"};
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = style,
                     .theme = theme,
                     .orientation = orientation,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .open = true,
                     .english = true,
                     .cat_x = 250,
                     .cat_y = 300,
                     .cat_height = height,
                     .desk_offset = offset,
                     .surface_height = surface_height,
                     .typing = true,
                     .typing_key = 2,
                     .desk_snap = true,
                     .now_ms = 1000};
  strcpy(in.desk_name, "repo");
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  return frame;
}
static uint64_t pixels(const sign_frame_t *frame, int scale) {
  size_t size = 960 * 1200 * 4;
  uint8_t *data = calloc(size, 1);
  TEST_ASSERT(data);
  sign_draw(data, 960, 1200, scale, frame, SIGN_DRAW_UNDER);
  sign_draw(data, 960, 1200, scale, frame, SIGN_DRAW_OVER);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; i++)
    hash = (hash ^ data[i]) * UINT64_C(1099511628211);
  free(data);
  return hash;
}
static void near(double a, double b) {
  TEST_ASSERT(fabs(a - b) < 1e-8);
}
static const sign_shape_t *desk(const sign_frame_t *frame) {
  TEST_ASSERT(frame->shape_count > 0);
  return &frame->shapes[frame->shape_count - 1];
}
static void displacement(void) {
  const double heights[] = {40, 110, 137, 200};
  const int offsets[] = {-6, 12, 24};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW; orientation++)
      for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); h++) {
        double scale = heights[h] / 110;
        sign_frame_t base =
            scene((sign_style_t)style, SIGN_THEME_LIGHT,
                  (sign_orientation_t)orientation, heights[h], 0, 0);
        for (size_t o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++) {
          int offset = offsets[o];
          sign_frame_t moved =
              scene((sign_style_t)style, SIGN_THEME_LIGHT,
                    (sign_orientation_t)orientation, heights[h], offset, 0);
          double delta = offset * scale;
          if (orientation == SIGN_BELOW)
            delta = -delta;
          near(desk(&moved)->y - desk(&base)->y, delta);
          near(desk(&moved)->x, desk(&base)->x);
          near(desk(&moved)->w, desk(&base)->w);
          near(desk(&moved)->h, desk(&base)->h);
          const sign_text_t *a = &base.texts[base.text_count - 1];
          const sign_text_t *b = &moved.texts[moved.text_count - 1];
          near(b->line_top - a->line_top, delta);
          near(b->clip_y - a->clip_y, delta);
          near(moved.cat_lift, base.cat_lift);
          if (orientation == SIGN_BELOW) {
            near(moved.shapes[0].h - base.shapes[0].h,
                 (offset > 0 ? offset : 0) * scale);
            TEST_ASSERT(moved.hit_count == base.hit_count);
          }
          // Bound the canvas tightly enough to force edge tightening.
          double edge = desk(&base)->y + desk(&base)->h;
          sign_frame_t bounded =
              scene((sign_style_t)style, SIGN_THEME_LIGHT,
                    (sign_orientation_t)orientation, heights[h], offset, edge);
          TEST_ASSERT(desk(&bounded)->y >= 0);
          TEST_ASSERT(desk(&bounded)->y + desk(&bounded)->h + 2 <= edge);
        }
      }
}
int main(void) {
  // Frozen before desk offset changes: desk plus signs, both themes and
  // orientations, at integer and fractional output scales. Shapes only: no font
  // is loaded yet, because glyph rasterisation differs between FreeType builds.
  static const uint64_t expected[] = {
      UINT64_C(0xc6a8605aec6a05a5), UINT64_C(0xc966264d4b4e3563),
      UINT64_C(0x34ba8e401909ee1e), UINT64_C(0xa4beee1123b01185),
      UINT64_C(0x48bab7971b7ee508), UINT64_C(0xa80873a3fcd0499a),
      UINT64_C(0xb151ddce1251e995), UINT64_C(0xe0c347373dc4d08b),
      UINT64_C(0x31771629187de247), UINT64_C(0xcbfdb000f5051385),
      UINT64_C(0xe2225009a1ceef8d), UINT64_C(0x8b93b7292b937d73),
      UINT64_C(0x91ae1adb69381d99), UINT64_C(0x190723d9421b253f),
      UINT64_C(0x97911cd85d77eaa9), UINT64_C(0x32430a620f2336d1),
      UINT64_C(0x0476713473b4037b), UINT64_C(0xe43a58610841f211),
      UINT64_C(0xa3a34769837bdf09), UINT64_C(0x1b24fa004231a58f),
      UINT64_C(0x7e799340a025feb8), UINT64_C(0x817c8d0f1a540d39),
      UINT64_C(0x7b10b9db904ebf78), UINT64_C(0xa04d728df7f17d6c),
  };
  size_t at = 0;
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int theme = 0; theme < 2; theme++)
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        sign_frame_t frame = scene((sign_style_t)style, (sign_theme_t)theme,
                                   (sign_orientation_t)orientation, 110, 0, 0);
        for (int scale = 120; scale <= 180; scale += 30)
          TEST_ASSERT(pixels(&frame, scale) == expected[at++]);
      }
  TEST_ASSERT(text_init("sans") == 0);
  displacement();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

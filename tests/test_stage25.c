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
  // Frozen before stage 25: desk plus signs, both themes and orientations,
  // at integer and fractional output scales.
  static const uint64_t expected[] = {
      UINT64_C(0x21f3eddd9af3ab4f), UINT64_C(0xa44360daafcc4f97),
      UINT64_C(0x5b8c50d19fc3a909), UINT64_C(0x92f3f504a6415b35),
      UINT64_C(0xef99e98456cf66ef), UINT64_C(0x67debdcf1144d266),
      UINT64_C(0x1bc5f72588b2c3bd), UINT64_C(0x027db5f120150635),
      UINT64_C(0xb4b452ef1cbb1a02), UINT64_C(0xb0c826698c294a0d),
      UINT64_C(0x4bec3b2283ff93af), UINT64_C(0x2e582944a9a41766),
      UINT64_C(0xa88862b28a83646b), UINT64_C(0xf8c59f6699a83f7d),
      UINT64_C(0x0d69c4458155cf7e), UINT64_C(0xbfc5d66bb1391f43),
      UINT64_C(0x9458bfe6374f101d), UINT64_C(0xe61c7944cd9dc49f),
      UINT64_C(0x4cc91344fdbdc1d0), UINT64_C(0xd436727f58160c7d),
      UINT64_C(0x010c9d5e1fc9ab71), UINT64_C(0xdf5d6a7a537bcfe8),
      UINT64_C(0x458a9a7b69ad1222), UINT64_C(0x8d5f3af9e37f2f9d),
  };
  size_t at = 0;
  TEST_ASSERT(text_init("sans") == 0);
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int theme = 0; theme < 2; theme++)
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        sign_frame_t frame = scene((sign_style_t)style, (sign_theme_t)theme,
                                   (sign_orientation_t)orientation, 110, 0, 0);
        for (int scale = 120; scale <= 180; scale += 30)
          TEST_ASSERT(pixels(&frame, scale) == expected[at++]);
      }
  displacement();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

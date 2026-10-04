#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#  pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  pragma GCC diagnostic ignored "-Wstrict-prototypes"
#  pragma GCC diagnostic ignored "-Wold-style-definition"
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define W 700
#define H 420

static uint8_t *pixel(uint8_t *buf, int bw, int x, int y) {
  return buf + ((size_t)y * (size_t)bw + (size_t)x) * 4;
}
static void expect(uint8_t *buf, int bw, int x, int y, uint32_t argb) {
  uint8_t *p = pixel(buf, bw, x, y);
  int got_r = p[2], got_g = p[1], got_b = p[0], got_a = p[3];
  int want_r = (int)((argb >> 16) & 255);
  int want_g = (int)((argb >> 8) & 255);
  int want_b = (int)(argb & 255);
  int want_a = (int)(argb >> 24);
  if (abs(got_r - want_r) > 12 || abs(got_g - want_g) > 12 ||
      abs(got_b - want_b) > 12 || abs(got_a - want_a) > 2) {
    fprintf(stderr,
            "pixel %d,%d is #%02x%02x%02x alpha %d, wanted #%06x alpha %d\n", x,
            y, got_r, got_g, got_b, got_a, argb & 0xffffff, want_a);
    TEST_ASSERT(0);
  }
}
static void assert_inside(uint8_t *buf, int bw, int bh,
                          const sign_frame_t *frame, double scale) {
  int left = (int)floor(frame->bounds_x * scale);
  int top = (int)floor(frame->bounds_y * scale);
  int right = (int)ceil((frame->bounds_x + frame->bounds_w) * scale);
  int bottom = (int)ceil((frame->bounds_y + frame->bounds_h) * scale);
  bool any = false;
  for (int y = 0; y < bh; y++) {
    for (int x = 0; x < bw; x++) {
      if (!pixel(buf, bw, x, y)[3])
        continue;
      any = true;
      if (x < left || y < top || x >= right || y >= bottom) {
        fprintf(stderr, "ink at %d,%d outside bounds %d,%d %dx%d\n", x, y, left,
                top, right - left, bottom - top);
        TEST_ASSERT(0);
      }
    }
  }
  TEST_ASSERT(any);
}
static const sign_shape_t *board_of(const sign_frame_t *frame) {
  const sign_shape_t *found = NULL;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (shape->kind == SIGN_RECT && shape->stroke > 0 &&
        (!found || shape->w > found->w))
      found = shape;
  }
  TEST_ASSERT(found);
  return found;
}
static void settled(sign_frame_t *frame) {
  signs_t model = {0};
  agent_session_view_t session = {
      .key = 1, .order = 1, .state = AGENT_STATE_DONE, .pid = 42};
  strcpy(session.agent, "claude");
  strcpy(session.name, "dotfiles");
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = SIGN_STYLE_POST,
                     .animations = SIGN_ANIM_OFF,
                     .open = true,
                     .now_ms = 5000,
                     .cat_x = 100,
                     .cat_y = 170,
                     .cat_height = 110};
  signs_frame(&model, &in, frame);
  TEST_ASSERT(!frame->animating && !frame->transitioning);
  TEST_ASSERT(frame->text_count == 1 && !frame->texts[0].above);
}
static void test_colors_and_cache(void) {
  sign_frame_t frame;
  settled(&frame);
  static uint8_t a[W * H * 4], b[W * H * 4], c[W * H * 4];
  sign_draw(a, W, H, 120, &frame, SIGN_DRAW_UNDER);
  assert_inside(a, W, H, &frame, 1);
  const sign_shape_t *board = board_of(&frame);
  expect(a, W, (int)lround(board->x + 1), (int)lround(board->y + board->h / 2),
         0xff111827);
  expect(a, W, (int)lround(board->x + board->w - 5), (int)lround(board->y + 4),
         0xffc7f1d6);
  bool check = false;
  int cx = (int)lround(board->x + 17), cy = (int)lround(board->y + 13);
  for (int y = cy - 7; y <= cy + 7; y++) {
    for (int x = cx - 8; x <= cx + 8; x++) {
      uint8_t *p = pixel(a, W, x, y);
      if (p[3] > 200 && p[2] < 80 && p[1] > 70 && p[1] < 160 && p[0] < 100)
        check = true;
    }
  }
  TEST_ASSERT(check);
  const sign_text_t *text = &frame.texts[0];
  bool ink = false;
  int x0 = (int)floor(text->x), y1 = (int)ceil(text->baseline_y);
  for (int y = y1 - 12; y <= y1; y++) {
    for (int x = x0; x < x0 + 48; x++) {
      uint8_t *p = pixel(a, W, x, y);
      if (p[3] > 180 && p[2] < 60 && p[1] < 60 && p[0] < 70)
        ink = true;
    }
  }
  TEST_ASSERT(ink);
  sign_draw(b, W, H, 120, &frame, SIGN_DRAW_UNDER);
  TEST_ASSERT(!memcmp(a, b, sizeof(a)));
  memset(c, 0, sizeof(c));
  sign_draw(c, W, H, 120, &frame, SIGN_DRAW_OVER);
  for (size_t i = 0; i < sizeof(c); i++)
    TEST_ASSERT(c[i] == 0);
  sign_draw_cleanup();
  memset(b, 0, sizeof(b));
  sign_draw(b, W, H, 120, &frame, SIGN_DRAW_UNDER);
  TEST_ASSERT(!memcmp(a, b, sizeof(a)));
}
static void test_transition_and_scale(void) {
  signs_t model = {0};
  agent_session_view_t session = {
      .key = 7, .order = 2, .state = AGENT_STATE_DONE, .pid = 9};
  strcpy(session.agent, "codex");
  strcpy(session.name, "wayland-bongocat-session");
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = SIGN_STYLE_POST,
                     .animations = SIGN_ANIM_REDUCED,
                     .open = true,
                     .now_ms = 1000,
                     .cat_x = 40,
                     .cat_y = 200,
                     .cat_height = 110};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  in.now_ms = 1180;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.transitioning);
  static uint8_t mid[W * H * 4];
  sign_draw(mid, W, H, 120, &frame, SIGN_DRAW_UNDER);
  assert_inside(mid, W, H, &frame, 1);
  in.now_ms = 2000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.transitioning && frame.texts[0].reverse);
  int bw = 1100, bh = 800;
  uint8_t *scaled = calloc((size_t)bw * (size_t)bh, 4);
  TEST_ASSERT(scaled);
  sign_draw(scaled, bw, bh, 240, &frame, SIGN_DRAW_UNDER);
  assert_inside(scaled, bw, bh, &frame, 2);
  const sign_shape_t *board = board_of(&frame);
  expect(scaled, bw, (int)lround((board->x + board->w - 5) * 2),
         (int)lround((board->y + board->h / 2) * 2), 0xffc7f1d6);
  free(scaled);
  sign_draw_cleanup();
}
static void test_orbit(void) {
  sign_frame_t frame = {0};
  frame.shape_count = 1;
  frame.shapes[0] = (sign_shape_t){.kind = SIGN_RECT,
                                   .x = 40,
                                   .y = 20,
                                   .w = 30,
                                   .h = 10,
                                   .fill = 0xffff0000,
                                   .rotation = 90,
                                   .orbit = true,
                                   .origin_x = 55,
                                   .origin_y = 80};
  frame.bounds_w = 200;
  frame.bounds_h = 200;
  static uint8_t buf[W * H * 4];
  memset(buf, 0, sizeof(buf));
  sign_draw(buf, W, H, 120, &frame, SIGN_DRAW_UNDER);
  expect(buf, W, 110, 80, 0xffff0000);
  TEST_ASSERT(pixel(buf, W, 55, 25)[3] == 0);
}
int main(void) {
  TEST_ASSERT(text_init(NULL) == 0);
  test_colors_and_cache();
  test_transition_and_scale();
  test_orbit();
  sign_draw(NULL, 1, 1, 120, NULL, SIGN_DRAW_UNDER);
  sign_draw_cleanup();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

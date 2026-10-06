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
  int x0 = (int)floor(text->x);
  int y0 = (int)floor(text->clip_y);
  int y1 = (int)ceil(text->clip_y + text->clip_h);
  for (int y = y0; y < y1; y++) {
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
  strcpy(session.name, "herdcat-session");
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
static void test_agent_shapes(const char *snapshot) {
  static uint8_t buf[360 * 180 * 4];
  const char *agents[] = {"claude", "codex", "pi", "custom"};
  const char *labels[] = {"Claude", "Codex", "Pi", "Custom"};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    for (int i = 0; i < 4; i++) {
      signs_t model = {0};
      agent_session_view_t session = {.key = 1, .state = AGENT_STATE_WORKING};
      strcpy(session.agent, agents[i]);
      strcpy(session.name, "project");
      sign_input_t in = {.sessions = &session,
                         .count = 1,
                         .style = (sign_style_t)style,
                         .animations = SIGN_ANIM_OFF,
                         .open = true,
                         .english = true,
                         .cat_height = 110,
                         .cat_y = 200};
      sign_frame_t frame;
      signs_frame(&model, &in, &frame);
      bool found = false;
      for (int j = 0; j < frame.shape_count; j++) {
        sign_shape_t shape = frame.shapes[j];
        if (shape.w < 29 || shape.stroke != 2)
          continue;
        TEST_ASSERT(shape.kind == (i < 2 ? SIGN_RECT : SIGN_CUT));
        if (i >= 2)
          TEST_ASSERT(shape.radius == 9);
        // Native raster capture of the actual model's compact plates at 1x.
        shape.x = 24 + i * 85;
        shape.y = style == SIGN_STYLE_FAN ? 26 : 96;
        shape.w = style == SIGN_STYLE_POST ? 34 : shape.w;
        shape.orbit = false;
        shape.rotation = 0;
        sign_frame_t sample = {
            .shape_count = 1, .bounds_w = 360, .bounds_h = 180};
        sample.shapes[0] = shape;
        sign_draw(buf, 360, 180, 120, &sample, SIGN_DRAW_UNDER);
        if (i >= 2) {
          TEST_ASSERT(pixel(buf, 360, (int)shape.x + 3, (int)shape.y + 3)[3] ==
                      0);
          expect(buf, 360, (int)shape.x + 17, (int)shape.y + 1, 0xff111827);
          expect(buf, 360, (int)shape.x + 17, (int)shape.y + 13, 0xffd9ebff);
        }
        found = true;
        break;
      }
      TEST_ASSERT(found);
      in.has_hover = true;
      in.hover_key = 1;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.text_count &&
                  strstr(frame.texts[0].meta, labels[i]) != NULL);
    }
  }
  if (snapshot) {
    FILE *file = fopen(snapshot, "wb");
    TEST_ASSERT(file);
    fprintf(file, "P6\n360 180\n255\n");
    for (size_t i = 0; i < sizeof(buf); i += 4) {
      unsigned alpha = buf[i + 3];
      unsigned char rgb[3];
      for (int j = 0; j < 3; j++)
        rgb[j] = (unsigned char)(buf[i + 2 - j] + (238 * (255 - alpha)) / 255);
      TEST_ASSERT(fwrite(rgb, 1, 3, file) == 3);
    }
    TEST_ASSERT(fclose(file) == 0);
  }
}

static void plate_padding(const char *name, const char *meta, int *top,
                          int *bottom) {
  static uint8_t buf[W * H * 4];
  memset(buf, 0, sizeof(buf));
  sign_frame_t frame = {.bounds_w = W, .bounds_h = H, .text_count = 1};
  frame.texts[0] = (sign_text_t){.x = 320,
                                 .anchor_y = 180,
                                 .px = 13,
                                 .meta_px = 11.5,
                                 .gap = 8,
                                 .color = 0xff111827,
                                 .meta_color = 0xff111827,
                                 .tag_scale = 1,
                                 .font_ratio = 1,
                                 .back = 0xffffe4a3};
  snprintf(frame.texts[0].value, sizeof(frame.texts[0].value), "%s", name);
  snprintf(frame.texts[0].meta, sizeof(frame.texts[0].meta), "%s", meta);
  sign_draw(buf, W, H, 120, &frame, SIGN_DRAW_UNDER);
  int y0 = H, y1 = -1, x0 = W, x1 = -1;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint8_t *p = pixel(buf, W, x, y);
      if (p[3] > 200 && p[2] > 200 && p[1] > 180 && p[0] > 120) {
        if (y < y0)
          y0 = y;
        if (y > y1)
          y1 = y;
        if (x < x0)
          x0 = x;
        if (x > x1)
          x1 = x;
      }
    }
  }
  TEST_ASSERT(y1 > y0 && x1 > x0);
  // Stay off the rounded border. Ink in the middle is the label.
  int inset = 14;
  int ink0 = H, ink1 = -1;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0 + inset; x <= x1 - inset; x++) {
      uint8_t *p = pixel(buf, W, x, y);
      if (p[3] > 160 && p[2] < 80 && p[1] < 90 && p[0] < 90) {
        if (y < ink0)
          ink0 = y;
        if (y > ink1)
          ink1 = y;
      }
    }
  }
  TEST_ASSERT(ink1 >= ink0);
  *top = ink0 - y0;
  *bottom = y1 - ink1;
}
static void test_nameplate_padding(void) {
  const char *names[] = {"Wayland", "项目名称", "Wayland项目"};
  const char *metas[] = {"2 min", "等你批准", "2 分钟"};
  for (int i = 0; i < 3; i++) {
    if (i > 0 && !text_has_glyph(0x9879, true)) {
      puts("SKIP CJK nameplate padding: no system Chinese font");
      break;
    }
    int top = 0, bottom = 0;
    plate_padding(names[i], metas[i], &top, &bottom);
    if (abs(top - bottom) > 1) {
      fprintf(stderr, "nameplate %s padding top %d bottom %d\n", names[i], top,
              bottom);
      TEST_ASSERT(0);
    }
  }
}
int main(int argc, char **argv) {
  TEST_ASSERT(text_init(NULL) == 0);
  test_nameplate_padding();
  test_colors_and_cache();
  test_transition_and_scale();
  test_orbit();
  test_agent_shapes(argc == 2 ? argv[1] : NULL);
  sign_draw(NULL, 1, 1, 120, NULL, SIGN_DRAW_UNDER);
  sign_draw_cleanup();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

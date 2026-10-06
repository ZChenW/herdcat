#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 760
#define H 760
static void near(double a, double b) {
  TEST_ASSERT(fabs(a - b) < 1e-9);
}
static sign_frame_t scene(sign_style_t style, sign_orientation_t orientation,
                          sign_theme_t theme, int mode, int64_t now) {
  agent_session_view_t sessions[5] = {0};
  const agent_state_t states[] = {AGENT_STATE_WORKING, AGENT_STATE_WAITING,
                                  AGENT_STATE_DONE, AGENT_STATE_ERROR,
                                  AGENT_STATE_IDLE};
  for (int i = 0; i < 5; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].pid = 100 + i;
    sessions[i].state = states[i];
    sessions[i].state_since_ms = 0;
    sessions[i].unread = i == 2 || i == 3;
    snprintf(sessions[i].name, sizeof(sessions[i].name), "session-%d", i);
    snprintf(sessions[i].agent, sizeof(sessions[i].agent), "%s",
             i % 2 ? "codex" : "claude");
  }
  signs_t model = {0};
  sign_frame_t frame;
  sign_input_t input = {.sessions = sessions,
                        .count = 5,
                        .style = style,
                        .orientation = orientation,
                        .theme = theme,
                        .animations = SIGN_ANIM_OFF,
                        .idle = SIGN_IDLE_ALWAYS,
                        .open = true,
                        .has_hover = true,
                        .hover_key = 2,
                        .font_size = 13,
                        .english = true,
                        .cat_x = 260,
                        .cat_y = 321,
                        .cat_height = 110,
                        .menu = mode == 1,
                        .typing = mode == 2,
                        .typing_key = 1,
                        .desk_snap = true,
                        .typing_until = 999999,
                        .now_ms = now};
  strcpy(input.desk_name, "desk-session");
  signs_frame(&model, &input, &frame);
  return frame;
}
static void transform_geometry(void) {
  sign_frame_t a = {0};
  a.shape_count = 3;
  a.shapes[0] = (sign_shape_t){.x = 10,
                               .y = 20,
                               .w = 30,
                               .h = 40,
                               .rotation = 25,
                               .orbit = true,
                               .origin_x = 60,
                               .origin_y = 80,
                               .clipped = true,
                               .clip_y = 10,
                               .clip_h = 70};
  a.shapes[1] = (sign_shape_t){.x = 70,
                               .y = 90,
                               .w = 4,
                               .h = 9,
                               .upright = true,
                               .icon_center_y = 100,
                               .rotation = 30,
                               .orbit = true,
                               .origin_y = 120};
  a.shapes[2] = a.shapes[1];
  a.shapes[2].y = 106;
  a.shapes[2].h = 4;
  a.text_count = 2;
  a.texts[0] =
      (sign_text_t){.line_top = 40, .line_h = 16, .clip_y = 30, .clip_h = 40};
  strcpy(a.texts[0].value, "upright");
  a.texts[1] = (sign_text_t){
      .anchor_y = 60, .px = 12.5, .tag_scale = .96, .back = 0xffffffff};
  a.hit_count = 1;
  a.hits[0] = (sign_hit_t){10, 20, 30, 40, 123, 42};
  a.has_pad = true;
  a.pad = (sign_rect_t){10, 0, 100, 100};
  a.bounds_y = 10;
  a.bounds_h = 120;
  a.menu_open = true;
  a.menu_card = a.menu_font = a.menu_style[0] = a.pad;
  sign_frame_t b = a;
  signs_reflect(&b, 200);
  near(b.shapes[0].y, 340);
  near(b.shapes[0].rotation, -25);
  near(b.shapes[0].origin_y, 320);
  near(b.shapes[0].clip_y, 320);
  near(b.shapes[1].y, 290);
  near(b.shapes[1].origin_y, 320);
  near(b.shapes[1].rotation, 30);
  near(b.shapes[1].icon_center_y, 300);
  near(b.shapes[2].y - b.shapes[1].y, 16);
  near(b.texts[0].line_top, 344);
  near(b.texts[0].clip_y, 330);
  TEST_ASSERT(strcmp(b.texts[0].value, "upright") == 0);
  near(b.texts[1].anchor_y, 370);
  TEST_ASSERT(b.hits[0].y == 340 && b.hits[0].key == 123);
  TEST_ASSERT(b.pad.y == 300 && b.bounds_y == 270);
  TEST_ASSERT(b.menu_card.y == 300 && b.menu_font.y == 300);
  signs_reflect(&b, 200);
  TEST_ASSERT(memcmp(&a, &b, sizeof(a)) == 0);
}
static void model_geometry(void) {
  for (int theme = SIGN_THEME_LIGHT; theme <= SIGN_THEME_DARK; theme++) {
    for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
      for (int mode = 0; mode < 3; mode++) {
        sign_frame_t above = scene((sign_style_t)style, SIGN_ABOVE,
                                   (sign_theme_t)theme, mode, 2000);
        sign_frame_t below = scene((sign_style_t)style, SIGN_BELOW,
                                   (sign_theme_t)theme, mode, 2000);
        double center = 321 - above.cat_lift + 55;
        TEST_ASSERT(above.shape_count == below.shape_count);
        TEST_ASSERT(above.text_count == below.text_count);
        // The last shape and text belong to the desk, which stays put.
        int shapes = above.shape_count - (mode == 2 ? 1 : 0);
        int texts = above.text_count - (mode == 2 ? 1 : 0);
        if (mode == 2) {
          // Below typing now extends the fan rod and lowers post rows.
          // Reflection equivalence is still checked with no desk or a menu.
          shapes = 0;
          texts = 0;
        }
        for (int i = 0; i < shapes; i++) {
          const sign_shape_t *a = &above.shapes[i], *b = &below.shapes[i];
          TEST_ASSERT(a->kind == b->kind && a->fill == b->fill);
          if (a->upright) {
            near(b->y - a->y, 2 * (center - a->icon_center_y));
            near(b->rotation, a->rotation);
          } else {
            near(b->y, 2 * center - (a->y + a->h));
            near(b->rotation, -a->rotation);
          }
        }
        for (int i = 0; i < texts; i++) {
          const sign_text_t *a = &above.texts[i], *b = &below.texts[i];
          TEST_ASSERT(strcmp(a->value, b->value) == 0);
          near(b->px, a->px);
          if (a->back >> 24)
            near(b->anchor_y, 2 * center - a->anchor_y + sign_tag_height(a));
          else
            near(b->line_top, 2 * center - (a->line_top + a->line_h));
        }
        if (mode == 2) {
          TEST_ASSERT(memcmp(&above.shapes[above.shape_count - 1],
                             &below.shapes[below.shape_count - 1],
                             sizeof(sign_shape_t)) == 0);
          TEST_ASSERT(memcmp(&above.texts[above.text_count - 1],
                             &below.texts[below.text_count - 1],
                             sizeof(sign_text_t)) == 0);
        }
        if (mode == 1) {
          TEST_ASSERT(below.menu_style[0].y > below.menu_lang[0].y);
          TEST_ASSERT(below.menu_lang[0].y > below.menu_font.y);
          TEST_ASSERT(below.menu_font.y > below.menu_theme[0].y);
        }
        if (mode != 2) {
          signs_reflect(&below, center);
          // Floating point transforms can round last bits; verify each field
          // above, then the integer geometry after the second reflection.
          TEST_ASSERT(memcmp(above.hits, below.hits, sizeof(above.hits)) == 0);
          TEST_ASSERT(above.bounds_y == below.bounds_y);
          TEST_ASSERT(memcmp(&above.pad, &below.pad, sizeof(above.pad)) == 0);
        }
      }
    }
  }
}
static uint8_t *render(const sign_frame_t *frame, int scale) {
  int width = W * scale / 120, height = H * scale / 120;
  uint8_t *pixels = calloc((size_t)width * height, 4);
  TEST_ASSERT(pixels);
  sign_draw(pixels, width, height, scale, frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, width, height, scale, frame, SIGN_DRAW_OVER);
  return pixels;
}
static void bases_only(sign_frame_t *frame) {
  int n = 0;
  for (int i = 0; i < frame->shape_count; i++)
    if (!frame->shapes[i].upright)
      frame->shapes[n++] = frame->shapes[i];
  frame->shape_count = n;
  frame->text_count = 0;
}
static void text_pixels(void) {
  for (int tag = 0; tag < 2; tag++) {
    sign_frame_t a = {.bounds_w = W, .bounds_h = H};
    a.text_count = 1;
    a.texts[0] = (sign_text_t){.x = 300,
                               .line_top = 100,
                               .line_h = 20,
                               .w = 200,
                               .px = 12.5,
                               .meta_px = 11,
                               .gap = 8,
                               .color = 0xff111827,
                               .meta_color = 0xff697386,
                               .clip_y = 96,
                               .clip_h = 28,
                               .anchor_y = 200,
                               .tag_scale = .96,
                               .back = tag ? 0xffffe1a0 : 0};
    strcpy(a.texts[0].value, "Upright abc!t");
    strcpy(a.texts[0].meta, "done");
    sign_frame_t b = a;
    signs_reflect(&b, 200);
    // Both the line and tag centers move by integral pixels in this scene.
    int dy = tag ? 30 : 180;
    uint8_t *above = render(&a, 120), *below = render(&b, 120);
    size_t ink = 0;
    for (int y = 0; y < H - dy; y++) {
      TEST_ASSERT(memcmp(above + (size_t)y * W * 4,
                         below + (size_t)(y + dy) * W * 4, (size_t)W * 4) == 0);
      for (int x = 0; x < W; x++)
        ink += above[((size_t)y * W + x) * 4 + 3] > 0;
    }
    TEST_ASSERT(ink > 0);
    free(above);
    free(below);
  }
}
static void pixels(void) {
  const int scales[] = {120, 150, 240};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    for (int theme = SIGN_THEME_LIGHT; theme <= SIGN_THEME_DARK; theme++) {
      sign_frame_t a =
          scene((sign_style_t)style, SIGN_ABOVE, (sign_theme_t)theme, 0, 2000);
      sign_frame_t b =
          scene((sign_style_t)style, SIGN_BELOW, (sign_theme_t)theme, 0, 2000);
      bases_only(&a);
      bases_only(&b);
      for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
        int scale = scales[s], width = W * scale / 120;
        int axis = 752 * scale / 120;
        uint8_t *above = render(&a, scale), *below = render(&b, scale);
        int differences = 0, maximum = 0;
        for (int y = 0; y < axis; y++) {
          for (int x = 0; x < width; x++) {
            size_t one = ((size_t)y * width + x) * 4;
            size_t two = ((size_t)(axis - 1 - y) * width + x) * 4;
            for (int channel = 0; channel < 4; channel++) {
              int delta = abs(above[one + channel] - below[two + channel]);
              if (delta > maximum)
                maximum = delta;
              if (delta > 0)
                differences++;
            }
          }
        }
        printf("bases style=%d theme=%d scale=%d max=%d differences=%d\n",
               style, theme, scale, maximum, differences);
        TEST_ASSERT(differences == 0);
        free(above);
        free(below);
      }
    }
  }
  // Render each upright group in isolation: its ink must be the same after
  // translation, including tilted fan icons and style/auto/moon card glyphs.
  for (int mode = 0; mode < 2; mode++) {
    sign_frame_t a =
        scene(SIGN_STYLE_FAN, SIGN_ABOVE, SIGN_THEME_LIGHT, mode, 2000);
    sign_frame_t b =
        scene(SIGN_STYLE_FAN, SIGN_BELOW, SIGN_THEME_LIGHT, mode, 2000);
    bool check = false, waiting = false;
    for (int i = 0; i < a.shape_count; i++) {
      if (!a.shapes[i].upright)
        continue;
      sign_frame_t one = {.bounds_w = W, .bounds_h = H};
      sign_frame_t two = one;
      one.shapes[0] = a.shapes[i];
      two.shapes[0] = b.shapes[i];
      one.shape_count = two.shape_count = 1;
      double dy = two.shapes[0].y - one.shapes[0].y;
      // Normalize translation before rasterization, avoiding a fractional
      // origin bias unrelated to whether the glyph is upside down.
      two.shapes[0].y -= dy;
      if (two.shapes[0].orbit)
        two.shapes[0].origin_y -= dy;
      uint8_t *pa = render(&one, 240), *pb = render(&two, 240);
      TEST_ASSERT(memcmp(pa, pb, (size_t)W * H * 16) == 0);
      size_t ink = 0;
      for (size_t pixel = 0; pixel < (size_t)W * H * 4; pixel++)
        ink += pa[pixel * 4 + 3] > 0;
      TEST_ASSERT(ink > 0);
      free(pa);
      free(pb);
      check |= a.shapes[i].kind == SIGN_CHECK;
      if (!mode && i + 1 < a.shape_count && a.shapes[i].h > 8 &&
          a.shapes[i + 1].upright && a.shapes[i + 1].h < a.shapes[i].h / 2 &&
          a.shapes[i + 1].icon_center_y == a.shapes[i].icon_center_y) {
        TEST_ASSERT(b.shapes[i + 1].y > b.shapes[i].y);
        waiting = true;
      }
    }
    if (!mode)
      TEST_ASSERT(check && waiting);
  }
}
// The desk sits the same distance under the cat whether the cat could rise
// for it or is held by the top edge of the output.
static double desk_gap(sign_orientation_t orientation, double cat_y) {
  agent_session_view_t session = {.key = 1, .pid = 100};
  strcpy(session.agent, "claude");
  strcpy(session.name, "desk-session");
  signs_t model = {0};
  sign_frame_t frame;
  sign_input_t input = {.sessions = &session,
                        .count = 1,
                        .style = SIGN_STYLE_FAN,
                        .orientation = orientation,
                        .animations = SIGN_ANIM_OFF,
                        .font_size = 13,
                        .cat_x = 260,
                        .cat_y = cat_y,
                        .cat_height = 110,
                        .typing = true,
                        .typing_key = 1,
                        .desk_snap = true,
                        .typing_until = 999999,
                        .now_ms = 1000};
  strcpy(input.desk_name, "desk-session");
  signs_frame(&model, &input, &frame);
  TEST_ASSERT(frame.shape_count > 0);
  // The desk board is emitted last.
  const sign_shape_t *board = &frame.shapes[frame.shape_count - 1];
  TEST_ASSERT(fabs(board->w - 164) < 0.01 && fabs(board->h - 26) < 0.01);
  return board->y - (cat_y - frame.cat_lift);
}
static void desk_at_edge(void) {
  double above = desk_gap(SIGN_ABOVE, 321);
  near(above, 8 + 67);
  near(desk_gap(SIGN_BELOW, 321), above);
  near(desk_gap(SIGN_BELOW, 3), above);  // Only three pixels of room.
  near(desk_gap(SIGN_BELOW, 0), above);  // Against the top edge.
}
int main(void) {
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  desk_at_edge();
  transform_geometry();
  model_geometry();
  pixels();
  text_pixels();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

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

#include "graphics/font_panel.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static font_panel_t panel;
static font_panel_t other;

static void near(double got, double want) {
  if (fabs(got - want) > 1e-4) {
    fprintf(stderr, "got %.6f want %.6f\n", got, want);
    TEST_ASSERT(fabs(got - want) <= 1e-4);
  }
}
static void face_set(font_panel_face_t *out, const char *name, bool mono) {
  memset(out, 0, sizeof(*out));
  snprintf(out->name, sizeof(out->name), "%s", name);
  out->mono = mono;
}
static void open_panel(sign_animations_t animations, int64_t now) {
  font_panel_reset(&panel);
  font_panel_face_t faces[2];
  face_set(&faces[0], "Mono Sans", false);
  face_set(&faces[1], "Proportional", true);
  font_panel_set_faces(&panel, faces, 2);
  TEST_ASSERT(font_panel_open(&panel, false, 1, animations, NULL, now));
}
static void place_at(double card_x, double card_y, double card_w, double card_h,
                     double panel_w, double panel_h, double output_w,
                     double output_h, double want_x, double want_y) {
  font_panel_box_t card = {card_x, card_y, card_w, card_h};
  font_panel_size_t size = {panel_w, panel_h};
  font_panel_size_t output = {output_w, output_h};
  double x = -1, y = -1;
  font_panel_place(&card, &size, &output, 10, &x, &y);
  near(x, want_x);
  near(y, want_y);
}
static void test_place(void) {
  place_at(100, 200, 154, 130, 384, 200, 2000, 1200, 264, 130);
  place_at(0, 0, 154, 130, 384, 200, 2000, 1200, 164, 0);
  place_at(1846, 0, 154, 130, 384, 200, 2000, 1200, 1452, 0);
  place_at(0, 1070, 154, 130, 384, 200, 2000, 1200, 164, 1000);
  place_at(1846, 1070, 154, 130, 384, 200, 2000, 1200, 1452, 1000);
  place_at(100, 20, 154, 130, 384, 300, 2000, 1200, 264, 0);
  place_at(646, 200, 154, 130, 384, 200, 800, 1200, 252, 130);
  place_at(100, 1100, 154, 130, 384, 400, 2000, 900, 264, 500);
  place_at(0, 0, 10, 10, 384, 200, 100, 100, 0, 0);
  place_at(0, 0, 154, 130, 384, 200, 548, 1200, 164, 0);
}
static void test_boxes(void) {
  near(font_panel_height(0, 1), 95);
  near(font_panel_height(1, 1), 121);
  near(font_panel_height(2, 1), 121);
  near(font_panel_height(3, 1), 151);
  near(font_panel_height(4, 1), 151);
  near(font_panel_height(20, 1), 391);
  near(font_panel_height(30, 1), 391);
  near(font_panel_height(31, 1), 391);
  near(font_panel_height(17, 1), 361);
  near(font_panel_height(30, 2), 782);
  near(font_panel_width(2), 768);
  open_panel(SIGN_ANIM_OFF, 0);
  font_panel_layout_t layout;
  font_panel_layout(&panel, &layout);
  near(layout.width, 384);
  near(layout.height, 151);
  near(layout.height, font_panel_height(font_panel_count(&panel), 1));
  near(layout.filter.x, 204);
  near(layout.filter.y, 12);
  near(layout.filter.w, 168);
  near(layout.filter.h, 24);
  near(layout.cells[0].x, 12);
  near(layout.cells[0].h, 26);
  // Two columns, each wide enough for a long family name.
  near(layout.cells[0].w, 178);
  near(layout.cells[1].x, 194);
  near(layout.cells[1].w, 178);
  near(layout.cells[1].y, layout.cells[0].y);
  near(layout.cells[2].x, 12);
  near(layout.cells[2].y, layout.cells[0].y + 30);
  near(layout.preview.x, 12);
  near(layout.preview.y, 108);
  near(layout.preview.h, 31);
  near(layout.bar.w, 0);
  int pixels_w = 0, pixels_h = 0;
  font_panel_pixels(&panel, 120, &pixels_w, &pixels_h);
  TEST_ASSERT(pixels_w == 384 && pixels_h == 151);
}
static void test_filter(void) {
  open_panel(SIGN_ANIM_OFF, 0);
  char text[64];
  font_panel_count_text(&panel, text, sizeof(text));
  TEST_ASSERT(!strcmp(text, "3 个字体"));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 0), "默认"));
  TEST_ASSERT(!strcmp(font_panel_family(&panel, 0), "sans-serif"));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 1), "Mono Sans"));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 2), "Proportional"));
  TEST_ASSERT(font_panel_label(&panel, 3) == NULL);
  TEST_ASSERT(font_panel_selected_index(&panel) == 0);
  font_panel_layout_t layout;
  font_panel_layout(&panel, &layout);
  double prop_x = layout.filter.x + layout.filter.w / 2;
  double mono_x = layout.filter.x + layout.filter.w * 5 / 6;
  double y = layout.filter.y + layout.filter.h / 2;
  char chosen[128];
  memset(chosen, 1, sizeof(chosen));
  TEST_ASSERT(!font_panel_click(&panel, prop_x, y, 10, chosen, sizeof(chosen)));
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(font_panel_count(&panel) == 2);
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 0), "默认"));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 1), "Mono Sans"));
  TEST_ASSERT(font_panel_label(&panel, 2) == NULL);
  TEST_ASSERT(!font_panel_click(&panel, mono_x, y, 20, chosen, sizeof(chosen)));
  font_panel_count_text(&panel, text, sizeof(text));
  TEST_ASSERT(!strcmp(text, "1 个字体"));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 0), "Proportional"));
  TEST_ASSERT(font_panel_selected_index(&panel) == -1);
  TEST_ASSERT(!strcmp(font_panel_preview_family(&panel), "sans-serif"));
  font_panel_set_language(&panel, true);
  font_panel_count_text(&panel, text, sizeof(text));
  TEST_ASSERT(!strcmp(text, "1 fonts"));
  font_panel_layout(&panel, &layout);
  double all_x = layout.filter.x + layout.filter.w / 6;
  TEST_ASSERT(!font_panel_click(&panel, all_x, y, 30, chosen, sizeof(chosen)));
  TEST_ASSERT(!strcmp(font_panel_label(&panel, 0), "Default"));
  font_panel_count_text(&panel, text, sizeof(text));
  TEST_ASSERT(!strcmp(text, "3 fonts"));
  font_panel_reset(&panel);
  font_panel_face_t prop;
  face_set(&prop, "Only Text", false);
  font_panel_set_faces(&panel, &prop, 1);
  font_panel_open(&panel, false, 1, SIGN_ANIM_OFF, NULL, 0);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(!font_panel_click(&panel, mono_x, y, 5, chosen, sizeof(chosen)));
  TEST_ASSERT(font_panel_count(&panel) == 0);
  font_panel_count_text(&panel, text, sizeof(text));
  TEST_ASSERT(!strcmp(text, "0 个字体"));
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.cell_count == 0);
  near(layout.height, 95);
  near(layout.preview.h, 31);
  near(layout.preview.y, 52);
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(!font_panel_click(&panel, 40, 60, 6, chosen, sizeof(chosen)));
  TEST_ASSERT(font_panel_is_open(&panel));
}
static void test_hover_and_close(void) {
  open_panel(SIGN_ANIM_OFF, 1000);
  font_panel_layout_t layout;
  font_panel_layout(&panel, &layout);
  double x = layout.cells[1].x + layout.cells[1].w / 2;
  double y = layout.cells[1].y + layout.cells[1].h / 2;
  font_panel_pointer(&panel, x, y, 1100);
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(!strcmp(font_panel_preview_family(&panel), "Mono Sans"));
  TEST_ASSERT(font_panel_selected_index(&panel) == 0);
  int hot = -2;
  near(font_panel_hot(&panel, &hot), 1);
  TEST_ASSERT(hot == 1);
  font_panel_leave(&panel, 1200);
  TEST_ASSERT(!strcmp(font_panel_preview_family(&panel), "sans-serif"));
  TEST_ASSERT(font_panel_is_open(&panel));
  char chosen[128];
  memset(chosen, 1, sizeof(chosen));
  font_panel_pointer(&panel, x, y, 1300);
  TEST_ASSERT(font_panel_click(&panel, x, y, 1300, chosen, sizeof(chosen)));
  TEST_ASSERT(!strcmp(chosen, "Mono Sans"));
  // Choosing keeps the panel open so several faces can be tried in a row.
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(font_panel_selected_index(&panel) == 1);
  TEST_ASSERT(!strcmp(font_panel_hover_family(&panel), "Mono Sans"));
  font_panel_leave(&panel, 1350);
  TEST_ASSERT(font_panel_hover_family(&panel) == NULL);
  font_panel_close(&panel);
  font_panel_wake_t wake = font_panel_step(&panel, 1400);
  TEST_ASSERT(!wake.frame && wake.timeout_ms < 0 && !wake.redraw);
  TEST_ASSERT(font_panel_hover_family(&panel) == NULL);
  open_panel(SIGN_ANIM_OFF, 0);
  font_panel_layout(&panel, &layout);
  x = layout.cells[0].x + 4;
  y = layout.cells[0].y + 4;
  TEST_ASSERT(font_panel_click(&panel, x, y, 10, chosen, sizeof(chosen)));
  TEST_ASSERT(chosen[0] == '\0');
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(font_panel_selected_index(&panel) == 0);
}
static void test_idle(void) {
  open_panel(SIGN_ANIM_OFF, 1000);
  font_panel_wake_t wake = font_panel_step(&panel, 1000);
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(!wake.frame && wake.timeout_ms == 10000);
  wake = font_panel_step(&panel, 10999);
  TEST_ASSERT(font_panel_is_open(&panel) && wake.timeout_ms == 1);
  wake = font_panel_step(&panel, 11000);
  TEST_ASSERT(!font_panel_is_open(&panel));
  TEST_ASSERT(!wake.frame && wake.timeout_ms < 0);
  open_panel(SIGN_ANIM_OFF, 0);
  font_panel_pointer(&panel, 1, 1, 4000);
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(font_panel_step(&panel, 13999).timeout_ms == 1);
  TEST_ASSERT(!font_panel_is_open(&panel) == false);
  font_panel_step(&panel, 14000);
  TEST_ASSERT(!font_panel_is_open(&panel));
  open_panel(SIGN_ANIM_OFF, 0);
  font_panel_leave(&panel, 5000);
  font_panel_step(&panel, 10000);
  TEST_ASSERT(!font_panel_is_open(&panel));
  open_panel(SIGN_ANIM_FULL, 0);
  wake = font_panel_step(&panel, 0);
  TEST_ASSERT(wake.frame && wake.timeout_ms == 10000);
  font_panel_close(&panel);
  wake = font_panel_step(&panel, 10);
  TEST_ASSERT(!font_panel_is_open(&panel));
  TEST_ASSERT(!wake.frame && wake.timeout_ms < 0);
}
static void test_scroll(void) {
  font_panel_reset(&panel);
  font_panel_face_t faces[90];
  for (int i = 0; i < 90; i++) {
    char name[32];
    snprintf(name, sizeof(name), "Face %02d", i);
    face_set(&faces[i], name, true);
  }
  font_panel_set_faces(&panel, faces, 90);
  font_panel_open(&panel, true, 1, SIGN_ANIM_OFF, "Face 00", 0);
  font_panel_layout_t layout;
  font_panel_layout(&panel, &layout);
  double x = layout.filter.x + layout.filter.w * 5 / 6;
  double y = layout.filter.y + 8;
  TEST_ASSERT(!font_panel_click(&panel, x, y, 10, NULL, 0));
  TEST_ASSERT(font_panel_count(&panel) == 90);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.cell_count == 20);
  TEST_ASSERT(layout.rows == 45);
  near(layout.bar.w, 3);
  near(layout.bar.x, 372);
  near(layout.bar.y, layout.cells[0].y);
  near(layout.bar.h, 296.0 * 10 / 45);
  font_panel_wheel(&panel, 9, 20);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.first_row == 8);
  // Forty-five rows of two, ten visible: the last first row is 35.
  for (int turn = 0; turn < 4; turn++)
    font_panel_wheel(&panel, 9, 30 + turn);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.first_row == 35);
  near(layout.bar.y + layout.bar.h, layout.cells[19].y + layout.cells[19].h);
  font_panel_wheel(&panel, 9, 50);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.first_row == 35);
  font_panel_wheel(&panel, -100, 60);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.first_row == 27);
  for (int turn = 0; turn < 4; turn++)
    font_panel_wheel(&panel, -100, 70 + turn);
  font_panel_layout(&panel, &layout);
  TEST_ASSERT(layout.first_row == 0);
  near(layout.height, font_panel_height(90, 1));
  near(layout.cells[0].h, 26);
  near(layout.preview.h, 31);
}
static void test_off_motion(void) {
  font_panel_reset(&other);
  font_panel_face_t faces[2];
  face_set(&faces[0], "Mono Sans", false);
  face_set(&faces[1], "Proportional", true);
  font_panel_set_faces(&other, faces, 2);
  font_panel_open(&other, false, 1, SIGN_ANIM_FULL, NULL, 0);
  font_panel_reset(&panel);
  font_panel_set_faces(&panel, faces, 2);
  font_panel_open(&panel, false, 1, SIGN_ANIM_OFF, NULL, 0);
  font_panel_layout_t full, off;
  font_panel_layout(&other, &full);
  font_panel_layout(&panel, &off);
  double y = full.filter.y + full.filter.h / 2;
  double mono = full.filter.x + full.filter.w * 5 / 6;
  TEST_ASSERT(!font_panel_click(&other, mono, y, 0, NULL, 0));
  TEST_ASSERT(!font_panel_click(&panel, mono, y, 0, NULL, 0));
  font_panel_layout(&other, &full);
  font_panel_layout(&panel, &off);
  near(full.thumb.x, 208);
  TEST_ASSERT(fabs(off.thumb.x - full.thumb.x) > 1);
  font_panel_wake_t wake = font_panel_step(&panel, 0);
  TEST_ASSERT(!wake.frame);
  wake = font_panel_step(&other, 0);
  TEST_ASSERT(wake.frame);
  TEST_ASSERT(!font_panel_step(&other, 280).frame);
  font_panel_layout(&other, &full);
  near(full.thumb.x, off.thumb.x);
  int hot = 0;
  font_panel_pointer(&panel, off.cells[0].x + 2, off.cells[0].y + 2, 300);
  near(font_panel_hot(&panel, &hot), 1);
  font_panel_pointer(&other, off.cells[0].x + 2, off.cells[0].y + 2, 300);
  near(font_panel_hot(&other, &hot), 0);
  TEST_ASSERT(font_panel_step(&other, 300).frame);
  TEST_ASSERT(!font_panel_step(&other, 420).frame);
  near(font_panel_hot(&other, &hot), 1);
}
static bool yellow(const uint8_t *pixel) {
  return pixel[3] > 240 && pixel[2] > 230 && abs(pixel[1] - 228) < 36 &&
         abs(pixel[0] - 163) < 36;
}
static void yellow_span(const uint8_t *buf, int width, int height, int *top,
                        int *bottom, int *left) {
  *top = *bottom = *left = -1;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (!yellow(buf + ((size_t)y * (size_t)width + (size_t)x) * 4))
        continue;
      if (*top < 0)
        *top = y;
      *bottom = y;
      if (*left < 0 || x < *left)
        *left = x;
    }
  }
}
static void test_fixed_paint(void) {
  TEST_ASSERT(text_init("Noto Sans") == 0);
  const char *candidates[] = {"Noto Sans", "Noto Serif", "Liberation Mono",
                              "DejaVu Sans"};
  const char *wide = NULL, *narrow = NULL;
  int wide_w = 0, narrow_w = 0;
  for (int i = 0; i < 4; i++) {
    int width =
        text_measure_family(candidates[i], "wayland-bongocat", 13, true);
    if (width <= 0)
      continue;
    if (!narrow || width < narrow_w) {
      narrow = candidates[i];
      narrow_w = width;
    }
    if (!wide || width > wide_w) {
      wide = candidates[i];
      wide_w = width;
    }
  }
  TEST_ASSERT(wide && narrow && wide_w > narrow_w);
  font_panel_reset(&panel);
  font_panel_face_t faces[2];
  face_set(&faces[0], narrow, false);
  face_set(&faces[1], wide, false);
  font_panel_set_faces(&panel, faces, 2);
  font_panel_open(&panel, false, 1, SIGN_ANIM_OFF, narrow, 0);
  int bw = 0, bh = 0;
  font_panel_pixels(&panel, 120, &bw, &bh);
  uint8_t *first = calloc((size_t)bw * (size_t)bh, 4);
  uint8_t *second = calloc((size_t)bw * (size_t)bh, 4);
  TEST_ASSERT(first && second);
  font_panel_layout_t before;
  font_panel_layout(&panel, &before);
  font_panel_pointer(&panel, before.cells[1].x + 8, before.cells[1].y + 8, 10);
  font_panel_layout_t left, right;
  font_panel_pointer(&panel, before.cells[1].x + 8, before.cells[1].y + 8, 10);
  font_panel_layout(&panel, &left);
  font_panel_draw(&panel, first, bw, bh, 120);
  font_panel_pointer(&panel, before.cells[2].x + 8, before.cells[2].y + 8, 20);
  font_panel_layout(&panel, &right);
  font_panel_draw(&panel, second, bw, bh, 120);
  near(left.preview.x, right.preview.x);
  near(left.preview.y, right.preview.y);
  near(left.preview.h, 31);
  near(right.preview.h, 31);
  near(left.height, right.height);
  near(left.cells[1].h, 26);
  near(left.filter.y, right.filter.y);
  TEST_ASSERT(left.preview.w != right.preview.w);
  TEST_ASSERT(!strcmp(font_panel_preview_family(&panel), wide));
  TEST_ASSERT(font_panel_is_open(&panel));
  TEST_ASSERT(font_panel_selected_index(&panel) == 1);
  int top_a, bot_a, left_a, top_b, bot_b, left_b;
  yellow_span(first, bw, bh, &top_a, &bot_a, &left_a);
  yellow_span(second, bw, bh, &top_b, &bot_b, &left_b);
  TEST_ASSERT(top_a > 60 && bot_a > top_a);
  TEST_ASSERT(top_a == top_b && bot_a == bot_b && left_a == left_b);
  const char *names[FONT_PANEL_CAP];
  int count = text_families("en", names, FONT_PANEL_CAP);
  TEST_ASSERT(count > 0);
  int limit = count < 40 ? count : 40;
  font_panel_face_t many[40];
  for (int i = 0; i < limit; i++)
    face_set(&many[i], names[i],
             text_spacing_mono(text_family_spacing(names[i])));
  font_panel_set_faces(&panel, many, limit);
  font_panel_draw(&panel, first, bw, bh, 120);
  TEST_ASSERT(text_glyph_count() <= 512);
  if (font_panel_count(&panel) > 30)
    font_panel_wheel(&panel, 8, 30);
  font_panel_draw(&panel, first, bw, bh, 120);
  TEST_ASSERT(text_glyph_count() <= 512);
  free(first);
  free(second);
  sign_draw_cleanup();
  text_cleanup();
}
static void test_prepared(void) {
  font_panel_reset(&panel);
  TEST_ASSERT(font_panel_prepared(&panel) == -1);
  font_panel_set_prepared(&panel, -4);
  TEST_ASSERT(font_panel_prepared(&panel) == -1);
  font_panel_set_prepared(&panel, 0);
  TEST_ASSERT(font_panel_prepared(&panel) == 0);
  font_panel_open(&panel, true, 1, SIGN_ANIM_OFF, NULL, 0);
  TEST_ASSERT(font_panel_opacity(&panel) > 0.99);
  font_panel_close(&panel);
  font_panel_open(&panel, true, 1, SIGN_ANIM_FULL, NULL, 0);
  TEST_ASSERT(font_panel_opacity(&panel) < 0.01);
  font_panel_step(&panel, 140);
  TEST_ASSERT(font_panel_opacity(&panel) > 0.99);
}
int main(void) {
  test_prepared();
  test_place();
  test_boxes();
  test_filter();
  test_hover_and_close();
  test_idle();
  test_scroll();
  test_off_motion();
  test_fixed_paint();
  return 0;
}

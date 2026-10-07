#include "graphics/post_text_layout.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <math.h>
#include <string.h>

static int fallback;
void *__real_FT_Get_Sfnt_Table(FT_Face face, FT_Sfnt_Tag tag);
void *__wrap_FT_Get_Sfnt_Table(FT_Face face, FT_Sfnt_Tag tag);
FT_UInt __real_FT_Get_Char_Index(FT_Face face, FT_ULong cp);
FT_UInt __wrap_FT_Get_Char_Index(FT_Face face, FT_ULong cp);
void *__wrap_FT_Get_Sfnt_Table(FT_Face face, FT_Sfnt_Tag tag) {
  return fallback ? NULL : __real_FT_Get_Sfnt_Table(face, tag);
}
FT_UInt __wrap_FT_Get_Char_Index(FT_Face face, FT_ULong cp) {
  return fallback == 2 && cp == 'H' ? 0 : __real_FT_Get_Char_Index(face, cp);
}
static void cap_height(void) {
  for (fallback = 0; fallback < 3; fallback++) {
    TEST_ASSERT(text_init("Noto Serif CJK TC") == 0);
    text_metrics_t m;
    TEST_ASSERT(text_metrics(13, true, &m));
    TEST_ASSERT(m.cap_source == (fallback == 0   ? TEXT_CAP_OS2
                                 : fallback == 1 ? TEXT_CAP_GLYPH
                                                 : TEXT_CAP_NONE));
    double expect = fallback == 2
                        ? 10 + (20 - m.ascent - m.descent) / 2 + m.ascent
                        : 20 + m.cap_height / 2;
    TEST_ASSERT(fabs(text_baseline(10, 20, 13, true) - expect) < 1e-9);
    int count = text_metrics_count();
    for (int i = 0; i < 100; i++)
      TEST_ASSERT(text_baseline(10, 20, 13, true) == expect);
    TEST_ASSERT(text_metrics_count() == count);
    text_set_scale(180);
    TEST_ASSERT(text_metrics(13, true, &m));
    TEST_ASSERT(text_metrics_count() == count + 1);
    text_set_scale(120);
    TEST_ASSERT(text_metrics(13, true, &m));
    TEST_ASSERT(text_metrics_count() == count + 1);
    text_metrics_t family;
    TEST_ASSERT(text_metrics_family("Noto Sans", 13, false, &family));
    count = text_match_count();
    int metrics = text_metrics_count();
    for (int i = 0; i < 100; i++)
      TEST_ASSERT(text_metrics_family("Noto Sans", 13, false, &family));
    TEST_ASSERT(text_match_count() == count && text_metrics_count() == metrics);
    text_cleanup();
  }
  fallback = 0;
}
static sign_input_t input(agent_session_view_t *session) {
  return (sign_input_t){.sessions = session,
                        .count = 1,
                        .style = SIGN_STYLE_POST,
                        .animations = SIGN_ANIM_OFF,
                        .name = SIGN_NAME_PROJECT,
                        .name_extra = SIGN_EXTRA_INLINE,
                        .title_length = 0,
                        .english = true,
                        .open = true,
                        .cat_x = 210,
                        .cat_y = 210,
                        .cat_height = 110,
                        .surface_width = 900,
                        .now_ms = 100000};
}
static void boards(void) {
  TEST_ASSERT(text_init("Noto Sans") == 0);
  agent_session_view_t session = {.key = 1,
                                  .order = 1,
                                  .agent = "codex",
                                  .name = "i",
                                  .state = AGENT_STATE_IDLE};
  sign_input_t in = input(&session);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].w == POST_BOARD_MIN);
  strcpy(session.name, "medium-sized-project");
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].w > POST_BOARD_MIN &&
              frame.hits[0].w < POST_BOARD_MAX);
  memset(session.title, 'W', sizeof(session.title) - 1);
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].w == POST_BOARD_MAX);
  post_text_layout_t layout;
  post_text_layout(&frame.texts[0], &layout);
  TEST_ASSERT(layout.name_budget >= text_measure(session.name, 13, true));
  TEST_ASSERT(strstr(layout.extra, "…"));
  int count = text_measure_count();
  for (int i = 0; i < 100; i++) {
    signs_frame(&model, &in, &frame);
    post_text_layout(&frame.texts[0], &layout);
  }
  TEST_ASSERT(text_measure_count() == count);
  // Supplement loses its space before the main name gets ellipsized.
  sign_text_t text = frame.texts[0];
  double name = text_measure(text.value, (float)text.px, true);
  double meta = text_measure(text.meta, (float)text.meta_px, false);
  text.w = name + meta + 2 * text.gap + 23;
  post_text_layout(&text, &layout);
  TEST_ASSERT(!layout.extra[0] && layout.name_budget >= name);
  text.w = name + meta + text.gap - 10;
  post_text_layout(&text, &layout);
  TEST_ASSERT(!layout.extra[0] && layout.name_budget == name - 10);
  for (int direction = 0; direction < 2; direction++)
    for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
         orientation++) {
      memset(&model, 0, sizeof(model));
      session.order = direction ? 2 : 1;
      in.orientation = (sign_orientation_t)orientation;
      in.animations = SIGN_ANIM_OFF;
      in.cat_x = 210;
      in.surface_width = 900;
      signs_frame(&model, &in, &frame);
      // The target also covers where a displaced board rests.
      TEST_ASSERT(frame.hits[0].w >= POST_BOARD_MAX &&
                  frame.hits[0].w <= POST_BOARD_MAX + 6);
      in.animations = SIGN_ANIM_FULL;
      in.now_ms += 16;
      in.cat_x = direction ? -70 : 130;
      in.surface_width = 400;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.hits[0].w < POST_BOARD_MIN);
      TEST_ASSERT(frame.hits[0].x >= 0);
      TEST_ASSERT(frame.hits[0].x + frame.hits[0].w <= 400);
      // Edge tightening must also clamp the in-flight previous width.
      in.animations = SIGN_ANIM_FULL;
      in.has_hover = true;
      in.hover_key = 1;
      for (int t = 0; t < 800; t += 16) {
        in.now_ms += 16;
        signs_frame(&model, &in, &frame);
        TEST_ASSERT(frame.hits[0].x >= 0);
        TEST_ASSERT(frame.hits[0].x + frame.hits[0].w <= 400);
      }
    }
  in = input(&session);
  in.open = false;
  in.idle = SIGN_IDLE_ALWAYS;
  memset(&model, 0, sizeof(model));
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].w == 34);
  text_cleanup();
}
static void caret_and_menu(void) {
  const char *families[] = {"Noto Serif CJK TC", "Noto Sans"};
  for (int f = 0; f < 2; f++) {
    TEST_ASSERT(text_init(families[f]) == 0);
    sign_input_t in = {.style = SIGN_STYLE_POST,
                       .animations = SIGN_ANIM_OFF,
                       .menu = true,
                       .menu_font_hot = true,
                       .cat_x = 50,
                       .cat_y = 180,
                       .cat_height = 110};
    for (int named = 0; named < 2; named++) {
      if (named)
        snprintf(in.menu_font, sizeof(in.menu_font), "%s", families[f]);
      signs_t model = {0};
      sign_frame_t frame;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.text_count == 3);
      for (int t = 0; t < frame.text_count; t++) {
        uint8_t pixels[300 * 240 * 4] = {0};
        sign_frame_t ink = {.text_count = 1, .bounds_w = 300, .bounds_h = 240};
        ink.texts[0] = frame.texts[t];
        ink.texts[0].color = 0xffffffff;
        sign_draw(pixels, 300, 240, 120, &ink, SIGN_DRAW_OVER);
        double sum = 0, moment = 0;
        for (int y = 0; y < 240; y++)
          for (int x = 0; x < 300; x++) {
            unsigned alpha = pixels[(y * 300 + x) * 4 + 3];
            sum += alpha;
            moment += alpha * (y + .5);
          }
        sign_rect_t track = t < 2 ? frame.menu_lang[0] : frame.menu_font;
        TEST_ASSERT(sum > 0);
        TEST_ASSERT(fabs(moment / sum - (track.y + track.h / 2.0)) <= 1);
      }
    }
    uint8_t pixels[200 * 60 * 4] = {0};
    sign_frame_t frame = {.text_count = 1, .bounds_w = 200, .bounds_h = 60};
    frame.texts[0] = (sign_text_t){.x = 10,
                                   .line_top = 20,
                                   .line_h = 16,
                                   .w = 100,
                                   .px = 13,
                                   .gap = 4,
                                   .clip_y = 10,
                                   .clip_h = 40,
                                   .value = "H",
                                   .color = 0xffff0000,
                                   .meta_color = 0xff00ff00,
                                   .caret = true};
    sign_draw(pixels, 200, 60, 120, &frame, SIGN_DRAW_UNDER);
    double sum = 0, moment = 0;
    for (int y = 0; y < 60; y++)
      for (int x = 0; x < 200; x++) {
        unsigned green = pixels[(y * 200 + x) * 4 + 1];
        sum += green;
        moment += green * (y + .5);
      }
    TEST_ASSERT(sum > 0 && fabs(moment / sum - 28) <= .5);
    text_cleanup();
  }
}
int main(void) {
  cap_height();
  boards();
  caret_and_menu();
  sign_draw_cleanup();
  puts("stage 23 cap sources, caching, content widths and edge limits passed");
  return 0;
}

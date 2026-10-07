#define _GNU_SOURCE
#include "config/config.h"
#include "graphics/post_text_layout.h"
#include "graphics/sign_draw.h"
#include "graphics/sign_names.h"
#include "graphics/sign_palette.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static config_t defaults(void) {
  char path[] = "/tmp/herdcat-sign-names-config-XXXXXX";
  int fd = mkstemp(path);
  TEST_ASSERT(fd >= 0);
  close(fd);
  config_t config;
  TEST_ASSERT(load_config_strict(&config, path) == HERDCAT_SUCCESS);
  unlink(path);
  return config;
}

static sign_frame_t scene(const config_t *config, sign_style_t style,
                          sign_theme_t theme, sign_orientation_t orientation,
                          bool titled) {
  text_set_scale(120);
  agent_session_view_t sessions[2] = {
      {.key = 1,
       .order = 1,
       .agent = "claude",
       .name = "repo",
       .state = AGENT_STATE_WAITING},
      {.key = 2,
       .order = 2,
       .agent = "codex",
       .name = "notes",
       .state = AGENT_STATE_DONE,
       .unread = true}
  };
  if (titled) {
    strcpy(sessions[0].title, "Fix session labels");
    strcpy(sessions[1].title, "Review notes");
  }
  sign_input_t in = {.sessions = sessions,
                     .count = 2,
                     .style = style,
                     .theme = theme,
                     .orientation = orientation,
                     .name = config->sign_name,
                     .name_extra = config->sign_name_extra,
                     .title_length = config->sign_title_length,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .english = true,
                     .open = true,
                     .has_hover = true,
                     .hover_key = 1,
                     .cat_x = 220,
                     .cat_y = 290,
                     .cat_height = 110,
                     .surface_width = 640,
                     .surface_height = 800};
  snprintf(in.nameplate, sizeof(in.nameplate), "%s", config->sign_nameplate);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  return frame;
}

static uint8_t *render_pixels(const sign_frame_t *frame, int scale) {
  size_t size = 960 * 1200 * 4;
  uint8_t *data = calloc(size, 1);
  TEST_ASSERT(data);
  sign_draw(data, 960, 1200, scale, frame, SIGN_DRAW_UNDER);
  sign_draw(data, 960, 1200, scale, frame, SIGN_DRAW_OVER);
  return data;
}

static bool same_pixels(const sign_frame_t *a, const sign_frame_t *b,
                        int scale) {
  uint8_t *first = render_pixels(a, scale), *second = render_pixels(b, scale);
  bool equal = !memcmp(first, second, 960 * 1200 * 4);
  free(first);
  free(second);
  return equal;
}

static uint64_t pixels(const sign_frame_t *frame, int scale) {
  // Golden hashes always run before text_init; font-dependent checks compare
  // complete buffers instead, so installed fonts cannot enter the goldens.
  size_t size = 960 * 1200 * 4;
  uint8_t *data = render_pixels(frame, scale);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < size; i++)
    hash = (hash ^ data[i]) * UINT64_C(1099511628211);
  free(data);
  return hash;
}

static void no_title_pixels(void) {
  // Frozen without loading a font. Post goldens reflect the split plates;
  // fan values were captured before this change, also without a font.
  static const uint64_t expected[] = {
      UINT64_C(0x118254e2911bcbe3), UINT64_C(0x78071947bfd4dd32),
      UINT64_C(0xf6d58c501cb6f8be), UINT64_C(0xb8487f95645618ff),
      UINT64_C(0xbfb4ba4c9d01034c), UINT64_C(0x5cbe6cc42852c9e6),
      UINT64_C(0xf22b145fda63ad78), UINT64_C(0x281ed7b427070a69),
      UINT64_C(0x2ade814089181345), UINT64_C(0x910a3ab928134e7c),
      UINT64_C(0x5bf40f95f6d4043b), UINT64_C(0x80d53c3272847855),
      UINT64_C(0x541c7c22afff812c), UINT64_C(0xa7b3fbfb48827f09),
      UINT64_C(0x411937d454b11cf3), UINT64_C(0x95fd5fb0c5efb64e),
      UINT64_C(0xd83eca0eec87286b), UINT64_C(0x07160c897cf500b8),
      UINT64_C(0xa80faec0bd015e49), UINT64_C(0x79d9a71328d4afe7),
      UINT64_C(0xa85a3428a3987152), UINT64_C(0x1d9d11f13cbc26ec),
      UINT64_C(0x46d914a766bb8de5), UINT64_C(0x23f7af77bc3c97d6),
  };
  size_t at = 0;
  config_t config = defaults();
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int theme = 0; theme < 2; theme++)
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        sign_frame_t frame =
            scene(&config, (sign_style_t)style, (sign_theme_t)theme,
                  (sign_orientation_t)orientation, false);
        for (int scale = 120; scale <= 180; scale += 30)
          TEST_ASSERT(pixels(&frame, scale) == expected[at++]);
      }
  config_cleanup_full(&config);
}

static const sign_text_t *label(const sign_frame_t *frame, const char *name) {
  for (int i = 0; i < frame->text_count; i++)
    if (!strncmp(frame->texts[i].value, name, strlen(name)))
      return &frame->texts[i];
  TEST_ASSERT(false);
  return NULL;
}

static void default_names(void) {
  config_t config = defaults();
  TEST_ASSERT(config.sign_name == SIGN_NAME_PROJECT &&
              config.sign_name_extra == SIGN_EXTRA_INLINE);
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (int theme = 0; theme < 2; theme++)
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        sign_frame_t a =
            scene(&config, (sign_style_t)style, (sign_theme_t)theme,
                  (sign_orientation_t)orientation, true);
        const sign_text_t *repo = label(&a, "repo");
        if (style == SIGN_STYLE_FAN) {
          TEST_ASSERT(strstr(repo->nameplate.text, "Fix session"));
          TEST_ASSERT(repo->nameplate.runs[0].bold);
        } else {
          TEST_ASSERT(a.text_count == 2);
          TEST_ASSERT(strstr(repo->extra, "Fix session"));
          TEST_ASSERT(!strcmp(label(&a, "notes")->extra, "Review notes"));
          TEST_ASSERT(repo->reverse != label(&a, "notes")->reverse);
          TEST_ASSERT(repo->secondary_color ==
                      sign_palette((sign_theme_t)theme)->secondary);
          for (int e = SIGN_EXTRA_INLINE; e <= SIGN_EXTRA_BELOW; e++) {
            config.sign_name_extra = (sign_name_extra_t)e;
            sign_frame_t b =
                scene(&config, SIGN_STYLE_POST, (sign_theme_t)theme,
                      (sign_orientation_t)orientation, true);
            TEST_ASSERT(!memcmp(&a, &b, sizeof(a)));
          }
          config.sign_name_extra = SIGN_EXTRA_INLINE;
          strcpy(config.sign_nameplate, "**{title}**");
          sign_frame_t custom =
              scene(&config, SIGN_STYLE_POST, (sign_theme_t)theme,
                    (sign_orientation_t)orientation, true);
          TEST_ASSERT(!memcmp(&a, &custom, sizeof(a)));
          config.sign_nameplate[0] = 0;
          config.sign_name_extra = SIGN_EXTRA_OFF;
          sign_frame_t off =
              scene(&config, SIGN_STYLE_POST, (sign_theme_t)theme,
                    (sign_orientation_t)orientation, true);
          sign_frame_t untitled =
              scene(&config, SIGN_STYLE_POST, (sign_theme_t)theme,
                    (sign_orientation_t)orientation, false);
          TEST_ASSERT(!off.texts[0].extra[0] && !off.texts[1].extra[0]);
          TEST_ASSERT(same_pixels(&off, &untitled, 120));
          TEST_ASSERT(!same_pixels(&a, &off, 120));
          config.sign_name_extra = SIGN_EXTRA_INLINE;
        }
        config.sign_name = SIGN_NAME_AUTO;
        sign_frame_t alias =
            scene(&config, (sign_style_t)style, (sign_theme_t)theme,
                  (sign_orientation_t)orientation, true);
        TEST_ASSERT(!memcmp(&a, &alias, sizeof(a)));
        config.sign_name = SIGN_NAME_PROJECT;
      }
  config.sign_name = SIGN_NAME_TITLE;
  sign_frame_t title =
      scene(&config, SIGN_STYLE_POST, SIGN_THEME_LIGHT, SIGN_ABOVE, true);
  TEST_ASSERT(!strcmp(label(&title, "Fix session")->extra, "repo"));
  title = scene(&config, SIGN_STYLE_POST, SIGN_THEME_LIGHT, SIGN_ABOVE, false);
  TEST_ASSERT(!label(&title, "repo")->extra[0]);
  config_cleanup_full(&config);
}

static void board_width(void) {
  for (int reverse = 0; reverse < 2; reverse++) {
    sign_text_t text = {.x = 30,
                        .w = 360,
                        .px = 13,
                        .meta_px = 11.5,
                        .gap = 7,
                        .reverse = reverse,
                        .value = "repo",
                        .meta = "Claude · Waiting",
                        .extra = "Short title",
                        .color = 0xff111827,
                        .meta_color = 0xffb45309,
                        .secondary_color = 0xff697386,
                        .line_top = 20,
                        .line_h = 16,
                        .clip_y = 18,
                        .clip_h = 22};
    double name_w = text_measure(text.value, 13, true);
    double meta_w = text_measure(text.meta, 11.5, false);
    post_text_layout_t layout;
    post_text_layout(&text, &layout);
    TEST_ASSERT(!strcmp(layout.extra, "Short title"));
    TEST_ASSERT(layout.name_budget == text.w - text.gap - meta_w);
    if (reverse) {
      TEST_ASSERT(layout.meta_x == text.x);
      TEST_ASSERT(layout.extra_x >= layout.meta_x + meta_w + text.gap);
      TEST_ASSERT(layout.extra_x + layout.extra_width + text.gap ==
                  layout.name_x);
      TEST_ASSERT(layout.name_x + name_w == text.x + text.w);
    } else {
      TEST_ASSERT(layout.name_x == text.x);
      TEST_ASSERT(layout.extra_x == layout.name_x + name_w + text.gap);
      TEST_ASSERT(layout.extra_x + layout.extra_width + text.gap <=
                  layout.meta_x);
    }
    // Check actual ink against an independent extra draw at the expected x.
    sign_frame_t frame = {.text_count = 1, .bounds_w = 500, .bounds_h = 60};
    frame.texts[0] = text;
    uint8_t p[500 * 60 * 4] = {0}, q[500 * 60 * 4] = {0};
    sign_draw(p, 500, 60, 120, &frame, SIGN_DRAW_UNDER);
    frame.texts[0].extra[0] = 0;
    sign_draw(q, 500, 60, 120, &frame, SIGN_DRAW_UNDER);
    double expected_x = reverse ? text.x + text.w - name_w - text.gap -
                                      text_measure(text.extra, 11.5, false)
                                : text.x + name_w + text.gap;
    double baseline = text_baseline(text.line_top, text.line_h, 13, true);
    text_draw_clip(q, 500, 60, (int)lround(expected_x), (int)lround(baseline),
                   "Short title", 11.5, false, text.secondary_color, 0,
                   (text_clip_t){30, 18, 360, 22});
    TEST_ASSERT(!memcmp(p, q, sizeof(p)));
    text.w = name_w + meta_w + 2 * text.gap + 24;
    strcpy(text.extra, "一二三四五六七八九十");
    post_text_layout(&text, &layout);
    TEST_ASSERT(layout.extra[0] && strstr(layout.extra, "…"));
    TEST_ASSERT(layout.extra_width <= 24);
    TEST_ASSERT(text_measure(layout.extra, 11.5, false) == layout.extra_width);
    if (reverse)
      TEST_ASSERT(layout.extra_x + layout.extra_width + text.gap ==
                  layout.name_x);
    text.w -= .001;
    post_text_layout(&text, &layout);
    TEST_ASSERT(!layout.extra[0] && !layout.extra_width);
    frame.texts[0] = text;
    sign_frame_t hidden = frame;
    frame.texts[0].extra[0] = 0;
    TEST_ASSERT(same_pixels(&frame, &hidden, 120));
    text.w += 1;
    memset(text.extra, 'x', sizeof(text.extra) - 1);
    text.extra[sizeof(text.extra) - 1] = 0;
    post_text_layout(&text, &layout);
    TEST_ASSERT(layout.extra_width <= 25 && strstr(layout.extra, "…"));
    strcpy(text.value, "A main name consuming the entire board width");
    post_text_layout(&text, &layout);
    TEST_ASSERT(!layout.extra[0]);
  }
}

static void desk_and_equal_names(void) {
  agent_session_view_t session = {.key = 1,
                                  .agent = "claude",
                                  .name = "repo",
                                  .title = "repo",
                                  .state = AGENT_STATE_IDLE};
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = SIGN_STYLE_POST,
                     .name = SIGN_NAME_PROJECT,
                     .name_extra = SIGN_EXTRA_INLINE,
                     .animations = SIGN_ANIM_OFF,
                     .cat_x = 200,
                     .cat_y = 290,
                     .cat_height = 110,
                     .open = true,
                     .idle = SIGN_IDLE_ALWAYS};
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.texts[0].extra[0]);
  strcpy(session.title, "Session title");
  for (int name = SIGN_NAME_AUTO; name <= SIGN_NAME_TITLE; name++) {
    in.name = (sign_name_t)name;
    in.typing = true;
    in.typing_key = 1;
    in.typing_until = 2500;
    in.desk_snap = true;
    sign_session_name(&in, &session, in.desk_name, NULL);
    memset(&model, 0, sizeof(model));
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.text_count == 1 && frame.texts[0].caret);
    TEST_ASSERT(!strcmp(frame.texts[0].value,
                        name == SIGN_NAME_TITLE ? "Session title" : "repo"));
    TEST_ASSERT(!frame.texts[0].extra[0]);
  }
}

int main(void) {
  no_title_pixels();
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  default_names();
  board_width();
  desk_and_equal_names();
  sign_draw_cleanup();
  text_cleanup();
  puts("sign name rendering defaults, frozen pixels, board extras and desk "
       "passed");
  return 0;
}

#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static sign_frame_t scene(sign_style_t style, sign_theme_t theme,
                          sign_orientation_t orientation, bool english,
                          agent_state_t state, agent_terminal_kind_t kind,
                          bool detached) {
  agent_session_view_t session = {
      .key = 1,
      .pid = 42,
      .state = state,
      .agent = "claude",
      .name = "repo",
      .unread = true,
      .terminal = {.kind = kind, .detached = detached}
  };
  sign_input_t input = {.sessions = &session,
                        .count = 1,
                        .style = style,
                        .theme = theme,
                        .orientation = orientation,
                        .english = english,
                        .animations = SIGN_ANIM_OFF,
                        .idle = SIGN_IDLE_ALWAYS,
                        .open = true,
                        .has_hover = true,
                        .hover_key = 1,
                        .cat_x = 220,
                        .cat_y = 260,
                        .cat_height = 110,
                        .now_ms = 60000};
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &input, &frame);
  return frame;
}
static void alpha(uint32_t a, uint32_t b) {
  TEST_ASSERT((a & 0xffffff) == (b & 0xffffff));
  TEST_ASSERT((b >> 24) == (unsigned)lround((a >> 24) * .55));
}
int main(void) {
  TEST_ASSERT(text_init("DejaVu Sans") == 0);
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    for (int theme = SIGN_THEME_LIGHT; theme <= SIGN_THEME_DARK; theme++) {
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        for (int language = 0; language < 2; language++) {
          for (int state = 0; state < AGENT_STATE_COUNT; state++) {
            sign_frame_t a = scene(style, theme, orientation, language, state,
                                   TERMINAL_TMUX, false);
            sign_frame_t b = scene(style, theme, orientation, language, state,
                                   TERMINAL_TMUX, true);
            sign_frame_t c = scene(style, theme, orientation, language, state,
                                   TERMINAL_WEZTERM, true);
            TEST_ASSERT(memcmp(&a, &c, sizeof(a)) == 0);
            TEST_ASSERT(a.shape_count == b.shape_count &&
                        a.text_count == b.text_count && b.text_count == 1);
            TEST_ASSERT(!strstr(a.texts[0].meta, "Detached"));
            TEST_ASSERT(!strstr(a.texts[0].meta, "已断开"));
            TEST_ASSERT(strstr(b.texts[0].meta,
                               language ? " · Detached" : " · 已断开"));
            alpha(a.texts[0].color, b.texts[0].color);
            alpha(a.texts[0].meta_color, b.texts[0].meta_color);
            if (style == SIGN_STYLE_FAN)
              alpha(a.texts[0].back, b.texts[0].back);
            // The post's shared pole is not owned by an individual row.
            for (int i = style == SIGN_STYLE_POST ? 2 : 0; i < a.shape_count;
                 i++) {
              alpha(a.shapes[i].fill, b.shapes[i].fill);
              alpha(a.shapes[i].outline, b.shapes[i].outline);
              TEST_ASSERT(a.shapes[i].y == b.shapes[i].y);
              if (style == SIGN_STYLE_FAN)
                TEST_ASSERT(a.shapes[i].x == b.shapes[i].x);
            }
            if (style == SIGN_STYLE_FAN)
              TEST_ASSERT(memcmp(a.hits, b.hits, sizeof(a.hits)) == 0);
            else {
              TEST_ASSERT(b.hits[0].w >= a.hits[0].w);
              TEST_ASSERT(b.hits[0].x + b.hits[0].w ==
                          a.hits[0].x + a.hits[0].w);
              TEST_ASSERT(b.hits[0].key == a.hits[0].key &&
                          b.hits[0].y == a.hits[0].y);
            }
            uint8_t p[640 * 640 * 4] = {0};
            uint8_t q[640 * 640 * 4] = {0};
            sign_draw(p, 640, 640, 120, &a, SIGN_DRAW_UNDER);
            sign_draw(p, 640, 640, 120, &a, SIGN_DRAW_OVER);
            sign_draw(q, 640, 640, 120, &c, SIGN_DRAW_UNDER);
            sign_draw(q, 640, 640, 120, &c, SIGN_DRAW_OVER);
            TEST_ASSERT(memcmp(p, q, sizeof(p)) == 0);
          }
        }
      }
    }
  }
  text_cleanup();
  return 0;
}

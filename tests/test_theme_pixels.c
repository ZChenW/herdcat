// Frozen before palette extraction. Text is omitted to make raster hashes
// independent of fonts installed on the test host.
#include "graphics/font_panel.h"
#include "graphics/sign_draw.h"
#include "graphics/signs.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <inttypes.h>
#include <string.h>

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

// Freeze the old board geometry with deterministic measured content. These
// shape-only goldens exercise rasterization, independently of installed fonts
// and the new content-dependent layout (covered in test_stage23).
int __wrap_text_measure(const char *text, float px, bool bold);
int __wrap_text_measure(const char *text, float px, bool bold) {
  (void)px;
  if (!*text)
    return 0;
  return bold ? 80 : 108;  // 41px icon/padding + 7px gap + 80 + 108 = 236.
}
#define W 800
#define H 520
static uint8_t pixels[W * H * 4];
static uint64_t hash_pixels(const uint8_t *data, size_t n) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < n; i++)
    hash = (hash ^ data[i]) * UINT64_C(1099511628211);
  return hash;
}
static uint64_t scene(int which, int scale, sign_theme_t theme) {
  signs_t model = {0};
  agent_session_view_t sessions[5] = {0};
  for (int i = 0; i < 5; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = (agent_state_t)i;
    sessions[i].unread = i >= AGENT_STATE_DONE;
    strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
    strcpy(sessions[i].name, "project");
  }
  sign_input_t in = {.sessions = sessions,
                     .count = 5,
                     .style = which == 0 ? SIGN_STYLE_POST : SIGN_STYLE_FAN,
                     .theme = theme,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .open = true,
                     .cat_x = 210,
                     .cat_y = 210,
                     .cat_height = 110,
                     .now_ms = 100000,
                     .menu = which == 2,
                     .menu_font_hot = true,
                     .typing = which == 3,
                     .typing_key = 2,
                     .typing_until = 900000,
                     .desk_snap = true};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  frame.text_count = 0;
  memset(pixels, 0, sizeof(pixels));
  sign_draw(pixels, W, H, scale, &frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, W, H, scale, &frame, SIGN_DRAW_OVER);
  // Freeze the first three menu tracks, independently of outer card height.
  if (which == 2) {
    uint64_t hash = UINT64_C(14695981039346656037);
    int x = frame.menu_style[0].x * scale / 120;
    int y = frame.menu_style[0].y * scale / 120;
    int width = 130 * scale / 120, height = 106 * scale / 120;
    for (int row = y; row < y + height; row++)
      for (int col = x * 4; col < (x + width) * 4; col++)
        hash = (hash ^ pixels[row * W * 4 + col]) * UINT64_C(1099511628211);
    return hash;
  }
  return hash_pixels(pixels, sizeof(pixels));
}
int main(void) {
  static const uint64_t expected[4][2] = {
      {UINT64_C(0x30ec85e190618dc8), UINT64_C(0xb4324404ffb2f4fb)},
      {UINT64_C(0x2b28efc8d9b12e9c), UINT64_C(0x0849ce61f21f96f8)},
      {UINT64_C(0x559466fb69fe25c8), UINT64_C(0x59be1c10b5127a0c)},
      {UINT64_C(0x11f0e3c6d0287cba), UINT64_C(0x02ad31635d7780f0)}
  };
  for (int scene_id = 0; scene_id < 4; scene_id++) {
    for (int s = 0; s < 2; s++) {
      uint64_t got = scene(scene_id, s ? 180 : 120, SIGN_THEME_LIGHT);
      printf("%d %d %016" PRIx64 "\n", scene_id, s, got);
      TEST_ASSERT(got == expected[scene_id][s]);
      uint64_t dark = scene(scene_id, s ? 180 : 120, SIGN_THEME_DARK);
      TEST_ASSERT(dark != got);
      TEST_ASSERT(scene(scene_id, s ? 180 : 120, SIGN_THEME_LIGHT) == got);
    }
  }
  font_panel_t panel;
  font_panel_reset(&panel);
  font_panel_open(&panel, true, 1, SIGN_ANIM_OFF, "", 0);
  memset(pixels, 0, sizeof(pixels));
  font_panel_draw(&panel, pixels, W, H, 120);
  TEST_ASSERT(hash_pixels(pixels, sizeof(pixels)) ==
              UINT64_C(0x024920bbacb79ded));
  font_panel_set_theme(&panel, SIGN_THEME_DARK);
  TEST_ASSERT(font_panel_step(&panel, 0).redraw);
  memset(pixels, 0, sizeof(pixels));
  font_panel_draw(&panel, pixels, W, H, 120);
  TEST_ASSERT(hash_pixels(pixels, sizeof(pixels)) !=
              UINT64_C(0x024920bbacb79ded));
  font_panel_set_theme(&panel, SIGN_THEME_LIGHT);
  memset(pixels, 0, sizeof(pixels));
  font_panel_draw(&panel, pixels, W, H, 120);
  TEST_ASSERT(hash_pixels(pixels, sizeof(pixels)) ==
              UINT64_C(0x024920bbacb79ded));
  for (int i = 0; i < 240; i++)
    (void)scene(i % 2, 120 + i % 3 * 60,
                i % 4 < 2 ? SIGN_THEME_LIGHT : SIGN_THEME_DARK);
  sign_draw_cache_stats_t stats = sign_draw_cache_stats();
  TEST_ASSERT(stats.entries <= stats.slot_limit);
  TEST_ASSERT(stats.bytes <= stats.byte_limit);
  // A light redraw after cache churn still has exactly the frozen pixels.
  TEST_ASSERT(scene(0, 120, SIGN_THEME_LIGHT) == expected[0][0]);
  sign_draw_cleanup();
  return 0;
}

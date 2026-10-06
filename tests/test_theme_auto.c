#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "test_helpers.h"

#include <string.h>

int main(void) {
  agent_session_view_t session = {
      .key = 1, .pid = 42, .state = AGENT_STATE_WAITING};
  strcpy(session.name, "project");
  sign_input_t in = {.sessions = &session,
                     .count = 1,
                     .style = SIGN_STYLE_FAN,
                     .idle = SIGN_IDLE_ALWAYS,
                     .open = true,
                     .animations = SIGN_ANIM_OFF,
                     .cat_x = 100,
                     .cat_y = 200,
                     .cat_height = 110};
  for (int dark = 0; dark < 2; dark++) {
    sign_theme_t theme = dark ? SIGN_THEME_DARK : SIGN_THEME_LIGHT;
    sign_theme_system(theme);
    TEST_ASSERT(sign_theme_effective(SIGN_THEME_AUTO) == theme);
    TEST_ASSERT(sign_theme_effective(SIGN_THEME_LIGHT) == SIGN_THEME_LIGHT);
    TEST_ASSERT(sign_theme_effective(SIGN_THEME_DARK) == SIGN_THEME_DARK);
    signs_t manual = {0}, automatic = {0};
    sign_frame_t expected, actual;
    in.theme = theme;
    in.theme_auto = false;
    signs_frame(&manual, &in, &expected);
    in.theme = sign_theme_effective(SIGN_THEME_AUTO);
    in.theme_auto = true;
    signs_frame(&automatic, &in, &actual);
    TEST_ASSERT(!memcmp(&expected, &actual, sizeof(actual)));
    in.menu = true;
    signs_frame(&automatic, &in, &actual);
    TEST_ASSERT(actual.menu_theme[1].x == 177);
    TEST_ASSERT(actual.menu_theme[2].x == 220);
    TEST_ASSERT(actual.menu_theme_thumb.x == 178 &&
                actual.menu_theme_thumb.w == 42);
    in.menu = false;
  }
  // A live automatic card keeps the middle selection through portal changes.
  signs_t model = {0};
  sign_frame_t frame;
  in.theme_auto = true;
  in.menu = true;
  in.animations = SIGN_ANIM_FULL;
  in.now_ms = 1000;
  in.theme = SIGN_THEME_LIGHT;
  signs_frame(&model, &in, &frame);
  in.now_ms = 2000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.transitioning && frame.menu_theme_thumb.x == 178);
  in.theme = SIGN_THEME_DARK;
  in.now_ms = 2100;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.transitioning && frame.menu_theme_thumb.x == 178);
  sign_theme_system(SIGN_THEME_LIGHT);
  puts("Automatic theme rendering and menu selection passed");
  return 0;
}

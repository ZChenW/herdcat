#include "graphics/sign_palette.h"
#include "graphics/signs.h"
#include "test_helpers.h"

#include <math.h>
#include <string.h>

static double channel(uint32_t color, int shift) {
  double s = ((color >> shift) & 255) / 255.0;
  return s <= .04045 ? s / 12.92 : pow((s + .055) / 1.055, 2.4);
}
static double luminance(uint32_t color) {
  return .2126 * channel(color, 16) + .7152 * channel(color, 8) +
         .0722 * channel(color, 0);
}
static double contrast(uint32_t a, uint32_t b) {
  double x = luminance(a), y = luminance(b);
  return (fmax(x, y) + .05) / (fmin(x, y) + .05);
}
static void pair(uint32_t light, uint32_t dark) {
  TEST_ASSERT(light >> 24 == 255 && dark >> 24 == 255);
  TEST_ASSERT(light != dark);
}
static void palettes(void) {
  const sign_palette_t *light = sign_palette(SIGN_THEME_LIGHT);
  const sign_palette_t *dark = sign_palette(SIGN_THEME_DARK);
  const uint32_t fills[] = {0xff232a39, 0xff1e3a5f, 0xff5a4410, 0xff1d4a33,
                            0xff5c2a22};
  const uint32_t icons[] = {0xff8b93a1, 0xff8ec5ff, 0xffffd166, 0xff7ee2a8,
                            0xffff9a8a};
  TEST_ASSERT(dark->ink == 0xffe6e9ef && dark->paper == 0xff1c2230);
  TEST_ASSERT(dark->secondary == 0xffa9b1c0 && dark->hover == 0xff2d3648);
  TEST_ASSERT(dark->count == 0xffa9b1c0 && dark->plate == 0xff5a4410);
  TEST_ASSERT(dark->meta == 0xffffd166);
  pair(light->ink, dark->ink);
  pair(light->paper, dark->paper);
  pair(light->secondary, dark->secondary);
  pair(light->hover, dark->hover);
  pair(light->count, dark->count);
  pair(light->plate, dark->plate);
  pair(light->meta, dark->meta);
  for (int i = 0; i < AGENT_STATE_COUNT; i++) {
    TEST_ASSERT(dark->fills[i] == fills[i] && dark->icons[i] == icons[i]);
    pair(light->fills[i], dark->fills[i]);
    if (i == AGENT_STATE_IDLE) {
      TEST_ASSERT(light->icons[i] == dark->icons[i]);
      TEST_ASSERT(dark->icons[i] >> 24 == 255);
    } else {
      pair(light->icons[i], dark->icons[i]);
    }
    uint32_t meta = i == AGENT_STATE_WAITING || i == AGENT_STATE_ERROR
                        ? dark->icons[i]
                        : dark->secondary;
    TEST_ASSERT(contrast(dark->ink, dark->fills[i]) >= 4.5);
    TEST_ASSERT(contrast(meta, dark->fills[i]) >= 4.5);
    printf("state %d: name %.3f meta %.3f\n", i,
           contrast(dark->ink, dark->fills[i]), contrast(meta, dark->fills[i]));
  }
}
static void model_theme(void) {
  const sign_palette_t *dark = sign_palette(SIGN_THEME_DARK);
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    for (int state = 0; state < AGENT_STATE_COUNT; state++) {
      agent_session_view_t session = {
          .key = 1, .state = (agent_state_t)state, .unread = true};
      strcpy(session.agent, "claude");
      strcpy(session.name, "project");
      signs_t model = {0};
      sign_frame_t frame;
      sign_input_t in = {.sessions = &session,
                         .count = 1,
                         .style = (sign_style_t)style,
                         .theme = SIGN_THEME_DARK,
                         .animations = SIGN_ANIM_OFF,
                         .idle = SIGN_IDLE_ALWAYS,
                         .open = true,
                         .has_hover = true,
                         .hover_key = 1,
                         .cat_height = 110,
                         .cat_y = 200,
                         .now_ms = 5000};
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.text_count == 1);
      TEST_ASSERT(frame.texts[0].color == dark->ink);
      TEST_ASSERT(frame.texts[0].meta_color ==
                  (style == SIGN_STYLE_FAN && (state == AGENT_STATE_WAITING ||
                                               state == AGENT_STATE_ERROR)
                       ? dark->icons[state]
                       : dark->secondary));
      bool fill = false, icon = false;
      for (int j = 0; j < frame.shape_count; j++) {
        fill |= frame.shapes[j].fill == dark->fills[state];
        icon |= (frame.shapes[j].fill >> 24 &&
                 (frame.shapes[j].fill & 0xffffff) ==
                     (dark->icons[state] & 0xffffff)) ||
                frame.shapes[j].outline == dark->icons[state];
      }
      TEST_ASSERT(fill && icon);
    }
  }
}
int main(void) {
  palettes();
  model_theme();
  return 0;
}

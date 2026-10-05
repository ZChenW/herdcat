#define _POSIX_C_SOURCE 200809L
#include "core/agent_sessions.h"
#include "graphics/text.h"
#include "platform/drag.h"
#include "platform/font_panel.h"
#include "platform/overlay_signs.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static config_t config_of(int cat_height, int bar, int y_offset) {
  config_t config;
  memset(&config, 0, sizeof(config));
  config.sign_style = SIGN_STYLE_POST;
  config.sign_max = 5;
  config.sign_font_size = 13;
  config.sign_typing_desk = 1;
  config.cat_height = cat_height;
  config.overlay_height = bar;
  config.cat_y_offset = y_offset;
  config.overlay_position = POSITION_BOTTOM;
  return config;
}
static void begin(void) {
  agent_sessions_reset();
  overlay_signs_cleanup();
}
static void geometry(void) {
  config_t config = config_of(110, 50, 10);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(config.sign_style == SIGN_STYLE_POST);
  TEST_ASSERT(height == 218);
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 104);
  config.cat_y_offset = -20;
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 84);
  config.cat_y_offset = -1000;
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 0);
  config_t room = config_of(110, 120, 0);
  int tall = overlay_signs_height(&room);
  TEST_ASSERT(tall == 284);
  TEST_ASSERT(overlay_signs_cat_y(&room, tall) == 170);
  config_t tight = config_of(110, 110, 0);
  int grown = overlay_signs_height(&tight);
  TEST_ASSERT(grown == 278);
  TEST_ASSERT(overlay_signs_cat_y(&tight, grown) == 164);
  config_t fan = config_of(110, 110, 0);
  fan.sign_style = SIGN_STYLE_FAN;
  int fan_height = overlay_signs_height(&fan);
  TEST_ASSERT(fan_height == 256);
  TEST_ASSERT(overlay_signs_cat_y(&fan, fan_height) == 142);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_FAN, 110) == 142);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_FAN, 110) - 136 >= 6);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_POST, 110) - 136 >= 6);
  config_t raised = config_of(110, 120, 0);
  raised.sign_style = SIGN_STYLE_FAN;
  TEST_ASSERT(overlay_signs_height(&raised) == 262);
  TEST_ASSERT(overlay_signs_cat_y(&raised, 262) == 148);
  int x = -4, y = -8;
  drag_clamp(&x, &y, 1000, 200, 800, fan_height);
  TEST_ASSERT(x == 0 && y == 0);
  x = 5000;
  y = 5000;
  drag_clamp(&x, &y, 1000, 200, 800, fan_height);
  TEST_ASSERT(x == 800 && y == 800 - fan_height);
}
static void quiet_pole(void) {
  begin();
  config_t config = config_of(40, 50, 10);
  int height = overlay_signs_height(&config);
  overlay_signs_step_t first =
      overlay_signs_step(0, &config, 10, 72, height, false, 0);
  TEST_ASSERT(first.redraw);
  TEST_ASSERT(!first.frame);
  TEST_ASSERT(first.timeout_ms < 0);
  TEST_ASSERT(first.damage_full);
  overlay_signs_step_t second =
      overlay_signs_step(0, &config, 10, 72, height, false, 50);
  TEST_ASSERT(!second.redraw);
  TEST_ASSERT(!second.frame);
  TEST_ASSERT(second.timeout_ms < 0);
  TEST_ASSERT(!second.damage_full);
}
static void working_dots(void) {
  begin();
  config_t config = config_of(40, 50, 0);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x11, "claude", AGENT_EVENT_WORKING, 42, 0,
                                   0, NULL) == 0);
  overlay_signs_step_t rising =
      overlay_signs_step(0, &config, 10, 72, height, false, 0);
  TEST_ASSERT(rising.frame);
  TEST_ASSERT(rising.timeout_ms < 0);
  overlay_signs_step_t dots =
      overlay_signs_step(0, &config, 10, 72, height, false, 420);
  TEST_ASSERT(!dots.frame);
  TEST_ASSERT(dots.timeout_ms > 0 && dots.timeout_ms <= 180);
}
static bool has_key(const sign_frame_t *frame, uint64_t key) {
  for (int i = 0; i < frame->hit_count; i++) {
    if (frame->hits[i].key == key && frame->hits[i].w > 0)
      return true;
  }
  return false;
}
static void open_and_close(void) {
  begin();
  config_t config = config_of(40, 50, 10);
  int height = overlay_signs_height(&config);
  int cat_y = overlay_signs_cat_y(&config, height);
  TEST_ASSERT(height == 110);
  TEST_ASSERT(cat_y == 68);
  TEST_ASSERT(agent_sessions_apply(0x21, "claude", AGENT_EVENT_DONE, 11, 1000,
                                   0, NULL) == 0);
  TEST_ASSERT(agent_sessions_apply(0x22, "codex", AGENT_EVENT_WORKING, 22, 1000,
                                   0, NULL) == 0);
  TEST_ASSERT(agent_sessions_apply(0x22, "codex", AGENT_EVENT_IDLE, 22, 1000, 0,
                                   NULL) == 0);
  overlay_signs_step(0, &config, 100, 72, height, false, 1000);
  overlay_signs_step(0, &config, 100, 72, height, false, 1500);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->hit_count == 1 && frame->hits[0].key == 0x21);

  overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
  int regions = overlay_signs_regions(0, &config, 100, 72, height, rects,
                                      OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(regions == 2);

  TEST_ASSERT(!overlay_signs_pointer(0, 102, cat_y + 38));
  overlay_signs_step(0, &config, 100, 72, height, false, 2000);
  overlay_signs_step_t opening =
      overlay_signs_step(0, &config, 100, 72, height, false, 2030);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->hit_count == 2);
  TEST_ASSERT(has_key(frame, 0x21) && has_key(frame, 0x22));
  TEST_ASSERT(opening.frame);
  regions = overlay_signs_regions(0, &config, 100, 72, height, rects,
                                  OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(regions == 4);

  overlay_signs_leave();
  overlay_signs_step_t waiting =
      overlay_signs_step(0, &config, 100, 72, height, false, 2030);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->hit_count == 2);
  TEST_ASSERT(waiting.timeout_ms == 150);
  overlay_signs_step(0, &config, 100, 72, height, false, 2180);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->hit_count == 1 && frame->hits[0].key == 0x21);
}
static void click(void) {
  begin();
  config_t config = config_of(40, 50, 0);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x31, "claude", AGENT_EVENT_DONE, 42, 0, 0,
                                   NULL) == 0);
  overlay_signs_step(0, &config, 10, 72, height, false, 0);
  overlay_signs_step(0, &config, 10, 72, height, false, 500);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->hit_count == 1);
  double x = frame->hits[0].x + frame->hits[0].w / 2.0;
  double y = frame->hits[0].y + frame->hits[0].h / 2.0;
  TEST_ASSERT(overlay_signs_pointer(0, x, y));
  TEST_ASSERT(overlay_signs_press(0));
  size_t index = 9;
  pid_t pid = 0;
  uint64_t key = 0;
  TEST_ASSERT(overlay_signs_release(false, &index, &pid, &key));
  TEST_ASSERT(index == 0 && pid == 42 && key == 0x31);
  TEST_ASSERT(overlay_signs_pointer(0, x, y));
  TEST_ASSERT(overlay_signs_press(0));
  TEST_ASSERT(!overlay_signs_release(true, &index, &pid, &key));
}
static void focus_shake(void) {
  begin();
  config_t config = config_of(40, 50, 0);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x41, "claude", AGENT_EVENT_DONE, 42, 0, 0,
                                   NULL) == 0);
  overlay_signs_step(0, &config, 10, 72, height, false, 0);
  overlay_signs_step_t settled =
      overlay_signs_step(0, &config, 10, 72, height, false, 500);
  TEST_ASSERT(!settled.frame);
  overlay_signs_arm_focus(0, 0x41);
  overlay_signs_note_focus(FOCUS_SUCCESS, 500);
  settled = overlay_signs_step(0, &config, 10, 72, height, false, 500);
  TEST_ASSERT(!settled.frame);
  overlay_signs_arm_focus(0, 0x41);
  overlay_signs_note_focus(FOCUS_PENDING, 500);
  overlay_signs_note_focus(FOCUS_NOT_FOUND, 500);
  overlay_signs_step_t shake =
      overlay_signs_step(0, &config, 10, 72, height, false, 500);
  TEST_ASSERT(shake.frame);
  overlay_signs_fail(0, 0x41, 900);
  shake = overlay_signs_step(0, &config, 10, 72, height, false, 900);
  TEST_ASSERT(shake.frame);
}
static void hidden_is_quiet(void) {
  begin();
  config_t config = config_of(40, 50, 0);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x51, "claude", AGENT_EVENT_WORKING, 42, 0,
                                   0, NULL) == 0);
  overlay_signs_step_t live =
      overlay_signs_step(0, &config, 10, 72, height, false, 0);
  TEST_ASSERT(live.frame);
  overlay_signs_step_t hidden =
      overlay_signs_step(0, &config, 10, 72, height, true, 10);
  TEST_ASSERT(!hidden.redraw);
  TEST_ASSERT(!hidden.frame);
  TEST_ASSERT(hidden.timeout_ms < 0);
}
static void damage_follows_cat(void) {
  begin();
  config_t config = config_of(40, 50, 0);
  int height = overlay_signs_height(&config);
  int cat_y = overlay_signs_cat_y(&config, height);
  overlay_signs_step(0, &config, 10, 72, height, false, 0);
  overlay_signs_step_t moved =
      overlay_signs_step(0, &config, 80, 72, height, false, 50);
  TEST_ASSERT(moved.redraw);
  TEST_ASSERT(!moved.damage_full);
  TEST_ASSERT(moved.damage_x <= 10);
  TEST_ASSERT(moved.damage_x + moved.damage_w >= 80 + 72);
  TEST_ASSERT(moved.damage_y <= cat_y);
  TEST_ASSERT(moved.damage_y + moved.damage_h >= cat_y + 40);
}
static bool wide_board(const sign_frame_t *frame) {
  if (!frame)
    return false;
  for (int i = 0; i < frame->shape_count; i++)
    if (frame->shapes[i].w > 100 && frame->shapes[i].h > 20 &&
        frame->shapes[i].h < 40)
      return true;
  return false;
}
static void typing_desk(void) {
  begin();
  config_t config = config_of(110, 120, 0);
  int height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x61, "claude", AGENT_EVENT_WORKING, 42, 0,
                                   0, NULL) == 0);
  overlay_signs_note_key();
  overlay_signs_step(0, &config, 10, 80, height, false, 0);
  TEST_ASSERT(!wide_board(overlay_signs_frame(0)));
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  overlay_signs_type_at(0x61, 1000);
  overlay_signs_step(0, &config, 10, 80, height, false, 1000);
  overlay_signs_step(0, &config, 10, 80, height, false, 1500);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->cat_lift == 8);
  TEST_ASSERT(wide_board(frame));
  TEST_ASSERT(!has_key(frame, 0x61));
  overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
  int regions = overlay_signs_regions(0, &config, 10, 80, height, rects,
                                      OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(regions == 1);
  overlay_signs_note_working(0x61);
  overlay_signs_step(0, &config, 10, 80, height, false, 1600);
  overlay_signs_step(0, &config, 10, 80, height, false, 2000);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  TEST_ASSERT(!wide_board(overlay_signs_frame(0)));
  overlay_signs_type_at(0x61, 3000);
  overlay_signs_step(0, &config, 10, 80, height, false, 3000);
  overlay_signs_step(0, &config, 10, 80, height, false, 3400);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 8);
  overlay_signs_sync_focus(0);
  overlay_signs_step(0, &config, 10, 80, height, false, 3400);
  overlay_signs_step(0, &config, 10, 80, height, false, 3800);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  overlay_signs_type_at(0x61, 5000);
  overlay_signs_step(0, &config, 10, 80, height, false, 5000);
  overlay_signs_step(0, &config, 10, 80, height, false, 5400);
  overlay_signs_step(0, &config, 10, 80, height, true, 5400);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  overlay_signs_type_at(0x61, 6000);
  overlay_signs_step(0, &config, 10, 80, height, false, 6000);
  overlay_signs_step(0, &config, 10, 80, height, false, 6400);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 8);
  overlay_signs_step(0, &config, 10, 80, height, false, 8500);
  overlay_signs_step(0, &config, 10, 80, height, false, 8900);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  TEST_ASSERT(!wide_board(overlay_signs_frame(0)));
}
static void live_options(void) {
  begin();
  config_t config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  config.sign_language = SIGN_LANGUAGE_EN;
  for (int i = 1; i <= 6; i++)
    TEST_ASSERT(agent_sessions_apply((uint64_t)i, "claude", AGENT_EVENT_START,
                                     0, i, 5, NULL) == 0);
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    config.sign_idle = SIGN_IDLE_ALWAYS;
    for (int max = 1; max <= 5; max++) {
      config.sign_max = max;
      overlay_signs_step(0, &config, 100, 199, 300, false, 1000);
      TEST_ASSERT(overlay_signs_frame(0)->hit_count == max);
    }
    config.sign_idle = SIGN_IDLE_NEVER;
    overlay_signs_pointer(0, 102, 295);
    overlay_signs_step(0, &config, 100, 199, 300, false, 1000);
    TEST_ASSERT(overlay_signs_frame(0)->hit_count == 0);
    config.sign_idle = SIGN_IDLE_HOVER;
    overlay_signs_step(0, &config, 100, 199, 300, false, 1000);
    TEST_ASSERT(overlay_signs_frame(0)->hit_count == 5);
    config.sign_typing_desk = 1;
    overlay_signs_type_at(1, 1000);
    overlay_signs_step(0, &config, 100, 199, 300, false, 1000);
    TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 8);
    config.sign_typing_desk = 0;
    overlay_signs_step(0, &config, 100, 199, 300, false, 1000);
    TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  }
  config.sign_style = SIGN_STYLE_OFF;
  TEST_ASSERT(overlay_signs_height(&config) == config.overlay_height);
  overlay_signs_step_t off =
      overlay_signs_step(0, &config, 100, 199, 120, false, 2000);
  TEST_ASSERT(!off.frame && overlay_signs_frame(0)->hit_count == 0);
}
static int expands;
static void count_expand(void) {
  expands++;
}

static void expand_edge(void) {
  begin();
  expands = 0;
  overlay_signs_on_expand(count_expand);
  config_t config = config_of(40, 50, 10);
  int height = overlay_signs_height(&config);
  int cat_y = overlay_signs_cat_y(&config, height);
  TEST_ASSERT(!overlay_signs_pointer(0, 102, cat_y + 38));
  overlay_signs_step(0, &config, 100, 72, height, false, 1000);
  TEST_ASSERT(expands == 1);
  overlay_signs_step(0, &config, 100, 72, height, false, 1100);
  TEST_ASSERT(expands == 1);
  overlay_signs_leave();
  overlay_signs_step(0, &config, 100, 72, height, false, 1100);
  overlay_signs_step(0, &config, 100, 72, height, false, 1250);
  TEST_ASSERT(expands == 1);
  TEST_ASSERT(!overlay_signs_pointer(0, 102, cat_y + 38));
  overlay_signs_step(0, &config, 100, 72, height, false, 1300);
  TEST_ASSERT(expands == 2);
  overlay_signs_press(0);
  TEST_ASSERT(expands == 2);
  begin();
  expands = 0;
  overlay_signs_on_expand(count_expand);
  TEST_ASSERT(!overlay_signs_press(0));
  TEST_ASSERT(expands == 1);
  overlay_signs_press(0);
  TEST_ASSERT(expands == 1);
  begin();
}
static config_t *menu_config;
static sign_style_t menu_style;
static sign_language_t menu_language;
static unsigned menu_paw;
static int menu_style_n, menu_language_n, menu_paw_n;
static void take_style(sign_style_t style) {
  menu_style = style;
  menu_style_n++;
  if (menu_config)
    menu_config->sign_style = style;
}
static void take_language(sign_language_t language) {
  menu_language = language;
  menu_language_n++;
  if (menu_config)
    menu_config->sign_language = language;
}
static void take_paw(unsigned paw) {
  menu_paw = paw;
  menu_paw_n++;
}
static void right_click(double x, double y) {
  overlay_signs_pointer(0, x, y);
  TEST_ASSERT(overlay_signs_button(0x111, 1));
  TEST_ASSERT(overlay_signs_button(0x111, 0));
}
static bool center_inside(sign_rect_t thumb, sign_rect_t half) {
  int mid_x = thumb.x + thumb.w / 2;
  int mid_y = thumb.y + thumb.h / 2;
  return mid_x >= half.x && mid_x < half.x + half.w && mid_y >= half.y &&
         mid_y < half.y + half.h;
}
static void click_rect(sign_rect_t rect) {
  overlay_signs_pointer(0, rect.x + rect.w / 2.0, rect.y + rect.h / 2.0);
  overlay_signs_press(0);
  overlay_signs_release(false, NULL, NULL, NULL);
}
static void switch_card(void) {
  begin();
  config_t config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_OFF;
  config.sign_animations = SIGN_ANIM_OFF;
  int height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  int cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);

  begin();
  setenv("LANG", "zh_CN.UTF-8", 1);
  unsetenv("LC_MESSAGES");
  config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_AUTO;
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open);
  TEST_ASSERT(center_inside(frame->menu_lang_thumb, frame->menu_lang[0]));
  TEST_ASSERT(!center_inside(frame->menu_lang_thumb, frame->menu_lang[1]));

  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  config.sign_language = SIGN_LANGUAGE_EN;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  TEST_ASSERT(!overlay_signs_button(0x110, 1));
  overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  right_click(120, cat_y + 20);
  overlay_signs_step_t opened =
      overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(opened.timeout_ms == 6000);
  right_click(120, cat_y + 20);
  overlay_signs_step_t closed =
      overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(closed.timeout_ms < 0);

  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  overlay_signs_leave();
  overlay_signs_step_t armed =
      overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(armed.timeout_ms > 0 && armed.timeout_ms <= 800);
  overlay_signs_step(0, &config, 100, 199, height, false, 1799);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_step_t left =
      overlay_signs_step(0, &config, 100, 199, height, false, 1800);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(left.timeout_ms < 0);

  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  overlay_signs_step(0, &config, 100, 199, height, false, 6999);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 7000);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  overlay_signs_pointer(0, 120, cat_y + 30);
  overlay_signs_step(0, &config, 100, 199, height, false, 4000);
  overlay_signs_step(0, &config, 100, 199, height, false, 7000);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 9999);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 10000);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);

  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_FULL;
  height = overlay_signs_height(&config);
  TEST_ASSERT(agent_sessions_apply(0x71, "claude", AGENT_EVENT_WORKING, 42, 0,
                                   0, NULL) == 0);
  overlay_signs_type_at(0x71, 1000);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  overlay_signs_step(0, &config, 100, 199, height, false, 1500);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 8);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(150, cat_y + 40);
  overlay_signs_step(0, &config, 100, 199, height, false, 1600);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->cat_lift == 0 && frame->hit_count == 0);
  right_click(150, cat_y + 40);
  overlay_signs_step(0, &config, 100, 199, height, false, 1600);
  overlay_signs_step(0, &config, 100, 199, height, false, 2000);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 8);

  begin();
  menu_config = &config;
  menu_style_n = menu_language_n = menu_paw_n = 0;
  overlay_signs_on_menu(take_style, take_language, take_paw, NULL);
  config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_EN;
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  frame = overlay_signs_frame(0);
  click_rect(frame->menu_style[1]);
  overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(menu_style_n == 1 && menu_style == SIGN_STYLE_POST);
  TEST_ASSERT(menu_paw_n == 1 && menu_paw == 1 && frame->menu_paw == 1);
  TEST_ASSERT(!frame->menu_open && config.sign_style == SIGN_STYLE_POST);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1200);
  frame = overlay_signs_frame(0);
  click_rect(frame->menu_style[1]);
  overlay_signs_step(0, &config, 100, 199, height, false, 1300);
  TEST_ASSERT(menu_style_n == 1 && menu_paw_n == 1);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  frame = overlay_signs_frame(0);
  click_rect(frame->menu_lang[0]);
  overlay_signs_step(0, &config, 100, 199, height, false, 1400);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(menu_language_n == 1 && menu_language == SIGN_LANGUAGE_ZH);
  TEST_ASSERT(menu_paw_n == 2 && menu_paw == 2 && frame->menu_paw == 2);
  TEST_ASSERT(frame->menu_open);
  TEST_ASSERT(config.sign_language == SIGN_LANGUAGE_ZH);
  click_rect(frame->menu_lang[0]);
  overlay_signs_step(0, &config, 100, 199, height, false, 1500);
  TEST_ASSERT(menu_language_n == 1 && menu_paw_n == 2);
  menu_config = NULL;

  begin();
  config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_EN;
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  frame = overlay_signs_frame(0);
  overlay_signs_pointer(0, frame->menu_card.x + 4, frame->menu_card.y + 4);
  TEST_ASSERT(!overlay_signs_press(0));
  TEST_ASSERT(overlay_signs_blocks_drag());
  overlay_signs_release(false, NULL, NULL, NULL);
  TEST_ASSERT(!overlay_signs_blocks_drag());
  overlay_signs_pointer(0, 120, cat_y + 20);
  TEST_ASSERT(!overlay_signs_press(0));
  TEST_ASSERT(!overlay_signs_blocks_drag());
  overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
  int regions = overlay_signs_regions(0, &config, 100, 199, height, rects,
                                      OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(regions == 2);

  begin();
  config = config_of(110, 120, 0);
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, true, 1100);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 1200);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);

  begin();
  overlay_signs_on_menu(NULL, NULL, NULL, NULL);
  config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_EN;
  config.sign_animations = SIGN_ANIM_OFF;
  height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  click_rect(overlay_signs_frame(0)->menu_lang[0]);
  overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(center_inside(frame->menu_lang_thumb, frame->menu_lang[0]));
  overlay_signs_use_config();
  overlay_signs_step(0, &config, 100, 199, height, false, 1200);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(center_inside(frame->menu_lang_thumb, frame->menu_lang[1]));
}
static char live_font[128], saved_font[128];
static int live_n, save_n;
static void take_font(const char *family, bool save) {
  if (save) {
    save_n++;
    snprintf(saved_font, sizeof(saved_font), "%s", family ? family : "");
  } else {
    live_n++;
    snprintf(live_font, sizeof(live_font), "%s", family ? family : "");
    text_set_family(live_font);
  }
}
static const sign_text_t *shown_text(const sign_frame_t *frame,
                                     const char *value) {
  for (int i = 0; i < frame->text_count; i++)
    if (!strcmp(frame->texts[i].value, value))
      return &frame->texts[i];
  return NULL;
}
static void font_row(void) {
  begin();
  TEST_ASSERT(text_init("Noto Sans") == 0);
  const char *families[8];
  int listed = text_families("en", families, 8);
  TEST_ASSERT(listed > 0);
  config_t config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_EN;
  config.sign_animations = SIGN_ANIM_OFF;
  live_n = save_n = 0;
  live_font[0] = saved_font[0] = '\0';
  overlay_signs_on_menu(NULL, NULL, NULL, take_font);
  int height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  int cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open && frame->menu_card.h == 130);
  TEST_ASSERT(frame->menu_card.y == cat_y - 136);
  TEST_ASSERT(frame->menu_font.w == 130 && frame->menu_font.h == 30);
  TEST_ASSERT(frame->menu_font_prev.w == 26 && frame->menu_font_next.w == 26);
  TEST_ASSERT(frame->menu_font_prev.x == frame->menu_font.x);
  TEST_ASSERT(frame->menu_font_next.x + 26 == frame->menu_font.x + 130);
  TEST_ASSERT(shown_text(frame, "Default"));
  overlay_signs_pointer(0, frame->menu_style[0].x + 4,
                        frame->menu_style[0].y + 4);
  overlay_signs_scroll(1);
  overlay_signs_step(0, &config, 100, 199, height, false, 1050);
  TEST_ASSERT(live_n == 0 && save_n == 0);
  overlay_signs_pointer(0, frame->menu_font.x + frame->menu_font.w / 2.0,
                        frame->menu_font.y + frame->menu_font.h / 2.0);
  overlay_signs_scroll(0);
  TEST_ASSERT(live_n == 0);
  overlay_signs_scroll(1);
  TEST_ASSERT(live_n == 1 && save_n == 0);
  TEST_ASSERT(!strcmp(live_font, families[0]));
  overlay_signs_step_t armed =
      overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  TEST_ASSERT(save_n == 0 && armed.timeout_ms == 500);
  frame = overlay_signs_frame(0);
  const sign_text_t *name = shown_text(frame, families[0]);
  TEST_ASSERT(name && !strcmp(name->family, families[0]));
  TEST_ASSERT(frame->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 1599);
  TEST_ASSERT(save_n == 0 && overlay_signs_frame(0)->menu_open);
  overlay_signs_step(0, &config, 100, 199, height, false, 1600);
  TEST_ASSERT(save_n == 1 && !strcmp(saved_font, families[0]));
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_scroll(1);
  overlay_signs_step(0, &config, 100, 199, height, false, 1700);
  TEST_ASSERT(save_n == 1);
  overlay_signs_scroll(1);
  overlay_signs_step(0, &config, 100, 199, height, false, 2000);
  TEST_ASSERT(save_n == 1);
  overlay_signs_step(0, &config, 100, 199, height, false, 2499);
  TEST_ASSERT(save_n == 1);
  overlay_signs_step(0, &config, 100, 199, height, false, 2500);
  TEST_ASSERT(save_n == 2);
  int before = live_n;
  overlay_signs_scroll(20);
  TEST_ASSERT(live_n == before + 8);
  overlay_signs_scroll(-9);
  TEST_ASSERT(live_n == before + 16);
  before = save_n;
  overlay_signs_scroll(1);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 2600);
  TEST_ASSERT(save_n == before + 1);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 2700);
  frame = overlay_signs_frame(0);
  int paws = menu_paw_n;
  int was_live = live_n;
  click_rect(frame->menu_font_next);
  overlay_signs_step(0, &config, 100, 199, height, false, 2800);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open && menu_paw_n == paws);
  TEST_ASSERT(live_n == was_live + 1);
  overlay_signs_scroll(1);
  overlay_signs_step(0, &config, 100, 199, height, false, 2900);
  int pending = save_n;
  overlay_signs_use_config();
  overlay_signs_step(0, &config, 100, 199, height, false, 4000);
  TEST_ASSERT(save_n == pending);
  TEST_ASSERT(shown_text(overlay_signs_frame(0), "Default"));
  TEST_ASSERT(text_glyph_count() <= 512);
  text_cleanup();
}
static bool stub_open, stub_armed, stub_covers, stub_toggled;
static int stub_close_n, stub_wheel_n;
bool font_panel_surface_is_open(void) {
  return stub_open;
}
void font_panel_surface_close(void) {
  stub_close_n++;
  stub_open = false;
}
void font_panel_surface_sync(size_t index, const config_t *config,
                             font_panel_anchor_t card, int surface_h,
                             int64_t now_ms, int *timeout_ms, bool toggle) {
  (void)index;
  (void)config;
  (void)card;
  (void)surface_h;
  (void)now_ms;
  (void)timeout_ms;
  if (!toggle)
    return;
  stub_toggled = true;
  stub_open = !stub_open;
}
void font_panel_surface_activity(int64_t now_ms) {
  (void)now_ms;
}
bool font_panel_surface_covers(size_t index) {
  return stub_covers && index == 0;
}
bool font_panel_surface_armed(void) {
  return stub_armed;
}
void font_panel_surface_wheel(int discrete) {
  (void)discrete;
  stub_wheel_n++;
}
bool font_panel_surface_button(uint32_t button, uint32_t state) {
  (void)button;
  (void)state;
  return false;
}
static const char *panel_hover;
const char *font_panel_surface_hover(void) {
  return panel_hover;
}
bool font_panel_surface_take_choice(char *out, size_t cap) {
  (void)out;
  (void)cap;
  return false;
}
void font_panel_surface_left(void) {}
void font_panel_surface_select(const char *family) {
  (void)family;
}
void font_panel_surface_language(bool english) {
  (void)english;
}
static void open_card(config_t *config, int *height, int *cat_y) {
  *height = overlay_signs_height(config);
  overlay_signs_step(0, config, 100, 199, *height, false, 1000);
  *cat_y = overlay_signs_cat_y(config, *height);
  right_click(120, *cat_y + 20);
  overlay_signs_step(0, config, 100, 199, *height, false, 1000);
}
static void font_name_button(void) {
  begin();
  stub_open = stub_armed = stub_covers = stub_toggled = false;
  stub_close_n = stub_wheel_n = 0;
  config_t config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_language = SIGN_LANGUAGE_EN;
  config.sign_animations = SIGN_ANIM_OFF;
  live_n = save_n = 0;
  overlay_signs_on_menu(NULL, NULL, NULL, take_font);
  int height = 0, cat_y = 0;
  open_card(&config, &height, &cat_y);
  const sign_frame_t *frame = overlay_signs_frame(0);
  int closes = stub_close_n;
  click_rect(frame->menu_font);
  TEST_ASSERT(stub_close_n == closes);
  overlay_signs_step(0, &config, 100, 199, height, false, 1100);
  TEST_ASSERT(stub_toggled && stub_open);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(live_n == 0 && save_n == 0);
  stub_armed = true;
  overlay_signs_scroll(1);
  TEST_ASSERT(stub_wheel_n == 1 && live_n == 0);
  stub_armed = false;
  stub_open = true;
  overlay_signs_pointer(0, 120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 7000);
  // The card steps aside while the panel is open so the real signs can show
  // the face being tried. The menu itself is still open.
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open && stub_open);
  int before_leave = stub_close_n;
  // Trying a face applies it live without saving, and leaving the cell puts
  // the chosen face back.
  int tried = live_n, kept = save_n;
  stub_armed = true;
  panel_hover = "Tried Face";
  overlay_signs_step(0, &config, 100, 199, height, false, 7100);
  TEST_ASSERT(live_n == tried + 1 && save_n == kept);
  TEST_ASSERT(!strcmp(live_font, "Tried Face"));
  overlay_signs_step(0, &config, 100, 199, height, false, 7200);
  TEST_ASSERT(live_n == tried + 1);
  panel_hover = NULL;
  overlay_signs_step(0, &config, 100, 199, height, false, 7300);
  TEST_ASSERT(live_n == tried + 2 && save_n == kept);
  TEST_ASSERT(strcmp(live_font, "Tried Face") != 0);
  stub_armed = false;
  // The card is gone from under the pointer the moment the panel opens.
  // That alone must not count as leaving.
  overlay_signs_pointer(0, -1000, -1000);
  overlay_signs_step(0, &config, 100, 199, height, false, 7400);
  overlay_signs_step(0, &config, 100, 199, height, false, 7750);
  TEST_ASSERT(stub_open && stub_close_n == before_leave);
  stub_covers = true;
  overlay_signs_step(0, &config, 100, 199, height, false, 7800);
  TEST_ASSERT(stub_open && stub_close_n == before_leave);
  stub_covers = false;
  overlay_signs_step(0, &config, 100, 199, height, false, 7900);
  TEST_ASSERT(stub_open && stub_close_n == before_leave);
  // Once the pointer has been on the panel and left, everything closes.
  overlay_signs_step(0, &config, 100, 199, height, false, 8700);
  TEST_ASSERT(!stub_open && stub_close_n > before_leave);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);

  begin();
  stub_open = false;
  stub_close_n = 0;
  config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_animations = SIGN_ANIM_OFF;
  open_card(&config, &height, &cat_y);
  int after_open = stub_close_n;
  frame = overlay_signs_frame(0);
  overlay_signs_pointer(0, frame->menu_card.x + 4, frame->menu_card.y + 4);
  overlay_signs_press(0);
  TEST_ASSERT(stub_close_n == after_open);
  // With the panel open, pressing the cat closes the panel before dragging.
  stub_open = true;
  overlay_signs_pointer(0, 120, cat_y + 20);
  overlay_signs_press(0);
  TEST_ASSERT(stub_close_n == after_open + 1 && !stub_open);
}
int main(void) {
  geometry();
  quiet_pole();
  working_dots();
  open_and_close();
  click();
  focus_shake();
  hidden_is_quiet();
  damage_follows_cat();
  typing_desk();
  live_options();
  expand_edge();
  switch_card();
  font_row();
  font_name_button();
  return 0;
}

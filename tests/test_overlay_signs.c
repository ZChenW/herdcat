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
  TEST_ASSERT(height == 234);
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 120);
  config.cat_y_offset = -20;
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 100);
  config.cat_y_offset = -1000;
  TEST_ASSERT(overlay_signs_cat_y(&config, height) == 0);
  config_t room = config_of(110, 120, 0);
  int tall = overlay_signs_height(&room);
  TEST_ASSERT(tall == 300);
  TEST_ASSERT(overlay_signs_cat_y(&room, tall) == 186);
  config_t tight = config_of(110, 110, 0);
  int grown = overlay_signs_height(&tight);
  TEST_ASSERT(grown == 294);
  TEST_ASSERT(overlay_signs_cat_y(&tight, grown) == 180);
  config_t fan = config_of(110, 110, 0);
  fan.sign_style = SIGN_STYLE_FAN;
  int fan_height = overlay_signs_height(&fan);
  TEST_ASSERT(fan_height == 294);
  TEST_ASSERT(overlay_signs_cat_y(&fan, fan_height) == 180);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_FAN, 110) == 180);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_FAN, 110) - 174 >= 6);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_POST, 110) - 174 >= 6);
  config_t raised = config_of(110, 120, 0);
  raised.sign_style = SIGN_STYLE_FAN;
  TEST_ASSERT(overlay_signs_height(&raised) == 300);
  TEST_ASSERT(overlay_signs_cat_y(&raised, 300) == 186);
  int x = -4, y = -8;
  drag_clamp(&x, &y, 1000, 200, 800, fan_height);
  TEST_ASSERT(x == 0 && y == 0);
  x = 5000;
  y = 5000;
  drag_clamp(&x, &y, 1000, 200, 800, fan_height);
  TEST_ASSERT(x == 800 && y == 800 - fan_height);
}
static void quiet_pole(void) {
  const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST,
                                 SIGN_STYLE_OFF};
  for (size_t style = 0; style < sizeof(styles) / sizeof(styles[0]); style++) {
    begin();
    config_t config = config_of(40, 50, 10);
    config.sign_style = styles[style];
    int height = overlay_signs_height(&config);
    overlay_signs_step_t first =
        overlay_signs_step(0, &config, 10, 72, height, false, 0);
    TEST_ASSERT(first.redraw);
    TEST_ASSERT(!first.frame);
    TEST_ASSERT(first.timeout_ms < 0);
    TEST_ASSERT(first.damage_full);
    const int64_t times[] = {50, 180, 1000, 60000, 3600000};
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
      overlay_signs_step_t next =
          overlay_signs_step(0, &config, 10, 72, height, false, times[i]);
      TEST_ASSERT(!next.redraw);
      TEST_ASSERT(!next.frame);
      TEST_ASSERT(next.timeout_ms < 0);
      TEST_ASSERT(!next.damage_full);
    }
  }
}
static void working_dots(void) {
  const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST};
  for (size_t style = 0; style < sizeof(styles) / sizeof(styles[0]); style++) {
    begin();
    config_t config = config_of(40, 50, 0);
    config.sign_style = styles[style];
    int height = overlay_signs_height(&config);
    TEST_ASSERT(agent_sessions_apply(0x11, "claude", AGENT_EVENT_WORKING, 42, 0,
                                     0, NULL) == 0);
    overlay_signs_step_t rising =
        overlay_signs_step(0, &config, 10, 72, height, false, 0);
    TEST_ASSERT(rising.frame);
    TEST_ASSERT(rising.timeout_ms < 0);
    // Let the entrance scalars advance as actual surface callbacks would.
    // The fan's plate starts appearing after opacity first becomes positive.
    for (int64_t now = 17; now < 720; now += 17)
      overlay_signs_step(0, &config, 10, 72, height, false, now);
    overlay_signs_step_t dots =
        overlay_signs_step(0, &config, 10, 72, height, false, 720);
    TEST_ASSERT(!dots.frame);
    TEST_ASSERT(dots.timeout_ms == 180);
    // Release events between dot steps must not cause another submission.
    for (int64_t now = 721; now <= 1440; now++) {
      overlay_signs_step_t step =
          overlay_signs_step(0, &config, 10, 72, height, false, now);
      TEST_ASSERT(!step.frame);
      TEST_ASSERT(step.redraw == (now % 180 == 0));
      TEST_ASSERT(step.timeout_ms == 180 - now % 180);
    }
  }
}
static void waiting_phases(void) {
  const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST};
  for (size_t style = 0; style < sizeof(styles) / sizeof(styles[0]); style++) {
    begin();
    config_t config = config_of(110, 120, 0);
    config.sign_style = styles[style];
    int height = overlay_signs_height(&config);
    TEST_ASSERT(agent_sessions_apply(0x11, "claude", AGENT_EVENT_WAITING, 42, 0,
                                     0, NULL) == 0);
    for (int64_t now = 0; now < 1000; now += 17)
      overlay_signs_step(0, &config, 100, 200, height, false, now);
    overlay_signs_step(0, &config, 100, 200, height, false, 6000);
    int submissions = 0;
    for (int64_t now = 6001; now <= 36000; now++) {
      overlay_signs_step_t step =
          overlay_signs_step(0, &config, 100, 200, height, false, now);
      int phase = (int)(now % 1500) * 48 / 1500;
      int previous = (int)((now - 1) % 1500) * 48 / 1500;
      int boundary = ((phase + 1) * 1500 + 47) / 48;
      TEST_ASSERT(!step.frame);
      TEST_ASSERT(!overlay_signs_frame(0)->transitioning);
      TEST_ASSERT(step.redraw == (phase != previous));
      TEST_ASSERT(step.timeout_ms == boundary - now % 1500);
      submissions += step.redraw;
    }
    TEST_ASSERT(submissions >= 912 && submissions <= 1008);
    TEST_ASSERT(submissions == 960);
  }
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
  TEST_ASSERT(height == 116);
  TEST_ASSERT(cat_y == 74);
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
  overlay_signs_on_menu(take_style, take_language, take_paw, NULL, NULL);
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
  overlay_signs_on_menu(NULL, NULL, NULL, NULL, NULL);
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
  overlay_signs_on_menu(NULL, NULL, NULL, take_font, NULL);
  int height = overlay_signs_height(&config);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  int cat_y = overlay_signs_cat_y(&config, height);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, 199, height, false, 1000);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open && frame->menu_card.h == 168);
  TEST_ASSERT(frame->menu_card.y == cat_y - 174);
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
static bool stub_has_choice;
static char stub_choice[128], stub_selected[128];
static const char *panel_hover;
static font_panel_anchor_t panel_anchor;
static sign_theme_t panel_theme;
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
  panel_anchor = card;
  panel_theme = config->sign_theme;
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
  if (!stub_armed)
    return false;
  if (button == 0x110 && state == 0 && panel_hover) {
    snprintf(stub_choice, sizeof(stub_choice), "%s", panel_hover);
    stub_has_choice = true;
  }
  return true;
}
const char *font_panel_surface_hover(void) {
  return panel_hover;
}
bool font_panel_surface_take_choice(char *out, size_t cap) {
  if (!stub_has_choice)
    return false;
  stub_has_choice = false;
  if (out && cap)
    snprintf(out, cap, "%s", stub_choice);
  return true;
}
void font_panel_surface_left(void) {
  stub_armed = stub_covers = false;
  panel_hover = NULL;
}
void font_panel_surface_select(const char *family) {
  snprintf(stub_selected, sizeof(stub_selected), "%s", family ? family : "");
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
  overlay_signs_on_menu(NULL, NULL, NULL, take_font, NULL);
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
// Keep contract failures fatal to the suite, but collect them so that a broken
// save deadline does not prevent checking the rest of the selection flow.
static int contract_failures;
#define EXPECT_CONTRACT(condition)                                        \
  do {                                                                    \
    if (!(condition)) {                                                   \
      fprintf(stderr, "%s:%d: contract failed: %s\n", __FILE__, __LINE__, \
              #condition);                                                \
      contract_failures++;                                                \
    }                                                                     \
  } while (0)

static config_t *flow_config;
static int64_t flow_now, flow_saved_at;
static int flow_saves;
static char flow_saved[128];
static void flow_font(const char *family, bool save) {
  // Match main.c's menu_font: both preview and save update the live face.
  snprintf(flow_config->sign_font, sizeof(flow_config->sign_font), "%s",
           family);
  TEST_ASSERT(text_set_family(family) == 0);
  if (save) {
    flow_saves++;
    flow_saved_at = flow_now;
    snprintf(flow_saved, sizeof(flow_saved), "%s", family);
  }
  printf("font callback: t=%lld save=%d family=%s\n", (long long)flow_now, save,
         family);
}
static overlay_signs_step_t flow_step(config_t *config, int height,
                                      int64_t now) {
  flow_now = now;
  return overlay_signs_step(0, config, 100, 199, height, false, now);
}
static void open_font_flow(config_t *config, int *height, char *original,
                           char *tried, size_t cap) {
  begin();
  stub_open = stub_armed = stub_covers = stub_toggled = false;
  stub_has_choice = false;
  panel_hover = NULL;
  flow_saves = 0;
  flow_saved_at = 0;
  flow_saved[0] = '\0';
  TEST_ASSERT(text_init(NULL) == 0);
  const char *families[2];
  TEST_ASSERT(text_families("en", families, 2) >= 2);
  snprintf(original, cap, "%s", families[0]);
  snprintf(tried, cap, "%s", families[1]);
  TEST_ASSERT(strcmp(original, tried) != 0);
  *config = config_of(110, 120, 0);
  config->sign_style = SIGN_STYLE_FAN;
  config->sign_language = SIGN_LANGUAGE_EN;
  config->sign_animations = SIGN_ANIM_OFF;
  snprintf(config->sign_font, sizeof(config->sign_font), "%s", original);
  TEST_ASSERT(text_set_family(original) == 0);
  flow_config = config;
  overlay_signs_on_menu(NULL, NULL, NULL, flow_font, NULL);
  TEST_ASSERT(agent_sessions_apply(0x91, "claude", AGENT_EVENT_WAITING, 42, 0,
                                   0, NULL) == 0);
  *height = overlay_signs_height(config);
  flow_step(config, *height, 1000);
  int cat_y = overlay_signs_cat_y(config, *height);
  right_click(120, cat_y + 20);
  flow_step(config, *height, 1000);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && frame->menu_open);
  click_rect(frame->menu_font);
  flow_step(config, *height, 1100);
  TEST_ASSERT(stub_toggled && stub_open);
  // Opening is synchronized after building the card frame. Let the next
  // frame present browsing before the pointer enters the separate panel.
  flow_step(config, *height, 1150);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open && stub_open);
  stub_armed = stub_covers = true;
  overlay_signs_track_panel(0);
  panel_hover = tried;
  flow_step(config, *height, 1200);
  TEST_ASSERT(!overlay_signs_frame(0)->menu_open);
  TEST_ASSERT(!strcmp(config->sign_font, tried));
  TEST_ASSERT(!strcmp(stub_selected, original));
  TEST_ASSERT(flow_saves == 0);
}
static void expect_sign_font(const config_t *config, const char *family) {
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame && !frame->menu_open && frame->text_count > 0);
  const sign_text_t *label = &frame->texts[0];
  enum {
    WIDTH = 512,
    HEIGHT = 64,
    BYTES = WIDTH * HEIGHT * 4
  };
  uint8_t *actual = calloc(BYTES, 1), *expected = calloc(BYTES, 1);
  TEST_ASSERT(actual && expected);
  // The sign renderer also uses the main face for an empty text.family.
  text_draw_family(actual, WIDTH, HEIGHT, 0, 50,
                   label->family[0] ? label->family : NULL, label->value, 32,
                   true, 0xffffffff, WIDTH);
  text_draw_family(expected, WIDTH, HEIGHT, 0, 50, family, label->value, 32,
                   true, 0xffffffff, WIDTH);
  bool ink = false;
  for (int i = 0; i < BYTES; i++)
    ink |= expected[i] != 0;
  TEST_ASSERT(ink);
  EXPECT_CONTRACT(!strcmp(config->sign_font, family));
  EXPECT_CONTRACT(memcmp(actual, expected, BYTES) == 0);
  free(actual);
  free(expected);
}
static void font_panel_selection(void) {
  config_t config;
  int height;
  char original[128], tried[128];
  open_font_flow(&config, &height, original, tried, sizeof(original));
  TEST_ASSERT(overlay_signs_button(0x110, 1));
  TEST_ASSERT(overlay_signs_button(0x110, 0));
  TEST_ASSERT(stub_has_choice);
  overlay_signs_step_t picked = flow_step(&config, height, 1300);
  TEST_ASSERT(!stub_has_choice);
  TEST_ASSERT(!strcmp(stub_selected, tried));
  printf("after choice: saves=%d timeout=%d\n", flow_saves, picked.timeout_ms);
  // A click in the panel is a deliberate choice and is saved at once. Only
  // stepping with the arrows or the wheel waits 500 ms before saving.
  EXPECT_CONTRACT(flow_saves == 1 && !strcmp(flow_saved, tried));
  EXPECT_CONTRACT(flow_saved_at == 1300);
  // Leaving the cell afterwards must keep the new choice.
  panel_hover = NULL;
  flow_step(&config, height, 1400);
  EXPECT_CONTRACT(!strcmp(config.sign_font, tried));
  EXPECT_CONTRACT(!strcmp(stub_selected, tried));
  flow_step(&config, height, 1800);
  EXPECT_CONTRACT(flow_saves == 1);
  // The one-shot choice and the save must not repeat on later steps.
  flow_step(&config, height, 1801);
  EXPECT_CONTRACT(flow_saves == 1);
  font_panel_surface_left();
  overlay_signs_leave();
  flow_step(&config, height, 1900);
  flow_step(&config, height, 2699);
  TEST_ASSERT(stub_open);
  flow_step(&config, height, 2700);
  EXPECT_CONTRACT(!stub_open);
  EXPECT_CONTRACT(flow_saves == 1);
  expect_sign_font(&config, tried);
  begin();
  flow_config = NULL;
  text_cleanup();
}
static void font_panel_cancel(void) {
  config_t config;
  int height;
  char original[128], tried[128];
  open_font_flow(&config, &height, original, tried, sizeof(original));
  font_panel_surface_left();
  overlay_signs_leave();
  overlay_signs_step_t left = flow_step(&config, height, 1300);
  // The hover pad can wake before the panel's 800 ms close deadline.
  EXPECT_CONTRACT(left.timeout_ms > 0 && left.timeout_ms <= 800);
  EXPECT_CONTRACT(!strcmp(config.sign_font, original));
  EXPECT_CONTRACT(flow_saves == 0);
  flow_step(&config, height, 2099);
  TEST_ASSERT(stub_open);
  flow_step(&config, height, 2100);
  EXPECT_CONTRACT(!stub_open);
  flow_step(&config, height, 7000);
  EXPECT_CONTRACT(flow_saves == 0 && !flow_saved[0]);
  expect_sign_font(&config, original);
  begin();
  flow_config = NULL;
  text_cleanup();
}
static void expect_output_y(size_t index, const config_t *config, int height,
                            int expected) {
  int actual = overlay_signs_cat_y_at(index, config, height);
  printf("cat_height=%d: y=%d expected=%d\n", config->cat_height, actual,
         expected);
  EXPECT_CONTRACT(actual == expected);
}
static void expect_cat_region(overlay_signs_rect_t rect, int x, int y, int w,
                              int h) {
  EXPECT_CONTRACT(rect.x == x && rect.y == y && rect.w == w && rect.h == h);
}
static void interleaved_outputs(void) {
  begin();
  config_t config[2] = {config_of(110, 120, 0), config_of(220, 260, -15)};
  int height[2], rest[2];
  for (size_t i = 0; i < 2; i++) {
    config[i].sign_animations = SIGN_ANIM_OFF;
    height[i] = overlay_signs_height(&config[i]);
    overlay_signs_step(i, &config[i], 100 + (int)i * 300, 199 * (int)(i + 1),
                       height[i], false, 1000);
    TEST_ASSERT(overlay_signs_frame(i)->cat_lift == 0);
    rest[i] = overlay_signs_cat_y(&config[i], height[i]);
  }
  TEST_ASSERT(rest[0] != rest[1]);
  TEST_ASSERT(agent_sessions_apply(0x92, "claude", AGENT_EVENT_WORKING, 42,
                                   1000, 0, NULL) == 0);
  overlay_signs_type_at(0x92, 1000);
  overlay_signs_step(0, &config[0], 100, 199, height[0], false, 1000);
  // Output 1 retains its unraised frame while output 0 has presented typing.
  // Reading either output must not change the geometry of the other one.
  const sign_frame_t *raised = overlay_signs_frame(0);
  const sign_frame_t *quiet = overlay_signs_frame(1);
  TEST_ASSERT(raised != quiet);
  TEST_ASSERT(raised->cat_lift == 8 && quiet->cat_lift == 0);
  TEST_ASSERT(wide_board(raised) && !wide_board(quiet));
  int expected[2] = {rest[0] - 8, rest[1]};
  expect_output_y(0, &config[0], height[0], expected[0]);
  expect_output_y(1, &config[1], height[1], expected[1]);

  overlay_signs_rect_t regions[2][OVERLAY_SIGNS_REGION_LIMIT];
  TEST_ASSERT(overlay_signs_regions(0, &config[0], 100, 199, height[0],
                                    regions[0],
                                    OVERLAY_SIGNS_REGION_LIMIT) == 1);
  TEST_ASSERT(overlay_signs_regions(1, &config[1], 400, 398, height[1],
                                    regions[1],
                                    OVERLAY_SIGNS_REGION_LIMIT) == 1);
  expect_cat_region(regions[0][0], 100, expected[0], 199, 110);
  expect_cat_region(regions[1][0], 400, expected[1], 398, 220);
  expect_output_y(0, &config[0], height[0], expected[0]);
  expect_output_y(1, &config[1], height[1], expected[1]);

  TEST_ASSERT(overlay_signs_frame(1) == quiet);
  TEST_ASSERT(overlay_signs_frame(0) == raised);
  expect_output_y(1, &config[1], height[1], expected[1]);
  expect_output_y(0, &config[0], height[0], expected[0]);
  EXPECT_CONTRACT(raised->cat_lift == 8 && quiet->cat_lift == 0);
  begin();
}
static int theme_choices;
static sign_theme_t last_theme;
static void take_theme(sign_theme_t theme) {
  theme_choices++;
  last_theme = theme;
}
static void theme_switch_and_reload(void) {
  begin();
  config_t config = config_of(110, 120, 0);
  config.sign_style = SIGN_STYLE_FAN;
  config.sign_animations = SIGN_ANIM_OFF;
  overlay_signs_on_menu(NULL, NULL, NULL, NULL, take_theme);
  theme_choices = 0;
  int height = overlay_signs_height(&config);
  int cat_y = overlay_signs_cat_y(&config, height);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1000);
  right_click(120, cat_y + 20);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1000);
  const sign_frame_t *frame = overlay_signs_frame(0);
  sign_rect_t light = frame->menu_theme[0], dark = frame->menu_theme[2];
  TEST_ASSERT(frame->menu_card.y >= 6);
  TEST_ASSERT(frame->menu_card.y + frame->menu_card.h == cat_y - 6);
  click_rect(dark);
  overlay_signs_step_t step =
      overlay_signs_step(0, &config, 100, cat_y, height, false, 1100);
  TEST_ASSERT(step.redraw && theme_choices == 1 &&
              last_theme == SIGN_THEME_DARK);
  frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open && frame->menu_theme_thumb.x == dark.x - 1);
  TEST_ASSERT(panel_theme == SIGN_THEME_DARK);
  TEST_ASSERT(panel_anchor.y == frame->menu_card.y &&
              panel_anchor.h == frame->menu_card.h);
  click_rect(dark);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1200);
  TEST_ASSERT(theme_choices == 1);
  click_rect(light);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1300);
  TEST_ASSERT(theme_choices == 2 && last_theme == SIGN_THEME_LIGHT);
  click_rect(dark);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1400);
  TEST_ASSERT(theme_choices == 3);
  // Reload discards the UI override and updates the card and panel together.
  overlay_signs_use_config();
  step = overlay_signs_step(0, &config, 100, cat_y, height, false, 1500);
  TEST_ASSERT(step.redraw && panel_theme == SIGN_THEME_LIGHT);
  TEST_ASSERT(overlay_signs_frame(0)->menu_theme_thumb.x == light.x + 4);
  overlay_signs_step_t quiet =
      overlay_signs_step(0, &config, 100, cat_y, height, false, 1600);
  TEST_ASSERT(!quiet.redraw && !quiet.frame);
  // Releasing on the other half never applies a choice.
  overlay_signs_pointer(0, dark.x + 5, dark.y + 5);
  TEST_ASSERT(overlay_signs_press(0));
  overlay_signs_pointer(0, light.x + 5, light.y + 5);
  overlay_signs_release(false, NULL, NULL, NULL);
  overlay_signs_step(0, &config, 100, cat_y, height, false, 1700);
  TEST_ASSERT(theme_choices == 3);
  begin();
  overlay_signs_on_menu(NULL, NULL, NULL, NULL, NULL);
}
int main(int argc, char **argv) {
  // Focused entry points preserve the same assertions used by make test.
  if (argc == 2) {
    if (!strcmp(argv[1], "font-panel-selection"))
      font_panel_selection();
    else if (!strcmp(argv[1], "font-panel-cancel"))
      font_panel_cancel();
    else if (!strcmp(argv[1], "interleaved-outputs"))
      interleaved_outputs();
    else {
      fprintf(stderr, "Unknown test: %s\n", argv[1]);
      return EXIT_FAILURE;
    }
    return contract_failures ? EXIT_FAILURE : EXIT_SUCCESS;
  }
  TEST_ASSERT(argc == 1);
  theme_switch_and_reload();
  geometry();
  quiet_pole();
  working_dots();
  waiting_phases();
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
  font_panel_selection();
  font_panel_cancel();
  interleaved_outputs();
  return contract_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

#include "core/agent_sessions.h"
#include "platform/overlay_signs.h"
#include "test_helpers.h"

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
  return 0;
}

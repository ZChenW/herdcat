#define _POSIX_C_SOURCE 200809L
#include "../src/platform/overlay_signs_internal.h"
#include "core/agent_sessions.h"
#include "platform/font_panel.h"
#include "platform/overlay_signs.h"
#include "test_helpers.h"

#include <string.h>

// The panel is a separate Wayland surface; this model test records its anchor.
static font_panel_anchor_t panel_anchor;
bool font_panel_surface_is_open(void) {
  return false;
}
void font_panel_surface_close(void) {}
void font_panel_surface_sync(size_t index, const config_t *config,
                             font_panel_anchor_t card, int surface_h,
                             int64_t now_ms, int *timeout_ms, bool toggle) {
  (void)index;
  (void)config;
  (void)surface_h;
  (void)now_ms;
  (void)timeout_ms;
  (void)toggle;
  panel_anchor = card;
}
void font_panel_surface_activity(int64_t now_ms) {
  (void)now_ms;
}
bool font_panel_surface_covers(size_t index) {
  (void)index;
  return false;
}
bool font_panel_surface_armed(void) {
  return false;
}
void font_panel_surface_wheel(int discrete) {
  (void)discrete;
}
bool font_panel_surface_button(uint32_t button, uint32_t state) {
  (void)button;
  (void)state;
  return false;
}
const char *font_panel_surface_hover(void) {
  return NULL;
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

static config_t config = {.cat_height = 110,
                          .overlay_height = 110,
                          .sign_style = SIGN_STYLE_FAN,
                          .sign_max = 5,
                          .sign_font_size = 13,
                          .sign_idle = SIGN_IDLE_ALWAYS,
                          .sign_animations = SIGN_ANIM_FULL,
                          .sign_typing_desk = 1};
static void step(size_t index, int64_t now) {
  overlay_signs_step(index, &config, 100, 198, 294, false, now);
}
static void reset_and_regions(void) {
  agent_sessions_reset();
  overlay_signs_cleanup();
  TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, 42, 0, 0,
                                   NULL) == 0);
  overlay_signs_place(0, SIGN_ABOVE, 180);
  overlay_signs_place(1, SIGN_ABOVE, 180);
  step(0, 1000);
  step(0, 1600);
  step(1, 1600);
  sign_frame_t other = *overlay_signs_frame(1);
  TEST_ASSERT(lanes[0].model.initialized);
  overlay_signs_place(0, SIGN_BELOW, 8);
  TEST_ASSERT(!lanes[0].model.initialized);
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    TEST_ASSERT(!lanes[0].model.slots[i].used);
  step(0, 1700);
  TEST_ASSERT(overlay_signs_last(0).damage_full);
  TEST_ASSERT(overlay_signs_frame(0)->transitioning);
  step(0, 2300);
  TEST_ASSERT(memcmp(&other, overlay_signs_frame(1), sizeof(other)) == 0);
  overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
  int count = overlay_signs_regions(0, &config, 100, 198, 294, rects,
                                    OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(count >= 2 && rects[0].y == 8 && rects[0].h == 110);
  TEST_ASSERT(rects[1].y + rects[1].h > rects[0].y + rects[0].h);
  TEST_ASSERT(overlay_signs_cat_y_at(1, &config, 294) == 180);

  // A menu keeps both its open state and animated scalar state on a flip.
  config.sign_animations = SIGN_ANIM_OFF;
  overlay_signs_pointer(0, 130, 28);
  TEST_ASSERT(overlay_signs_button(273, 1));
  TEST_ASSERT(overlay_signs_button(273, 0));
  step(0, 2400);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  sign_menu_t menu = lanes[0].model.menu;
  overlay_signs_place(0, SIGN_ABOVE, 180);
  TEST_ASSERT(memcmp(&menu, &lanes[0].model.menu, sizeof(menu)) == 0);
  step(0, 2500);
  TEST_ASSERT(overlay_signs_frame(0)->menu_open);
  overlay_signs_place(0, SIGN_BELOW, 8);
  step(0, 2600);
  const sign_frame_t *frame = overlay_signs_frame(0);
  TEST_ASSERT(frame->menu_open && frame->menu_card.y >= 118);
  TEST_ASSERT(panel_anchor.x == frame->menu_card.x &&
              panel_anchor.y == frame->menu_card.y);
  count = overlay_signs_regions(0, &config, 100, 198, 294, rects,
                                OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(count == 2 && rects[1].y == frame->menu_card.y);
}
static void desk_at_edge(void) {
  overlay_signs_cleanup();
  overlay_signs_place(0, SIGN_BELOW, 0);
  overlay_signs_type_at(1, 3000);
  step(0, 3000);
  TEST_ASSERT(overlay_signs_frame(0)->cat_lift == 0);
  TEST_ASSERT(overlay_signs_cat_y_at(0, &config, 294) == 0);
  const sign_frame_t *frame = overlay_signs_frame(0);
  bool desk = false;
  for (int i = 0; i < frame->text_count; i++)
    desk |= frame->texts[i].caret;
  TEST_ASSERT(desk);
  overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
  int n = overlay_signs_regions(0, &config, 100, 198, 294, rects,
                                OVERLAY_SIGNS_REGION_LIMIT);
  TEST_ASSERT(n == 1);  // The typing slot retracts; the desk is never an input.
}
int main(void) {
  reset_and_regions();
  desk_at_edge();
  overlay_signs_cleanup();
  return 0;
}

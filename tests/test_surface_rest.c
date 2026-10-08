#include "core/agent_sessions.h"
#include "platform/overlay_signs.h"
#include "platform/surface_tiers.h"
#include "test_helpers.h"

#include <math.h>
#include <string.h>

static config_t config = {.cat_height = 110,
                          .overlay_height = 120,
                          .sign_max = 10,
                          .sign_style = SIGN_STYLE_FAN};
static void animation_bounds(void) {
  const int heights[] = {40, 110, 220};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
    for (unsigned h = 0; h < sizeof(heights) / sizeof(*heights); h++)
      for (int count = 1; count <= 10; count++)
        for (int state = 0; state < AGENT_STATE_COUNT; state++) {
          config_t local = config;
          local.sign_style = (sign_style_t)style;
          local.cat_height = heights[h];
          local.overlay_height = heights[h] + 10;
          agent_session_view_t sessions[10] = {0};
          for (int i = 0; i < count; i++) {
            sessions[i].key = (uint64_t)i + 1;
            sessions[i].order = (uint64_t)i + 1;
            sessions[i].state = (agent_state_t)state;
            sessions[i].child_count = 3;
            sessions[i].unread = true;
            strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
          }
          if (sign_name_persistent(local.sign_style, &sessions[0]))
            continue;
          int tier = surface_tier_select(&local, count, false);
          surface_size_t size = surface_tier_size(&local, tier, 1920, 120);
          int cat_w = local.cat_height * 500 / 277;
          int cat_x = (size.width - cat_w) / 2;
          for (int below = 0; below < 2; below++) {
            local.overlay_position = POSITION_TOP;
            overlay_vertical_t v = surface_tier_vertical(
                &local, below ? 30 : 600, 1920, size.height, false, SIGN_ABOVE,
                120, tier);
            sign_input_t input = {.style = local.sign_style,
                                  .sessions = sessions,
                                  .count = (size_t)count,
                                  .idle = SIGN_IDLE_ALWAYS,
                                  .animations = SIGN_ANIM_FULL,
                                  .cat_height = local.cat_height,
                                  .cat_x = cat_x,
                                  .cat_y = v.cat_y_in_surface,
                                  .orientation = v.orientation,
                                  .surface_width = size.width,
                                  .surface_height = surface_tier_model_height(
                                      &local, v.orientation, size.height)};
            signs_t model = {0};
            sign_frame_t frame;
            for (int t = 0; t <= 2000; t += 4) {
              input.now_ms = t;
              signs_frame(&model, &input, &frame);
              for (int i = 0; i < frame.shape_count; i++) {
                const sign_shape_t *s = &frame.shapes[i];
                double r = s->rotation * acos(-1) / 180;
                double cx = s->orbit ? s->origin_x : s->x + s->w / 2;
                double cy = s->orbit ? s->origin_y : s->y + s->h / 2;
                for (int j = 0; j < 4; j++) {
                  double x = s->x + (j & 1 ? s->w : 0) - cx;
                  double y = s->y + (j & 2 ? s->h : 0) - cy;
                  double px = cx + x * cos(r) - y * sin(r);
                  double py = cy + x * sin(r) + y * cos(r);
                  TEST_ASSERT(px >= 0 && px < size.width);
                  TEST_ASSERT(py >= 0 && py < size.height);
                }
              }
            }
          }
        }
}

static void admission(void) {
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    config.sign_idle = SIGN_IDLE_ALWAYS;
    config.sign_animations = SIGN_ANIM_OFF;
    agent_sessions_reset();
    overlay_signs_cleanup();
    TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_WORKING, 0, 0, 0,
                                     NULL) == 0);
    overlay_signs_capacity(0, 5 | SURFACE_TIER_REST);
    overlay_signs_place(0, SIGN_ABOVE, 100);
    overlay_signs_step_t step =
        overlay_signs_step(0, &config, 8, 198, 308, false, 1000);
    TEST_ASSERT(step.required_capacity == (5 | SURFACE_TIER_REST));
    sign_frame_t closed = *overlay_signs_frame(0);
    TEST_ASSERT(closed.hit_count == 1);
    overlay_signs_pointer(0, closed.hits[0].x + closed.hits[0].w / 2,
                          closed.hits[0].y + closed.hits[0].h / 2);
    step = overlay_signs_step(0, &config, 8, 198, 308, false, 1100);
    TEST_ASSERT(step.required_capacity == 5 && step.shrink_blocked);
    TEST_ASSERT(memcmp(&closed, overlay_signs_frame(0), sizeof(closed)) == 0);
    overlay_signs_capacity(0, 5);
    step = overlay_signs_step(0, &config, 8, 198, 308, false, 1200);
    TEST_ASSERT(step.required_capacity == 5);
    TEST_ASSERT(memcmp(&closed, overlay_signs_frame(0), sizeof(closed)) != 0);
    overlay_signs_leave();
    overlay_signs_step(0, &config, 8, 198, 308, false, 4000);
    step = overlay_signs_step(0, &config, 8, 198, 308, false, 5000);
    TEST_ASSERT(step.required_capacity == (5 | SURFACE_TIER_REST));
    overlay_signs_capacity(0, 5 | SURFACE_TIER_REST);
    overlay_signs_step(0, &config, 8, 198, 308, false, 5100);
    closed = *overlay_signs_frame(0);
    TEST_ASSERT(agent_sessions_apply(1, "claude", AGENT_EVENT_WAITING, 0, 0, 0,
                                     NULL) == 0);
    step = overlay_signs_step(0, &config, 8, 198, 308, false, 5200);
    TEST_ASSERT(step.required_capacity == 5);
    TEST_ASSERT(memcmp(&closed, overlay_signs_frame(0), sizeof(closed)) == 0);
    overlay_signs_capacity(0, 5);
    overlay_signs_step(0, &config, 8, 198, 308, false, 5300);
    TEST_ASSERT(memcmp(&closed, overlay_signs_frame(0), sizeof(closed)) != 0);
  }
  overlay_signs_cleanup();
  agent_sessions_reset();
}

int main(void) {
  TEST_ASSERT(surface_tier_select(&config, 0, false) == 0);
  TEST_ASSERT(surface_tier_select(&config, 1, false) ==
              (5 | SURFACE_TIER_REST));
  TEST_ASSERT(surface_tier_select(&config, 5, true) == 5);
  TEST_ASSERT(surface_tier_select(&config, 6, false) ==
              (10 | SURFACE_TIER_REST));
  TEST_ASSERT(surface_tier_select(&config, 10, true) == 10);
  surface_size_t small =
      surface_tier_size(&config, 5 | SURFACE_TIER_REST, 1920, 120);
  TEST_ASSERT(small.width == 214 && small.height == 212);
  surface_size_t large =
      surface_tier_size(&config, 10 | SURFACE_TIER_REST, 1920, 120);
  TEST_ASSERT(large.width == 214 && large.height == 284);
  config.sign_style = SIGN_STYLE_POST;
  small = surface_tier_size(&config, 5 | SURFACE_TIER_REST, 1920, 120);
  large = surface_tier_size(&config, 10 | SURFACE_TIER_REST, 1920, 120);
  TEST_ASSERT(small.width == 214 && small.height == 308);
  TEST_ASSERT(large.width == 214 && large.height == 473);
  surface_tiers_t tiers = {.capacity = 10 | SURFACE_TIER_REST};
  TEST_ASSERT(surface_tier_update(&tiers, 5, true, false, 100) == 10);
  TEST_ASSERT(tiers.capacity == (10 | SURFACE_TIER_REST) && tiers.pending);
  surface_tier_ready(&tiers);
  TEST_ASSERT(tiers.capacity == 10);
  TEST_ASSERT(surface_tier_update(&tiers, 5 | SURFACE_TIER_REST, false, false,
                                  200) == -1);
  TEST_ASSERT(tiers.shrink_at == 10200);
  TEST_ASSERT(surface_tier_update(&tiers, 10, true, false, 10200) == -1);
  TEST_ASSERT(tiers.shrink_at == 0);
  TEST_ASSERT(surface_tier_update(&tiers, 10 | SURFACE_TIER_REST, false, true,
                                  11000) == -1);
  TEST_ASSERT(tiers.shrink_at == 0);
  TEST_ASSERT(surface_tier_update(&tiers, 10 | SURFACE_TIER_REST, false, false,
                                  12000) == -1);
  TEST_ASSERT(surface_tier_update(&tiers, 10 | SURFACE_TIER_REST, false, false,
                                  21999) == -1);
  TEST_ASSERT(surface_tier_update(&tiers, 10 | SURFACE_TIER_REST, false, false,
                                  22000) == (10 | SURFACE_TIER_REST));
  surface_tier_ready(&tiers);
  TEST_ASSERT(tiers.capacity == (10 | SURFACE_TIER_REST));
  animation_bounds();
  admission();
  return 0;
}

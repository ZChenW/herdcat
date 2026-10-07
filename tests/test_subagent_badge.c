#include "core/agent_sign_state.h"
#include "graphics/sign_draw.h"
#include "graphics/sign_palette.h"
#include "graphics/text.h"
#include "signs_nanosvg.h"
#include "subagent_pixel_hashes.h"
#include "test_helpers.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void near(double actual, double expected) {
  TEST_ASSERT(fabs(actual - expected) < 1e-8);
}
static sign_input_t input(agent_session_view_t *sessions, size_t count) {
  return (sign_input_t){.sessions = sessions,
                        .count = count,
                        .style = SIGN_STYLE_FAN,
                        .cat_x = 260,
                        .cat_y = 380,
                        .cat_height = 110,
                        .idle = SIGN_IDLE_HOVER,
                        .animations = SIGN_ANIM_OFF,
                        .english = true,
                        .now_ms = 181000};
}
static agent_session_view_t parent(void) {
  agent_session_view_t s = {
      .key = 1, .order = 1, .child_count = 2, .child_started_ms = 1000};
  strcpy(s.agent, "claude");
  strcpy(s.name, "repo");
  strcpy(s.child_agents[0], "codex");
  s.child_counts[0] = 2;
  return s;
}
static const sign_shape_t *badge(const sign_frame_t *frame, int n) {
  for (int i = 0; i < frame->shape_count; i++)
    if (frame->shapes[i].kind == SIGN_BADGE && n-- == 0)
      return &frame->shapes[i];
  return NULL;
}
static const sign_slot_t *slot(const signs_t *model, uint64_t key) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (model->slots[i].used && model->slots[i].session.key == key)
      return &model->slots[i];
  TEST_ASSERT(false);
  return NULL;
}
static void state_table(void) {
  for (int state = 0; state < AGENT_STATE_COUNT; state++)
    for (int unread = 0; unread < 2; unread++) {
      agent_session_view_t s = parent();
      s.state = (agent_state_t)state;
      s.unread = unread != 0;
      bool override =
          state == AGENT_STATE_IDLE || (state == AGENT_STATE_DONE && !unread);
      agent_state_t expected = override ? AGENT_STATE_WORKING : s.state;
      TEST_ASSERT(agent_sign_state(&s) == expected);
      sign_input_t in = input(&s, 1);
      in.has_hover = true;
      in.hover_key = s.key;
      for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
        in.style = (sign_style_t)style;
        in.open = style == SIGN_STYLE_POST;
        signs_t model = {0};
        sign_frame_t frame;
        signs_frame(&model, &in, &frame);
        TEST_ASSERT(frame.hit_count == 1);
        TEST_ASSERT(slot(&model, s.key)->session.state == s.state);
        near(slot(&model, s.key)->states[expected].target, 1);
        TEST_ASSERT((badge(&frame, 0) != NULL) == (style == SIGN_STYLE_FAN));
        TEST_ASSERT(frame.text_count == 1);
        if (override) {
          const sign_text_t *t = &frame.texts[0];
          if (style == SIGN_STYLE_POST)
            TEST_ASSERT(
                !strcmp(t->meta, "Claude +2 · Waiting on subagent 3 min"));
          else {
            bool found = false;
            for (int i = 0; i < t->nameplate.count; i++)
              found |= strstr(t->nameplate.text + t->nameplate.runs[i].start,
                              "Waiting on subagent 3 min") != NULL;
            TEST_ASSERT(found);
          }
          TEST_ASSERT(frame.next_frame_ms == 241000);
        }
      }
    }
}
static void wording_and_restore(void) {
  agent_session_view_t s = parent();
  sign_input_t in = input(&s, 1);
  in.has_hover = true;
  in.hover_key = s.key;
  strcpy(in.nameplate, "**{name}** · {state}");
  signs_t model = {0};
  sign_frame_t frame;
  for (int english = 0; english < 2; english++) {
    in.english = english != 0;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.text_count == 1);
    TEST_ASSERT(strstr(frame.texts[0].meta, english
                                                ? "Waiting on subagent 3 min"
                                                : "等待子代理 3 分钟"));
    bool found = false;
    for (int i = 0; i < frame.texts[0].nameplate.count; i++)
      found |= strstr(frame.texts[0].nameplate.text +
                          frame.texts[0].nameplate.runs[i].start,
                      english ? "Waiting on subagent 3 min"
                              : "等待子代理 3 分钟") != NULL;
    TEST_ASSERT(found);
  }
  in.now_ms = 240999;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(strstr(frame.texts[0].meta, "3 min"));
  in.now_ms++;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(strstr(frame.texts[0].meta, "4 min"));
  in.now_ms = 0;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(strstr(frame.texts[0].meta, "0 min"));
  in.now_ms = 300000;
  in.animations = SIGN_ANIM_REDUCED;
  s.child_count = 0;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.transitioning && frame.hit_count == 0);
  TEST_ASSERT(!badge(&frame, 0));
  near(slot(&model, 1)->states[AGENT_STATE_IDLE].target, 1);
  in.now_ms += 1000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.shape_count == 0 && !frame.animating);
  // A read completion restores its own done appearance instead of idle.
  s.state = AGENT_STATE_DONE;
  s.child_count = 1;
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&model, &in, &frame);
  near(slot(&model, 1)->states[AGENT_STATE_WORKING].target, 1);
  s.child_count = 0;
  signs_frame(&model, &in, &frame);
  near(slot(&model, 1)->states[AGENT_STATE_DONE].target, 1);
  TEST_ASSERT(frame.hit_count == 1);
}
static agent_session_view_t view(uint64_t key) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].key == key)
      return views[i];
  TEST_ASSERT(false);
  return (agent_session_view_t){0};
}
static void session_truth_and_timer(void) {
  agent_sessions_reset();
  TEST_ASSERT(
      !agent_sessions_apply(1, "claude", AGENT_EVENT_START, 100, 0, 0, NULL));
  for (int i = 0; i < 2; i++)
    TEST_ASSERT(!agent_sessions_apply_owned(
        2 + (uint64_t)i, "codex", AGENT_EVENT_WORKING, 200 + i, 200 + i, false,
        100, "/nonexistent", 1000 + i * 4000, 0));
  agent_session_view_t s = view(1);
  TEST_ASSERT(s.child_count == 2 && s.child_started_ms == 1000);
  TEST_ASSERT(s.state == AGENT_STATE_IDLE && !s.unread);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  TEST_ASSERT(!agent_sessions_apply(2, "codex", AGENT_EVENT_WAITING, 200,
                                    100000, 0, NULL));
  TEST_ASSERT(view(1).child_started_ms == 1000);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_IDLE);
  char before[4096], after[4096];
  agent_sessions_format(before, sizeof(before), 181000);
  s = view(1);
  sign_input_t in = input(&s, 1);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  agent_sessions_format(after, sizeof(after), 181000);
  TEST_ASSERT(!strcmp(before, after));
  TEST_ASSERT(!agent_sessions_apply(2, "codex", AGENT_EVENT_DONE, 200, 181000,
                                    0, NULL));
  s = view(1);
  TEST_ASSERT(s.child_count == 1 && s.child_started_ms == 5000);
  TEST_ASSERT(!view(2).unread);
  TEST_ASSERT(
      !agent_sessions_apply(3, "codex", AGENT_EVENT_END, 201, 182000, 0, NULL));
  s = view(1);
  TEST_ASSERT(!s.child_count && !s.child_started_ms);
  TEST_ASSERT(agent_sign_state(&s) == AGENT_STATE_IDLE);
  // Display selection prefers this idle parent over a more recent idle root.
  agent_session_view_t choices[2] = {
      parent(), {.key = 9, .updated_ms = 999}
  };
  agent_session_view_t selected[1];
  TEST_ASSERT(agent_sessions_select(choices, 2, selected, 1) == 1);
  TEST_ASSERT(selected[0].key == 1 && selected[0].state == AGENT_STATE_IDLE);
}
static void badge_geometry_and_layers(void) {
  agent_session_view_t sessions[6];
  for (int i = 0; i < 6; i++) {
    sessions[i] = parent();
    sessions[i].key = sessions[i].order = (uint64_t)i + 1;
    sessions[i].child_count = (unsigned)i + 1;
    sessions[i].state = i < 4 ? AGENT_STATE_WAITING : AGENT_STATE_IDLE;
  }
  sign_input_t in = input(sessions, 6);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 6);
  TEST_ASSERT(!slot(&model, 5)->back_row && slot(&model, 6)->back_row);
  const sign_shape_t *back = badge(&frame, 0);
  TEST_ASSERT(back && back->badge_count == 6);
  near(back->w, 13 * .88);
  near(badge(&frame, 1)->w, 13);
  for (int i = 0; i < 6; i++) {
    const sign_shape_t *b = badge(&frame, i);
    TEST_ASSERT(b && !b->above && b->upright && !b->orbit);
    TEST_ASSERT(b->x >= frame.bounds_x && b->y >= frame.bounds_y);
    TEST_ASSERT(b->x + b->w <= frame.bounds_x + frame.bounds_w);
    TEST_ASSERT(b->y + b->h <= frame.bounds_y + frame.bounds_h);
  }
  in.has_hover = true;
  in.hover_key = 6;
  signs_frame(&model, &in, &frame);
  back = badge(&frame, 0);
  TEST_ASSERT(back->badge_count == 6);
  near(back->w, 13 * .88 * 1.16);
  sign_shape_t above = *back;
  in.orientation = SIGN_BELOW;
  signs_frame(&model, &in, &frame);
  back = badge(&frame, 0);
  near(back->x, above.x);
  near(back->y, 870 - above.y - above.h);
  TEST_ASSERT(back->upright && !back->reflected);
  // Unread dot retains the right corner, badge switches to the left.
  agent_session_view_t s = parent();
  in = input(&s, 1);
  model = (signs_t){0};
  signs_frame(&model, &in, &frame);
  sign_shape_t right = *badge(&frame, 0);
  s.state = AGENT_STATE_DONE;
  s.unread = true;
  signs_frame(&model, &in, &frame);
  const sign_shape_t *left = badge(&frame, 0);
  near(left->x, right.x - 32);
  TEST_ASSERT(left->x + left->w < right.x);
  // Only the plate is interactive; the outside badge centre is not a target.
  sign_hit_t hit;
  TEST_ASSERT(!signs_hit(&frame, left->x + 1, left->y + 1, &hit));
  for (unsigned count = 1; count <= 12; count++) {
    s.child_count = count;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(badge(&frame, 0)->badge_count == count);
  }
  s.child_count = 0;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!badge(&frame, 0));
}
static void working_ink_and_clearance(void) {
  // A derived working sign has precisely the ordinary working plate/dots.
  for (int theme = 0; theme < 2; theme++)
    for (int anim = SIGN_ANIM_FULL; anim <= SIGN_ANIM_OFF; anim++) {
      agent_session_view_t derived = parent(), working = parent();
      working.state = AGENT_STATE_WORKING;
      working.child_count = 0;
      sign_input_t a = input(&derived, 1), b = input(&working, 1);
      a.theme = b.theme = (sign_theme_t)theme;
      a.animations = b.animations = (sign_animations_t)anim;
      signs_t ma = {0}, mb = {0};
      for (int t = 0; t < 1500; t += 31) {
        a.now_ms = b.now_ms = 300000 + t;
        sign_frame_t fa, fb;
        signs_frame(&ma, &a, &fa);
        signs_frame(&mb, &b, &fb);
        int next = 0;
        for (int i = 0; i < fa.shape_count; i++) {
          if (fa.shapes[i].kind == SIGN_BADGE)
            continue;
          TEST_ASSERT(next < fb.shape_count);
          TEST_ASSERT(
              !memcmp(&fa.shapes[i], &fb.shapes[next++], sizeof(sign_shape_t)));
        }
        TEST_ASSERT(next == fb.shape_count);
        TEST_ASSERT(fa.animating == fb.animating);
        TEST_ASSERT(fa.next_frame_ms == fb.next_frame_ms);
      }
    }
  // Badges remain inside the existing vertical surface at pop/hover/nudge
  // extrema, including larger cats, both directions and a shaking back sign.
  const int heights[] = {66, 110, 220};
  for (int n = 1; n <= 10; n += 9)
    for (int h = 0; h < 3; h++)
      for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
           orientation++) {
        agent_session_view_t sessions[10];
        for (int i = 0; i < n; i++) {
          sessions[i] = parent();
          sessions[i].key = sessions[i].order = (uint64_t)i + 1;
          sessions[i].state = AGENT_STATE_WAITING;
        }
        sign_input_t in = input(sessions, (size_t)n);
        int clearance = sign_clearance(SIGN_STYLE_FAN, heights[h], n);
        in.cat_height = heights[h];
        in.cat_y = orientation == SIGN_ABOVE ? clearance : 0;
        in.surface_height = clearance + heights[h] + 4;
        in.orientation = (sign_orientation_t)orientation;
        in.animations = SIGN_ANIM_FULL;
        in.open = true;
        in.has_hover = true;
        in.hover_key = (uint64_t)n;
        signs_t model = {0};
        for (int t = 0; t <= 3000; t += 17) {
          in.now_ms = 300000 + t;
          if (t == 1020)
            signs_focus_failed(&model, (uint64_t)n, in.now_ms);
          sign_frame_t frame;
          signs_frame(&model, &in, &frame);
          TEST_ASSERT(frame.bounds_y >= 0);
          TEST_ASSERT(frame.bounds_y + frame.bounds_h <= in.surface_height);
          for (int i = 0; i < frame.shape_count; i++)
            if (frame.shapes[i].kind == SIGN_BADGE) {
              const sign_shape_t *b = &frame.shapes[i];
              near(b->rotation, 0);
              TEST_ASSERT(b->upright);
            }
        }
      }
}
static void digit_pixels(void) {
  uint8_t pixels[40 * 40 * 4], nine_plus[sizeof(pixels)];
  sign_frame_t frame = {.shape_count = 1, .bounds_w = 40, .bounds_h = 40};
  frame.shapes[0] = (sign_shape_t){.kind = SIGN_BADGE,
                                   .x = 10,
                                   .y = 10,
                                   .w = 13,
                                   .h = 13,
                                   .radius = 6.5,
                                   .stroke = 1,
                                   .fill = 0xff222222,
                                   .outline = 0xffeeeeee};
  for (unsigned count = 0; count <= 12; count++) {
    memset(pixels, 0, sizeof(pixels));
    frame.shapes[0].badge_count = count;
    sign_draw(pixels, 40, 40, 120, &frame, SIGN_DRAW_UNDER);
    if (count == 9)
      memcpy(nine_plus, pixels, sizeof(pixels));
    if (count == 10) {
      TEST_ASSERT(memcmp(nine_plus, pixels, sizeof(pixels)));
      memcpy(nine_plus, pixels, sizeof(pixels));
    }
    if (count > 10)
      TEST_ASSERT(!memcmp(nine_plus, pixels, sizeof(pixels)));
  }
  // A later front plate covers both the circle and its glyph, not just the
  // circle. This detects accidental deferred text drawing above all shapes.
  frame.shape_count = 2;
  frame.shapes[1] = (sign_shape_t){
      .kind = SIGN_RECT, .x = 8, .y = 8, .w = 20, .h = 20, .fill = 0xff34785a};
  memset(pixels, 0, sizeof(pixels));
  sign_draw(pixels, 40, 40, 120, &frame, SIGN_DRAW_UNDER);
  for (int y = 10; y < 24; y++)
    for (int x = 10; x < 24; x++) {
      size_t at = ((size_t)y * 40 + (size_t)x) * 4;
      TEST_ASSERT(pixels[at] == 0x5a && pixels[at + 1] == 0x78 &&
                  pixels[at + 2] == 0x34 && pixels[at + 3] == 255);
    }
}
static void legacy_pixels(void) {
  const int counts[] = {6, 7, 10};
  const int w = 1000, h = 1100;
  size_t size = (size_t)w * h * 4;
  uint8_t *pixels = calloc(size, 1);
  agent_session_view_t sessions[10] = {0};
  for (int i = 0; i < 10; i++) {
    sessions[i].key = sessions[i].order = i + 1;
    sessions[i].state = (agent_state_t)(i % AGENT_STATE_COUNT);
    sessions[i].unread = true;
    strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
    strcpy(sessions[i].name, "repo");
  }
  size_t at = 0;
  for (int n = 0; n < 3; n++)
    for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
      for (int theme = 0; theme < 2; theme++)
        for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
             orientation++)
          for (int hover = 0; hover < 2; hover++) {
            sign_input_t in = {.sessions = sessions,
                               .count = counts[n],
                               .style = style,
                               .theme = theme,
                               .orientation = orientation,
                               .cat_x = 300,
                               .cat_y = 450,
                               .cat_height = 110,
                               .idle = SIGN_IDLE_ALWAYS,
                               .animations = SIGN_ANIM_OFF,
                               .english = true,
                               .now_ms = 100000,
                               .open = hover,
                               .has_hover = hover,
                               .hover_key = 6};
            signs_t model = {0};
            sign_frame_t frame;
            signs_frame(&model, &in, &frame);
            for (int scale = 120; scale <= 180; scale += 30) {
              memset(pixels, 0, size);
              sign_draw(pixels, w, h, scale, &frame, SIGN_DRAW_UNDER);
              sign_draw(pixels, w, h, scale, &frame, SIGN_DRAW_OVER);
              uint64_t hash = UINT64_C(14695981039346656037);
              for (size_t i = 0; i < size; i++)
                hash = (hash ^ pixels[i]) * UINT64_C(1099511628211);
              TEST_ASSERT(hash == SUBAGENT_LEGACY_HASHES[at++]);
            }
          }
  TEST_ASSERT(at == sizeof(SUBAGENT_LEGACY_HASHES) /
                        sizeof(SUBAGENT_LEGACY_HASHES[0]));
  free(pixels);
  sign_draw_cleanup();
}
int main(void) {
  // All hashes are taken before FreeType/Fontconfig can load any face.
  legacy_pixels();
  TEST_ASSERT(text_init("sans") == 0);
  state_table();
  wording_and_restore();
  session_truth_and_timer();
  badge_geometry_and_layers();
  working_ink_and_clearance();
  digit_pixels();
  sign_draw_cleanup();
  text_cleanup();
  puts("Subagent display state, timing, truth, badge and layers passed.");
  return 0;
}

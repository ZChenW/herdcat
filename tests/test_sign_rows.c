#define _POSIX_C_SOURCE 200809L
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/overlay_vertical.h"
#include "sign_rows_hashes.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void near(double got, double want) {
  if (fabs(got - want) >= 1e-8)
    fprintf(stderr, "near: got %.12f, want %.12f\n", got, want);
  TEST_ASSERT(fabs(got - want) < 1e-8);
}
static void sessions_init(agent_session_view_t *sessions, int count) {
  memset(sessions, 0, (size_t)count * sizeof(*sessions));
  for (int i = 0; i < count; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = (agent_state_t)(i % AGENT_STATE_COUNT);
    sessions[i].unread = true;
    strcpy(sessions[i].agent, i % 3 == 0   ? "claude"
                              : i % 3 == 1 ? "codex"
                                           : "custom");
    strcpy(sessions[i].name, "repo");
  }
}
static sign_input_t input(agent_session_view_t *sessions, int count) {
  return (sign_input_t){.sessions = sessions,
                        .count = (size_t)count,
                        .style = SIGN_STYLE_FAN,
                        .cat_x = 260,
                        .cat_y = 380,
                        .cat_height = 110,
                        .idle = SIGN_IDLE_ALWAYS,
                        .animations = SIGN_ANIM_OFF,
                        .english = true,
                        .now_ms = 100000};
}
static const sign_slot_t *slot_of(const signs_t *model, uint64_t key) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (model->slots[i].used && model->slots[i].session.key == key)
      return &model->slots[i];
  TEST_ASSERT(false);
  return NULL;
}
static const sign_hit_t *hit_of(const sign_frame_t *frame, uint64_t key) {
  for (int i = 0; i < frame->hit_count; i++)
    if (frame->hits[i].key == key)
      return &frame->hits[i];
  TEST_ASSERT(false);
  return NULL;
}
static void draw(uint8_t *data, int w, int h, int scale,
                 const sign_frame_t *frame) {
  memset(data, 0, (size_t)w * (size_t)h * 4);
  sign_draw(data, w, h, scale, frame, SIGN_DRAW_UNDER);
  sign_draw(data, w, h, scale, frame, SIGN_DRAW_OVER);
}
static void legacy_pixels(void) {
  const int w = 800, h = 900;
  size_t size = (size_t)w * (size_t)h * 4;
  uint8_t *data = calloc(size, 1);
  TEST_ASSERT(data);
  agent_session_view_t sessions[5];
  sessions_init(sessions, 5);
  sign_input_t in = input(sessions, 5);
  size_t at = 0;
  for (int count = 1; count <= 5; count++)
    for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++)
      for (int theme = 0; theme < 2; theme++)
        for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW;
             orientation++)
          for (int open = 0; open < 2; open++) {
            in.count = (size_t)count;
            in.style = (sign_style_t)style;
            in.theme = (sign_theme_t)theme;
            in.orientation = (sign_orientation_t)orientation;
            in.open = open != 0;
            in.has_hover = open != 0;
            in.hover_key = (uint64_t)count;
            signs_t model = {0};
            sign_frame_t frame;
            signs_frame(&model, &in, &frame);
            for (int scale = 120; scale <= 180; scale += 30) {
              draw(data, w, h, scale, &frame);
              uint64_t hash = UINT64_C(14695981039346656037);
              for (size_t i = 0; i < size; i++)
                hash = (hash ^ data[i]) * UINT64_C(1099511628211);
              TEST_ASSERT(hash == LEGACY_HASHES[at++]);
            }
          }
  TEST_ASSERT(at == sizeof(LEGACY_HASHES) / sizeof(LEGACY_HASHES[0]));
  free(data);
}
static double back_spread(double spread, double length) {
  return 2 * asin(80.5 / (80.5 + length) * sin(spread * acos(-1) / 360)) * 180 /
         acos(-1);
}
static void row_layout(void) {
  const int counts[] = {6, 7, 10};
  for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++)
    for (int open = 0; open < 2; open++) {
      int count = counts[c];
      agent_session_view_t sessions[10];
      sessions_init(sessions, count);
      for (int i = 0; i < count; i++)
        sessions[i].state = AGENT_STATE_WORKING;
      sign_input_t in = input(sessions, count);
      in.open = open != 0;
      signs_t model = {0};
      sign_frame_t frame;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.hit_count == count);
      double spread = open ? 22 : 15;
      double extension = count == 10 ? 56 : 40;
      for (int i = 0; i < count; i++) {
        const sign_slot_t *slot = slot_of(&model, sessions[i].key);
        bool back = i >= 5;
        double offset = back ? i - 5 - (count - 6) / 2.0 : i - 2;
        near(slot->bottom.target,
             offset * (back ? back_spread(spread, extension) : spread));
        near(slot->width.target, back ? 94 + extension : 94);
        near(slot->row_size.target, back ? .88 : 1);
        TEST_ASSERT(slot->back_row == back);
        const sign_hit_t *hit = hit_of(&frame, sessions[i].key);
        TEST_ASSERT(hit->back_row == back && hit->precise);
        // Plate centres are directly targetable, but the rod base is not.
        sign_hit_t target;
        TEST_ASSERT(signs_hit(&frame, hit->center_x, hit->center_y, &target));
        TEST_ASSERT(target.key == hit->key);
      }
      sign_hit_t target;
      TEST_ASSERT(!signs_hit(&frame, 368, 452, &target));
      // Every shape of the front row equals a standalone five-sign scene.
      signs_t five = {0};
      sign_frame_t reference;
      in.count = 5;
      signs_frame(&five, &in, &reference);
      int first = frame.shape_count - reference.shape_count;
      TEST_ASSERT(first > 0);
      TEST_ASSERT(
          !memcmp(&frame.shapes[first], reference.shapes,
                  (size_t)reference.shape_count * sizeof(sign_shape_t)));
      // Pairwise vertical plate bounds leave all back plates exposed.
      for (int b = 5; b < count; b++)
        for (int f = 0; f < 5; f++) {
          const sign_hit_t *back = hit_of(&frame, sessions[b].key);
          const sign_hit_t *front = hit_of(&frame, sessions[f].key);
          TEST_ASSERT(back->y + back->h < front->y);
        }
      // Reflection preserves hit ownership and draw order.
      in.count = (size_t)count;
      in.orientation = SIGN_BELOW;
      signs_t below = {0};
      sign_frame_t reflected;
      signs_frame(&below, &in, &reflected);
      TEST_ASSERT(reflected.shape_count == frame.shape_count);
      for (int i = 0; i < count; i++) {
        const sign_hit_t *a = hit_of(&frame, sessions[i].key);
        const sign_hit_t *b = hit_of(&reflected, sessions[i].key);
        near(b->center_x, a->center_x);
        near(b->center_y, 870 - a->center_y);
        TEST_ASSERT(b->back_row == a->back_row);
        TEST_ASSERT(signs_hit(&reflected, b->center_x, b->center_y, &target));
        TEST_ASSERT(target.key == b->key);
      }
    }
}
static void priority_and_transition(void) {
  agent_session_view_t sessions[10];
  sessions_init(sessions, 10);
  const agent_state_t states[] = {AGENT_STATE_IDLE,    AGENT_STATE_WORKING,
                                  AGENT_STATE_DONE,    AGENT_STATE_ERROR,
                                  AGENT_STATE_WAITING, AGENT_STATE_WORKING,
                                  AGENT_STATE_WAITING, AGENT_STATE_DONE,
                                  AGENT_STATE_ERROR,   AGENT_STATE_IDLE};
  for (int i = 0; i < 10; i++)
    sessions[i].state = states[i];
  sign_input_t in = input(sessions, 10);
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  // Waiting 5,7; error 4,9; unread done 3 precedes tied unread done 8.
  const bool front[] = {false, false, true,  true, true,
                        false, true,  false, true, false};
  int cursor = 0;
  for (int i = 0; i < 10; i++) {
    const sign_slot_t *slot = slot_of(&model, sessions[i].key);
    TEST_ASSERT(slot->back_row != front[i]);
    if (front[i])
      near(slot->bottom.target, (cursor++ - 2) * 15);
  }
  // Promotion moves key 1 into the front and key 3 into the back.
  const sign_slot_t *promoted = slot_of(&model, 1);
  double old_length = promoted->width.target;
  double old_angle = promoted->bottom.target;
  in.animations = SIGN_ANIM_REDUCED;
  in.now_ms += 1000;
  sessions[0].state = AGENT_STATE_WAITING;
  sessions[0].state_since_ms = in.now_ms;
  signs_frame(&model, &in, &frame);
  promoted = slot_of(&model, 1);
  const sign_slot_t *demoted = slot_of(&model, 3);
  TEST_ASSERT(!promoted->back_row && demoted->back_row);
  near(promoted->width.from, old_length);
  near(promoted->bottom.from, old_angle);
  near(promoted->row_size.from, .88);
  near(promoted->width.target, 110);
  near(demoted->width.from, 94);
  near(demoted->width.target, 166);
  TEST_ASSERT(frame.transitioning);
  in.now_ms += 210;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.transitioning && frame.hit_count == 10);
  TEST_ASSERT(promoted->width.start + promoted->width.duration > in.now_ms);
  in.now_ms += 790;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.transitioning);
  near(promoted->row_size.target, 1);
  near(demoted->row_size.target, .88);
}
// Assert group order using each group's stick to delimit all of its ink.
static void assert_layers(const sign_frame_t *frame, int back_count) {
  int groups = 0;
  bool front_started = false;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *s = &frame->shapes[i];
    if (s->h > 40 && s->w < 6 && s->stroke > 0) {
      bool back = s->w < 4.8;
      TEST_ASSERT(!back || !front_started);
      if (!back) {
        TEST_ASSERT(groups >= back_count);
        front_started = true;
      }
      groups++;
    }
    // Plate, icon and unread dot stay between their rod and the next rod.
    TEST_ASSERT(groups > 0);
    TEST_ASSERT(!s->above);
  }
  TEST_ASSERT(groups == 10);
}
static void layers_and_hits(void) {
  agent_session_view_t sessions[10];
  sessions_init(sessions, 10);
  for (int i = 0; i < 10; i++)
    sessions[i].state = AGENT_STATE_DONE;
  sign_input_t in = input(sessions, 10);
  in.open = true;
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  assert_layers(&frame, 5);
  // A back hover/press cannot raise any of its ink over any front ink.
  for (int orientation = SIGN_ABOVE; orientation <= SIGN_BELOW; orientation++)
    for (int pressed = 0; pressed < 2; pressed++) {
      in.orientation = (sign_orientation_t)orientation;
      in.has_hover = true;
      in.hover_key = 8;
      in.has_pressed = pressed != 0;
      in.pressed_key = 8;
      signs_frame(&model, &in, &frame);
      assert_layers(&frame, 5);
      TEST_ASSERT(frame.text_count == 1 && frame.texts[0].above);
      near(hit_of(&frame, 8)->half_w, 15 * .88 * (pressed ? .94 : 1.16));
    }
  in.orientation = SIGN_ABOVE;
  in.animations = SIGN_ANIM_FULL;
  for (int i = 0; i < 10; i++)
    sessions[i].state = AGENT_STATE_WAITING;
  in.now_ms += 1000;
  signs_frame(&model, &in, &frame);
  in.now_ms += 1000;
  signs_frame(&model, &in, &frame);
  signs_focus_failed(&model, 8, in.now_ms);
  for (int64_t dt = 0; dt < 350; dt += 31) {
    in.now_ms += 31;
    signs_frame(&model, &in, &frame);
    assert_layers(&frame, 5);
  }
  // Explicit overlapping plates: front wins even closer to a back centre.
  sign_frame_t overlap = {.hit_count = 2};
  overlap.hits[0] = (sign_hit_t){.x = 0,
                                 .y = 0,
                                 .w = 40,
                                 .h = 40,
                                 .key = 1,
                                 .back_row = true,
                                 .precise = true,
                                 .center_x = 20,
                                 .center_y = 20,
                                 .half_w = 20,
                                 .half_h = 20};
  overlap.hits[1] = overlap.hits[0];
  overlap.hits[1].key = 2;
  overlap.hits[1].back_row = false;
  overlap.hits[1].center_x = 30;
  sign_hit_t target;
  TEST_ASSERT(signs_hit(&overlap, 20, 20, &target) && target.key == 2);
  signs_reflect(&overlap, 50);
  TEST_ASSERT(signs_hit(&overlap, 20, 80, &target) && target.key == 2);
  // A rotated plate's axis-aligned bounding-box corner is not a target.
  const sign_hit_t *tilted = hit_of(&frame, 6);
  TEST_ASSERT(!signs_hit(&frame, tilted->x, tilted->y, &target));
  in.menu = true;
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 0 && frame.menu_open);
}
static void post_and_clearance(void) {
  agent_session_view_t sessions[10];
  sessions_init(sessions, 10);
  sign_input_t in = input(sessions, 10);
  in.style = SIGN_STYLE_POST;
  in.open = true;
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 10 && frame.text_count == 10);
  near(frame.shapes[0].h, 406);
  for (int i = 0; i < 10; i++) {
    const sign_slot_t *slot = slot_of(&model, sessions[i].key);
    near(slot->bottom.target, 100 + (9 - i) * 30);
    TEST_ASSERT(slot->direction == (sessions[i].order % 2 ? 1 : -1));
  }
  for (int max = 1; max <= 10; max++) {
    int fan = sign_clearance(SIGN_STYLE_FAN, 110, max);
    int post = sign_clearance(SIGN_STYLE_POST, 110, max);
    TEST_ASSERT(fan == (max <= 5 ? 180 : 273));
    TEST_ASSERT(post == (max <= 5 ? 180 : 180 + 33 * (max - 5)));
    config_t config = {
        .sign_style = SIGN_STYLE_FAN, .cat_height = 110, .sign_max = max};
    TEST_ASSERT(overlay_orientation(&config, fan - 1, 1080, fan + 114, false,
                                    SIGN_ABOVE) == SIGN_BELOW);
    TEST_ASSERT(overlay_orientation(&config, fan, 1080, fan + 114, false,
                                    SIGN_BELOW) == SIGN_ABOVE);
    TEST_ASSERT(overlay_orientation(&config, fan + 23, 1080, fan + 114, true,
                                    SIGN_BELOW) == SIGN_BELOW);
    TEST_ASSERT(overlay_orientation(&config, fan + 24, 1080, fan + 114, true,
                                    SIGN_BELOW) == SIGN_ABOVE);
  }
  // Animation overshoot stays within configured clearance, both directions.
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    for (int i = 0; i < 10; i++)
      sessions[i].state = AGENT_STATE_WAITING;
    in = input(sessions, 10);
    in.style = (sign_style_t)style;
    in.animations = SIGN_ANIM_FULL;
    in.open = true;
    in.has_hover = true;
    in.hover_key = 6;
    int clearance = sign_clearance(in.style, 110, 10);
    in.cat_y = clearance;
    in.surface_height = clearance + 114;
    model = (signs_t){0};
    for (int64_t now = 0; now <= 3000; now += 17) {
      in.now_ms = now;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.bounds_y >= 0);
      TEST_ASSERT(frame.bounds_y + frame.bounds_h <= in.surface_height);
    }
  }
}
static void cache_two_rows(void) {
  const int w = 1000, h = 1000;
  uint8_t *pixels = calloc((size_t)w * (size_t)h, 4);
  TEST_ASSERT(pixels);
  agent_session_view_t sessions[10];
  sessions_init(sessions, 10);
  for (int i = 0; i < 10; i++) {
    sessions[i].state = AGENT_STATE_WAITING;
    strcpy(sessions[i].agent, "claude");
  }
  sign_input_t in = input(sessions, 10);
  in.animations = SIGN_ANIM_FULL;
  signs_t model = {0};
  sign_frame_t frame;
  for (int64_t now = 0; now < 1000; now += 17) {
    in.now_ms = now;
    signs_frame(&model, &in, &frame);
  }
  // Ten waiting signs at one scale fit; more than that may evict.
  sign_draw_cleanup();
  for (int cycle = 0; cycle < 2; cycle++) {
    sign_draw_cache_reset_stats();
    for (int phase = 0; phase < 48; phase++) {
      in.now_ms = 6000 + cycle * 1500 + (phase * 1500 + 47) / 48;
      signs_frame(&model, &in, &frame);
      draw(pixels, w, h, 120, &frame);
    }
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    printf("two-row cache scale=120 cycle=%d entries=%d bytes=%zu "
           "misses=%llu\n",
           cycle, stats.entries, stats.bytes, (unsigned long long)stats.misses);
    if (cycle)
      TEST_ASSERT(stats.misses == 0);
    TEST_ASSERT(stats.bytes <= stats.byte_limit);
    TEST_ASSERT(stats.entries <= stats.slot_limit);
  }
  free(pixels);
}
int main(void) {
  // Shapes only: glyph rasterisation differs between FreeType builds.
  legacy_pixels();
  TEST_ASSERT(text_init("sans") == 0);
  row_layout();
  priority_and_transition();
  layers_and_hits();
  post_and_clearance();
  cache_two_rows();
  sign_draw_cleanup();
  text_cleanup();
  return 0;
}

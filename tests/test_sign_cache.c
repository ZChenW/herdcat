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

#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "test_helpers.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define W      700
#define H      420
#define PHASES 48
#define PERIOD 1500

static void draw(uint8_t *pixels, int w, int h, int scale,
                 const sign_frame_t *frame) {
  memset(pixels, 0, (size_t)w * (size_t)h * 4);
  sign_draw(pixels, w, h, scale, frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, w, h, scale, frame, SIGN_DRAW_OVER);
}
static void waiting_input(signs_t *model, sign_input_t *in, sign_frame_t *frame,
                          agent_session_view_t *session, sign_style_t style) {
  *session = (agent_session_view_t){.key = 1, .state = AGENT_STATE_WAITING};
  snprintf(session->agent, sizeof(session->agent), "claude");
  snprintf(session->name, sizeof(session->name), "测量");
  *in = (sign_input_t){.sessions = session,
                       .count = 1,
                       .style = style,
                       .animations = SIGN_ANIM_FULL,
                       .cat_x = 100,
                       .cat_y = 170,
                       .cat_height = 110};
  for (int64_t now = 0; now < 1000; now += 17) {
    in->now_ms = now;
    signs_frame(model, in, frame);
  }
}
static void profile(sign_style_t style, int scale) {
  text_set_scale(scale);
  signs_t model = {0};
  sign_input_t in;
  sign_frame_t frame;
  agent_session_view_t session;
  waiting_input(&model, &in, &frame, &session, style);
  int w = W * scale / 120, h = H * scale / 120;
  uint8_t *pixels = calloc((size_t)w * (size_t)h, 4);
  TEST_ASSERT(pixels);
  sign_draw_cleanup();
  for (int cycle = 0; cycle < 2; cycle++) {
    sign_draw_cache_reset_stats();
    for (int phase = 0; phase < PHASES; phase++) {
      in.now_ms =
          6000 + cycle * PERIOD + (phase * PERIOD + PHASES - 1) / PHASES;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(!frame.transitioning);
      sign_draw_cache_stats_t before = sign_draw_cache_stats();
      draw(pixels, w, h, scale, &frame);
      sign_draw_cache_stats_t after = sign_draw_cache_stats();
      printf("{\"style\":\"%s\",\"scale_120\":%d,\"cycle\":%d,"
             "\"phase\":%d,\"hits\":%llu,\"misses\":%llu,"
             "\"bytes\":%zu,\"entries\":%d}\n",
             style == SIGN_STYLE_FAN ? "fan" : "post", scale, cycle, phase,
             (unsigned long long)(after.hits - before.hits),
             (unsigned long long)(after.misses - before.misses), after.bytes,
             after.entries);
    }
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    printf("{\"summary\":true,\"style\":\"%s\",\"scale_120\":%d,"
           "\"cycle\":%d,\"hits\":%llu,\"misses\":%llu,"
           "\"bytes\":%zu,\"entries\":%d,\"byte_limit\":%zu,"
           "\"slot_limit\":%d}\n",
           style == SIGN_STYLE_FAN ? "fan" : "post", scale, cycle,
           (unsigned long long)stats.hits, (unsigned long long)stats.misses,
           stats.bytes, stats.entries, stats.byte_limit, stats.slot_limit);
  }
  free(pixels);
  sign_draw_cleanup();
}
static void reuse_phase(sign_style_t style, int scale, bool neighbors) {
  text_set_scale(scale);
  signs_t model = {0};
  sign_input_t in;
  sign_frame_t frame;
  agent_session_view_t session;
  waiting_input(&model, &in, &frame, &session, style);
  agent_session_view_t sessions[5];
  if (neighbors) {
    sessions[0] = session;
    for (int i = 1; i < 5; i++) {
      sessions[i] = (agent_session_view_t){.key = (uint64_t)i + 1,
                                           .order = (uint64_t)i,
                                           .state = AGENT_STATE_DONE};
      snprintf(sessions[i].agent, sizeof(sessions[i].agent), "%s",
               i % 2 ? "codex" : "custom");
      snprintf(sessions[i].name, sizeof(sessions[i].name), "neighbor %d", i);
    }
    in.sessions = sessions;
    in.count = 5;
    model = (signs_t){0};
    for (int64_t now = 0; now < 1000; now += 17) {
      in.now_ms = now;
      signs_frame(&model, &in, &frame);
    }
  }
  int w = W * scale / 120, h = H * scale / 120;
  size_t bytes = (size_t)w * (size_t)h * 4;
  uint8_t *cached = calloc(bytes, 1), *plain = calloc(bytes, 1);
  TEST_ASSERT(cached && plain);
  sign_draw_cleanup();
  for (int phase = 0; phase < PHASES; phase++) {
    in.now_ms = 6000 + (phase * PERIOD + PHASES - 1) / PHASES;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(!frame.transitioning);
    draw(cached, w, h, scale, &frame);
    sign_draw_cache_reset_stats();
    draw(cached, w, h, scale, &frame);
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    TEST_ASSERT(stats.hits > 0 && stats.misses == 0);
    sign_draw_cache_disable(true);
    draw(plain, w, h, scale, &frame);
    sign_draw_cache_disable(false);
    TEST_ASSERT(!memcmp(cached, plain, bytes));
  }
  // The entire next period must fit, not just two identical draws.
  sign_draw_cache_reset_stats();
  for (int phase = 0; phase < PHASES; phase++) {
    in.now_ms = 7500 + (phase * PERIOD + PHASES - 1) / PHASES;
    signs_frame(&model, &in, &frame);
    draw(cached, w, h, scale, &frame);
  }
  sign_draw_cache_stats_t stats = sign_draw_cache_stats();
  TEST_ASSERT(stats.hits > 0 && stats.misses == 0);
  TEST_ASSERT(stats.bytes <= stats.byte_limit);
  TEST_ASSERT(stats.entries <= stats.slot_limit);
  free(cached);
  free(plain);
  sign_draw_cleanup();
}
static void cache_limits(void) {
  static uint8_t pixels[W * H * 4];
  sign_frame_t frame = {.bounds_w = W, .bounds_h = H, .shape_count = 1};
  sign_draw_cleanup();
  // Large bitmaps reach the byte budget before filling the slot array.
  for (int i = 0; i < 72; i++) {
    frame.shapes[0] = (sign_shape_t){.x = 10,
                                     .y = 10,
                                     .w = 500,
                                     .h = 500,
                                     .fill = 0xff100000U + (uint32_t)i};
    draw(pixels, W, H, 120, &frame);
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    TEST_ASSERT(stats.bytes <= stats.byte_limit);
    TEST_ASSERT(stats.entries <= stats.slot_limit);
  }
  sign_draw_cache_reset_stats();
  draw(pixels, W, H, 120, &frame);
  TEST_ASSERT(sign_draw_cache_stats().hits == 1);
  frame.shapes[0].fill = 0xff100000U;
  draw(pixels, W, H, 120, &frame);
  TEST_ASSERT(sign_draw_cache_stats().misses == 1);
  // Tiny bitmaps instead reach the slot cap and evict the oldest entry.
  sign_draw_cleanup();
  for (int i = 0; i < sign_draw_cache_stats().slot_limit + 28; i++) {
    frame.shapes[0] = (sign_shape_t){
        .x = 10, .y = 10, .w = 3, .h = 3, .fill = 0xff100000U + (uint32_t)i};
    draw(pixels, W, H, 120, &frame);
  }
  sign_draw_cache_stats_t stats = sign_draw_cache_stats();
  TEST_ASSERT(stats.entries == stats.slot_limit);
  TEST_ASSERT(stats.bytes < stats.byte_limit);
  sign_draw_cache_reset_stats();
  draw(pixels, W, H, 120, &frame);
  TEST_ASSERT(sign_draw_cache_stats().hits == 1);
  frame.shapes[0].fill = 0xff100000U;
  draw(pixels, W, H, 120, &frame);
  TEST_ASSERT(sign_draw_cache_stats().misses == 1);
  // Another scale adds its own bitmap and leaves the others alone; cleanup
  // releases their bytes.
  int held = sign_draw_cache_stats().entries;
  sign_draw_cache_reset_stats();
  draw(pixels, W, H, 150, &frame);
  TEST_ASSERT(sign_draw_cache_stats().misses == 1);
  TEST_ASSERT(sign_draw_cache_stats().entries == held);  // Full: one evicted.
  sign_draw_cleanup();
  TEST_ASSERT(sign_draw_cache_stats().entries == 0);
  TEST_ASSERT(sign_draw_cache_stats().bytes == 0);
}
static void expire_phases(sign_style_t style, int scale) {
  signs_t model = {0};
  sign_input_t in;
  sign_frame_t frame;
  agent_session_view_t session;
  waiting_input(&model, &in, &frame, &session, style);
  int w = W * scale / 120, h = H * scale / 120;
  size_t bytes = (size_t)w * (size_t)h * 4;
  uint8_t *before = calloc(bytes, 1), *after = calloc(bytes, 1);
  TEST_ASSERT(before && after);
  sign_draw_cleanup();
  TEST_ASSERT(sign_draw_cache_timeout(0) == -1);
  for (int phase = 0; phase < PHASES; phase++) {
    in.now_ms = 6000 + (phase * PERIOD + PHASES - 1) / PHASES;
    signs_frame(&model, &in, &frame);
    draw(before, w, h, scale, &frame);
  }
  sign_draw_cache_stats_t full = sign_draw_cache_stats();
  TEST_ASSERT(full.phase_entries > 0 && full.phase_bytes > 0);
  // Keep a bitmap shared with a non-waiting shape, plus an ordinary shape.
  sign_frame_t stable = {.bounds_w = W, .bounds_h = H, .shape_count = 2};
  int shared = 0;
  while (shared < frame.shape_count && !frame.shapes[shared].pixel_snap)
    shared++;
  TEST_ASSERT(shared < frame.shape_count);
  stable.shapes[0] = frame.shapes[shared];
  TEST_ASSERT(stable.shapes[0].pixel_snap);
  stable.shapes[0].pixel_snap = false;
  double s = scale / 120.0;
  stable.shapes[0].x = round(stable.shapes[0].x * s) / s;
  stable.shapes[0].y = round(stable.shapes[0].y * s) / s;
  stable.shapes[0].origin_x = round(stable.shapes[0].origin_x * s) / s;
  stable.shapes[0].origin_y = round(stable.shapes[0].origin_y * s) / s;
  stable.shapes[1] =
      (sign_shape_t){.x = 10, .y = 10, .w = 20, .h = 20, .fill = 0xffabcdefU};
  draw(after, w, h, scale, &stable);
  TEST_ASSERT(sign_draw_cache_stats().phase_entries == full.phase_entries - 1);
  full = sign_draw_cache_stats();
  int retained = full.entries - full.phase_entries;
  TEST_ASSERT(retained >= 2);
  sign_draw_cache_update(true, 10000);
  TEST_ASSERT(sign_draw_cache_timeout(10000) == -1);
  sign_draw_cache_update(true, 200000);
  TEST_ASSERT(sign_draw_cache_timeout(200000) == -1);
  TEST_ASSERT(sign_draw_cache_stats().bytes == full.bytes);
  sign_draw_cache_update(false, 200000);
  TEST_ASSERT(sign_draw_cache_timeout(200000) == 60000);
  sign_draw_cache_update(false, 259999);
  TEST_ASSERT(sign_draw_cache_timeout(259999) == 1);
  TEST_ASSERT(sign_draw_cache_stats().bytes == full.bytes);
  // Another waiting sign cancels the old deadline, including on its boundary.
  sign_draw_cache_update(true, 260000);
  TEST_ASSERT(sign_draw_cache_timeout(260000) == -1);
  sign_draw_cache_update(false, 280000);
  sign_draw_cache_update(false, 339999);
  TEST_ASSERT(sign_draw_cache_timeout(339999) == 1);
  TEST_ASSERT(sign_draw_cache_stats().bytes == full.bytes);
  TEST_ASSERT(sign_draw_cache_timeout(340000) == 0);
  sign_draw_cache_update(false, 340000);
  sign_draw_cache_stats_t empty = sign_draw_cache_stats();
  TEST_ASSERT(empty.phase_entries == 0 && empty.phase_bytes == 0);
  TEST_ASSERT(empty.entries == retained);
  TEST_ASSERT(empty.bytes == full.bytes - full.phase_bytes);
  printf("{\"release\":true,\"style\":\"%s\",\"scale_120\":%d,"
         "\"before_bytes\":%zu,\"phase_bytes\":%zu,\"after_bytes\":%zu}\n",
         style == SIGN_STYLE_FAN ? "fan" : "post", scale, full.bytes,
         full.phase_bytes, empty.bytes);
  TEST_ASSERT(sign_draw_cache_timeout(340000) == -1);
  sign_draw_cache_update(false, 500000);
  TEST_ASSERT(sign_draw_cache_timeout(500000) == -1);
  sign_draw_cache_reset_stats();
  draw(after, w, h, scale, &stable);
  TEST_ASSERT(sign_draw_cache_stats().hits == 2);
  TEST_ASSERT(sign_draw_cache_stats().misses == 0);
  // A cold return to the same waiting phase paints exactly the old pixels.
  draw(before, w, h, scale, &frame);
  sign_draw_cache_update(false, 600000);
  sign_draw_cache_update(false, 660000);
  draw(after, w, h, scale, &frame);
  TEST_ASSERT(!memcmp(before, after, bytes));
  sign_draw_cache_update(false, 700000);
  // Two outputs with different scales are drawn in turn. Each keeps its
  // own bitmaps: going to the other scale and back must not rasterize again.
  int other = scale == 120 ? 150 : 120;
  draw(after, w, h, scale, &frame);
  draw(after, w, h, other, &frame);
  sign_draw_cache_reset_stats();
  draw(after, w, h, scale, &frame);
  draw(after, w, h, other, &frame);
  draw(after, w, h, scale, &frame);
  TEST_ASSERT(sign_draw_cache_stats().misses == 0);
  TEST_ASSERT(sign_draw_cache_stats().hits > 0);
  TEST_ASSERT(sign_draw_cache_timeout(700000) >= 0);
  sign_draw_cleanup();
  TEST_ASSERT(sign_draw_cache_timeout(700000) == -1);
  TEST_ASSERT(sign_draw_cache_stats().bytes == 0);
  free(before);
  free(after);
}
static void tag_cycle(sign_style_t style, int scale, bool report) {
  if (style != SIGN_STYLE_FAN)
    return;
  signs_t model = {0};
  sign_input_t in;
  sign_frame_t frame;
  agent_session_view_t session;
  waiting_input(&model, &in, &frame, &session, style);
  int w = W * scale / 120, h = H * scale / 120;
  size_t bytes = (size_t)w * (size_t)h * 4;
  uint8_t *cached = calloc(bytes, 1), *plain = calloc(bytes, 1);
  TEST_ASSERT(cached && plain);
  sign_draw_cleanup();
  double min_x = INFINITY, min_y = INFINITY;
  double max_x = -INFINITY, max_y = -INFINITY;
  for (int cycle = 0; cycle < 2; cycle++) {
    sign_draw_cache_reset_stats();
    for (int phase = 0; phase < PHASES; phase++) {
      in.now_ms =
          6000 + cycle * PERIOD + (phase * PERIOD + PHASES - 1) / PHASES;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(!frame.transitioning);
      // Isolate the nameplate background; glyphs use the separate text cache.
      int tag = -1;
      for (int i = 0; i < frame.text_count; i++)
        if (frame.texts[i].back >> 24)
          tag = i;
      TEST_ASSERT(tag >= 0);
      frame.texts[0] = frame.texts[tag];
      frame.text_count = 1;
      frame.shape_count = 0;
      min_x = fmin(min_x, frame.texts[0].x);
      max_x = fmax(max_x, frame.texts[0].x);
      min_y = fmin(min_y, frame.texts[0].anchor_y);
      max_y = fmax(max_y, frame.texts[0].anchor_y);
      draw(cached, w, h, scale, &frame);
    }
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    TEST_ASSERT(stats.hits + stats.misses == PHASES);
    TEST_ASSERT(stats.misses == (cycle ? 0 : 1));
    if (report)
      printf("{\"tag\":true,\"style\":\"%s\",\"scale_120\":%d,"
             "\"cycle\":%d,\"hits\":%llu,\"misses\":%llu,"
             "\"min_x\":%.3f,\"max_x\":%.3f,"
             "\"min_anchor_y\":%.3f,\"max_anchor_y\":%.3f}\n",
             style == SIGN_STYLE_FAN ? "fan" : "post", scale, cycle,
             (unsigned long long)stats.hits, (unsigned long long)stats.misses,
             min_x, max_x, min_y, max_y);
  }
  sign_draw_cache_disable(true);
  draw(plain, w, h, scale, &frame);
  sign_draw_cache_disable(false);
  TEST_ASSERT(!memcmp(cached, plain, bytes));
  free(cached);
  free(plain);
  sign_draw_cleanup();
}
static void mixed_cycle(bool report) {
  signs_t model = {0};
  agent_session_view_t sessions[2] = {
      {.key = 1, .state = AGENT_STATE_WAITING},
      {.key = 2, .order = 1, .state = AGENT_STATE_WORKING}
  };
  strcpy(sessions[0].agent, "manual");
  strcpy(sessions[0].name, "manual 0000");
  strcpy(sessions[1].agent, "claude");
  strcpy(sessions[1].name, "Claude 测量");
  sign_input_t in = {.sessions = sessions,
                     .count = 2,
                     .style = SIGN_STYLE_FAN,
                     .animations = SIGN_ANIM_FULL,
                     .cat_x = 100,
                     .cat_y = 170,
                     .cat_height = 110};
  sign_frame_t frame;
  for (in.now_ms = 0; in.now_ms < 2000; in.now_ms += 17)
    signs_frame(&model, &in, &frame);
  uint8_t *pixels = calloc((size_t)W * H, 4);
  TEST_ASSERT(pixels);
  sign_draw_cleanup();
  // Working dots repeat after lcm(180, 1100) = 9900 ms. Waiting phases
  // have all been visited within that time, even though their offset changes.
  for (int cycle = 0; cycle < 2; cycle++) {
    sign_draw_cache_reset_stats();
    for (int tick = 0; tick < 330; tick++) {
      in.now_ms = 12000 + cycle * 9900 + tick * 30;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(!frame.transitioning);
      draw(pixels, W, H, 120, &frame);
    }
    sign_draw_cache_stats_t stats = sign_draw_cache_stats();
    printf("{\"mixed\":true,\"cycle\":%d,\"hits\":%llu,"
           "\"misses\":%llu,\"entries\":%d,\"bytes\":%zu}\n",
           cycle, (unsigned long long)stats.hits,
           (unsigned long long)stats.misses, stats.entries, stats.bytes);
    if (cycle && !report)
      TEST_ASSERT(stats.misses == 0);
  }
  free(pixels);
  sign_draw_cleanup();
}
int main(int argc, char **argv) {
  bool profiling = argc == 2 && !strcmp(argv[1], "--profile");
  bool tags = argc == 2 && !strcmp(argv[1], "--profile-tag");
  bool mixed = argc == 2 && !strcmp(argv[1], "--profile-mixed");
  TEST_ASSERT(argc == 1 || profiling || tags || mixed);
  TEST_ASSERT(text_init(tags ? "Noto Serif CJK TC" : NULL) == 0);
  const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST};
  const int scales[] = {120, 150, 180, 240};
  if (mixed) {
    mixed_cycle(true);
    text_cleanup();
    return 0;
  }
  for (size_t style = 0; style < sizeof(styles) / sizeof(styles[0]); style++)
    for (size_t scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++)
      if (tags)
        tag_cycle(styles[style], scales[scale], true);
      else if (profiling)
        profile(styles[style], scales[scale]);
      else
        reuse_phase(styles[style], scales[scale], false);
  if (!profiling && !tags) {
    for (size_t style = 0; style < sizeof(styles) / sizeof(styles[0]); style++)
      for (size_t scale = 0; scale < sizeof(scales) / sizeof(scales[0]);
           scale++) {
        reuse_phase(styles[style], scales[scale], true);
        expire_phases(styles[style], scales[scale]);
      }
    tag_cycle(SIGN_STYLE_FAN, 120, false);
    tag_cycle(SIGN_STYLE_POST, 120, false);
    cache_limits();
    mixed_cycle(false);
  }
  text_cleanup();
  return 0;
}

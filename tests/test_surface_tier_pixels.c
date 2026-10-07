// Freeze shapes before loading any font. Compare complete output canvases.
#include "graphics/sign_draw.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_signs.h"
#include "platform/overlay_vertical.h"
#include "platform/scale.h"
#include "platform/wayland.h"
#ifndef SURFACE_TIERS_BASELINE
#  include "platform/surface_tiers.h"
#  include "surface_tier_hashes.h"
#endif
#include "test_helpers.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint64_t scene(config_t config, uint32_t scale, int count, bool below,
                      bool typing, bool tiered) {
  int output_w = 1024, output_h = 1080, cat_w = 198;
  int old_h = overlay_signs_height(&config);
  int base = output_h - old_h + overlay_signs_resting_y(&config, old_h);
  int position = base - (below ? 50 : 450);
  int width = overlay_scaled_width(overlay_extent(&config, output_w).width,
                                   output_w, scale);
  int height = old_h;
  overlay_vertical_t vertical = overlay_place_vertical(
      &config, position, output_h, height,
      overlay_signs_resting_y(&config, height), false, SIGN_ABOVE);
#ifndef SURFACE_TIERS_BASELINE
  if (tiered) {
    int capacity = surface_tier_capacity(&config, count, false);
    surface_size_t size = surface_tier_size(&config, capacity, output_w, scale);
    width = size.width;
    height = size.height;
    vertical = surface_tier_vertical(&config, position, output_h, height, false,
                                     SIGN_ABOVE, scale);
  }
#else
  (void)tiered;
#endif
  overlay_placement_t horizontal =
      overlay_place(401, cat_w, width, output_w, scale);
  agent_session_view_t sessions[10] = {0};
  for (int i = 0; i < count; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = (agent_state_t)(i % AGENT_STATE_COUNT);
    sessions[i].unread = true;
    sessions[i].child_count = 3;
    strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
    strcpy(sessions[i].name, "repository");
  }
  sign_input_t input = {.sessions = sessions,
                        .count = (size_t)count,
                        .style = config.sign_style,
                        .idle = SIGN_IDLE_ALWAYS,
                        .animations = SIGN_ANIM_OFF,
                        .open = count > 0,
                        .has_hover = count > 0,
                        .hover_key = 1,
                        .cat_x = horizontal.cat_x_in_surface,
                        .cat_y = vertical.cat_y_in_surface,
                        .cat_height = 110,
                        .surface_width = width,
                        .surface_height = height,
                        .orientation = vertical.orientation,
                        .typing = typing,
                        .typing_key = 1,
                        .typing_until = 200000,
                        .desk_snap = true,
                        .now_ms = 100000};
#ifndef SURFACE_TIERS_BASELINE
  if (tiered)
    input.surface_height =
        surface_tier_model_height(&config, vertical.orientation, height);
#endif
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &input, &frame);
  int w = scale_size_120(width, scale), h = scale_size_120(height, scale);
  uint8_t *pixels = calloc((size_t)w * (size_t)h, 4);
  TEST_ASSERT(pixels);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_OVER);
  int ox = scale_offset_120(horizontal.margin_x, scale);
  int oy = scale_offset_120(output_h - height - vertical.margin_y, scale);
  int ow = scale_size_120(output_w, scale),
      oh = scale_size_120(output_h, scale);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (int y = 0; y < oh; y++)
    for (int x = 0; x < ow; x++)
      for (int byte = 0; byte < 4; byte++) {
        uint8_t value =
            x >= ox && x < ox + w && y >= oy && y < oy + h
                ? pixels[((size_t)(y - oy) * (size_t)w + (size_t)(x - ox)) * 4 +
                         (size_t)byte]
                : 0;
        hash = (hash ^ value) * UINT64_C(1099511628211);
      }
  free(pixels);
  return hash;
}
#ifndef SURFACE_TIERS_BASELINE
static void ink_bounds(const sign_frame_t *source, int width, int height,
                       uint32_t scale, const sign_input_t *input) {
  sign_frame_t frame = *source;
  const int pad = 4;
  for (int i = 0; i < frame.shape_count; i++) {
    sign_shape_t *shape = &frame.shapes[i];
    shape->x += pad;
    shape->y += pad;
    shape->origin_x += pad;
    shape->origin_y += pad;
    shape->clip_x += pad;
    shape->clip_y += pad;
    shape->icon_center_x += pad;
    shape->icon_center_y += pad;
  }
  for (int i = 0; i < frame.text_count; i++) {
    frame.texts[i].x += pad;
    frame.texts[i].line_top += pad;
    frame.texts[i].clip_y += pad;
    frame.texts[i].anchor_y += pad;
  }
  frame.bounds_x += pad;
  frame.bounds_y += pad;
  int w = scale_size_120(width + 2 * pad, scale);
  int h = scale_size_120(height + 2 * pad, scale);
  int left = scale_offset_120(pad, scale);
  int right = left + scale_size_120(width, scale);
  int bottom = left + scale_size_120(height, scale);
  uint8_t *pixels = calloc((size_t)w * (size_t)h, 4);
  TEST_ASSERT(pixels);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_OVER);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (x < left || y < left || x >= right || y >= bottom) {
        if (pixels[((size_t)y * (size_t)w + (size_t)x) * 4 + 3])
          fprintf(stderr,
                  "out-of-canvas ink style=%d cat=%.0f count=%zu below=%d "
                  "scale=%u time=%" PRId64 " canvas=%dx%d cat_y=%.0f "
                  "pixel=%d,%d\n",
                  input->style, input->cat_height, input->count,
                  input->orientation == SIGN_BELOW, scale, input->now_ms, width,
                  height, input->cat_y, x - left, y - left);
        TEST_ASSERT(pixels[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] == 0);
      }
  free(pixels);
}
static void motion_bounds(void) {
  config_t config = {.cat_height = 110,
                     .overlay_height = 120,
                     .sign_max = 10,
                     .overlay_position = POSITION_BOTTOM};
  agent_session_view_t sessions[10] = {0};
  for (int i = 0; i < 10; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = AGENT_STATE_WAITING;
    sessions[i].unread = true;
    sessions[i].child_count = 3;
    strcpy(sessions[i].agent, "claude");
  }
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    const int cats[] = {110, 40, 60};
    const uint32_t scales[] = {120, 240, 150};
    for (size_t c = 0; c < sizeof(cats) / sizeof(*cats); c++) {
      config.cat_height = cats[c];
      config.overlay_height = c ? 50 : 120;
      for (size_t s = 0; s < sizeof(scales) / sizeof(*scales); s++) {
        uint32_t scale = scales[s];
        for (int count = 1; count <= 10; count++) {
          int capacity = surface_tier_capacity(&config, count, false);
          surface_size_t size =
              surface_tier_size(&config, capacity, 2560, scale);
          for (int below = 0; below < 2; below++) {
            int old_h = overlay_signs_height(&config);
            int base = 1080 - old_h + overlay_signs_resting_y(&config, old_h);
            overlay_vertical_t vertical =
                surface_tier_vertical(&config, base - (below ? 50 : 450), 1080,
                                      size.height, false, SIGN_ABOVE, scale);
            TEST_ASSERT(vertical.orientation ==
                        (below ? SIGN_BELOW : SIGN_ABOVE));
            signs_t model = {0};
            sign_input_t input = {
                .sessions = sessions,
                .count = (size_t)count,
                .style = config.sign_style,
                .orientation = below ? SIGN_BELOW : SIGN_ABOVE,
                .animations = SIGN_ANIM_FULL,
                .idle = SIGN_IDLE_ALWAYS,
                .open = true,
                .has_hover = true,
                .hover_key = 1,
                .cat_x = (size.width - config.cat_height * 500 / 277) / 2,
                .cat_y = vertical.cat_y_in_surface,
                .cat_height = config.cat_height,
                .surface_width = size.width,
                .surface_height = surface_tier_model_height(
                    &config, below ? SIGN_BELOW : SIGN_ABOVE, size.height),
                .typing_key = 2,
                .typing_until = 6000};
            sign_frame_t frame;
            for (int t = 0; t <= 8000; t += 16) {
              input.now_ms = t;
              input.typing = t >= 1600 && t < 4800;
              if (t >= 6400)
                input.count = 0;
              if (t == 3200)
                signs_focus_failed(&model, 1, t);
              signs_frame(&model, &input, &frame);
              if (frame.bounds_w > 0) {
                // Damage bounds include a one-pixel transparent outset. Check
                // actual alpha on a larger canvas instead of clipping the
                // oracle.
                if (frame.bounds_x < 0 || frame.bounds_y < 0 ||
                    frame.bounds_x + frame.bounds_w > size.width ||
                    frame.bounds_y + frame.bounds_h > size.height)
                  ink_bounds(&frame, size.width, size.height, scale, &input);
              }
            }
          }
        }
      }
    }
  }
}
#endif
int main(int argc, char **argv) {
#ifndef SURFACE_TIERS_BASELINE
  if (argc == 2 && !strcmp(argv[1], "motion")) {
    motion_bounds();
    sign_draw_cleanup();
    puts("Surface tier motion stays inside the canvas.");
    return 0;
  }
#else
  (void)argv;
#endif
  TEST_ASSERT(argc == 1);
  config_t config = {.cat_height = 110,
                     .overlay_height = 120,
                     .sign_max = 10,
                     .overlay_position = POSITION_BOTTOM};
  const uint32_t scales[] = {120, 240, 150};
  const int counts[] = {0, 1, 5, 10};
#ifndef SURFACE_TIERS_BASELINE
  size_t index = 0;
#endif
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int below = 0; below < 2; below++)
      for (size_t s = 0; s < sizeof(scales) / sizeof(*scales); s++)
        for (size_t n = 0; n < sizeof(counts) / sizeof(*counts); n++)
          for (int typing = 0; typing < 2; typing++) {
            uint64_t before = scene(config, scales[s], counts[n], below != 0,
                                    typing != 0, false);
#ifdef SURFACE_TIERS_BASELINE
            printf("UINT64_C(0x%016" PRIx64 "),\n", before);
#else
            uint64_t after = scene(config, scales[s], counts[n], below != 0,
                                   typing != 0, true);
            if (before != after)
              fprintf(stderr,
                      "pixel mismatch style=%d below=%d scale=%u count=%d "
                      "typing=%d\n",
                      style, below, scales[s], counts[n], typing);
            TEST_ASSERT(before == SURFACE_TIER_HASHES[index++]);
            TEST_ASSERT(after == before);
#endif
          }
  }
#ifndef SURFACE_TIERS_BASELINE
  motion_bounds();
#endif
  sign_draw_cleanup();
  puts("Surface tier output pixel hashes unchanged (96 scenes, no fonts).");
  return 0;
}

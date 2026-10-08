#include "graphics/sign_draw.h"
#include "platform/overlay_geometry.h"
#include "platform/overlay_signs.h"
#include "platform/scale.h"
#include "platform/surface_tiers.h"
#include "test_helpers.h"

#include <stdlib.h>
#include <string.h>

static uint64_t scene(config_t config, int count, uint32_t scale, bool below,
                      bool typing, bool rest) {
  int tier = surface_tier_capacity(&config, count, false);
  if (rest)
    tier |= SURFACE_TIER_REST;
  surface_size_t size = surface_tier_size(&config, tier, 1024, scale);
  int full = overlay_signs_height(&config);
  int base = 1080 - full + overlay_signs_resting_y(&config, full);
  overlay_vertical_t v =
      surface_tier_vertical(&config, base - (below ? 30 : 450), 1080,
                            size.height, false, SIGN_ABOVE, scale, tier);
  int cat_w = config.cat_height * 500 / 277;
  overlay_placement_t x = overlay_place(401, cat_w, size.width, 1024, scale);
  agent_session_view_t sessions[10] = {0};
  for (int i = 0; i < count; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = config.sign_style == SIGN_STYLE_FAN && i % 3 == 0
                            ? AGENT_STATE_ERROR
                            : AGENT_STATE_WORKING;
    sessions[i].child_count = 3;
    sessions[i].unread = true;
    strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
  }
  sign_input_t in = {.style = config.sign_style,
                     .sessions = sessions,
                     .count = (size_t)count,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .cat_height = config.cat_height,
                     .cat_x = x.cat_x_in_surface,
                     .cat_y = v.cat_y_in_surface,
                     .orientation = v.orientation,
                     .surface_width = size.width,
                     .surface_height = surface_tier_model_height(
                         &config, v.orientation, size.height),
                     .typing = typing,
                     .typing_key = 2,
                     .desk_snap = true,
                     .now_ms = 2000};
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &in, &frame);
  int w = scale_size_120(size.width, scale),
      h = scale_size_120(size.height, scale);
  uint8_t *pixels = calloc((size_t)w * (size_t)h, 4);
  TEST_ASSERT(pixels);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_UNDER);
  sign_draw(pixels, w, h, (int)scale, &frame, SIGN_DRAW_OVER);
  int ox = scale_offset_120(x.margin_x, scale);
  int oy = scale_offset_120(1080 - size.height - v.margin_y, scale);
  int ow = scale_size_120(1024, scale), oh = scale_size_120(1080, scale);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (int y = 0; y < oh; y++)
    for (int px = 0; px < ow; px++)
      for (int byte = 0; byte < 4; byte++) {
        uint8_t value =
            px >= ox && px < ox + w && y >= oy && y < oy + h
                ? pixels[((size_t)(y - oy) * (size_t)w + (size_t)(px - ox)) *
                             4 +
                         (size_t)byte]
                : 0;
        hash = (hash ^ value) * UINT64_C(1099511628211);
      }
  free(pixels);
  return hash;
}
int main(void) {
  config_t config = {.cat_height = 110,
                     .overlay_height = 120,
                     .sign_max = 10,
                     .overlay_position = POSITION_BOTTOM};
  const uint32_t scales[] = {120, 150, 180, 240};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int below = 0; below < 2; below++)
      for (unsigned s = 0; s < sizeof(scales) / sizeof(*scales); s++)
        for (int count = 1; count <= 10; count++)
          for (int typing = 0; typing < 2; typing++) {
            uint64_t a =
                scene(config, count, scales[s], below != 0, typing != 0, false);
            uint64_t b =
                scene(config, count, scales[s], below != 0, typing != 0, true);
            if (a != b)
              fprintf(stderr,
                      "rest ink differs style=%d below=%d scale=%u count=%d "
                      "typing=%d\n",
                      style, below, scales[s], count, typing);
            TEST_ASSERT(a == b);
          }
  }
  sign_draw_cleanup();
  puts("Resting surfaces preserve all output-space no-font pixels.");
  return 0;
}

#include "graphics/animation.h"
#include "graphics/font_panel.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/input.h"
#include "platform/overlay_geometry.h"
#include "platform/scale.h"
#include "platform/wayland.h"
#include "test_helpers.h"

#include <stdlib.h>
#include <string.h>

atomic_uint *pending_paws;
void wayland_request_current_redraw(void) {}
void wayland_request_redraw(void) {}
int64_t input_timestamp(void) {
  return 0;
}

static void paint(uint8_t *pixels, int width, int height, int scale,
                  const sign_frame_t *frame, int cat_x, int cat_y) {
  sign_draw(pixels, width, height, scale, frame, SIGN_DRAW_UNDER);
  const cached_frame_t *cat = &anim_cached_frames[0];
  blit_cached_frame(pixels, width, height, cat->data, cat->width, cat->height,
                    scale_offset_120(cat_x, (uint32_t)scale),
                    scale_offset_120(cat_y, (uint32_t)scale));
  sign_draw(pixels, width, height, scale, frame, SIGN_DRAW_OVER);
}
static void scene(config_t config, int scale, int output_width, int output_x,
                  int mode) {
  int cat_width = config.cat_height * CAT_IMAGE_WIDTH / CAT_IMAGE_HEIGHT;
  int surface_h = 300;
  int cat_y = 186;
  overlay_extent_t extent = overlay_extent(&config, output_width);
  int narrow_width =
      overlay_scaled_width(extent.width, output_width, (uint32_t)scale);
  overlay_placement_t place = overlay_place(output_x, cat_width, narrow_width,
                                            output_width, (uint32_t)scale);
  int full_w = scale_size_120(output_width, (uint32_t)scale);
  int small_w = scale_size_120(narrow_width, (uint32_t)scale);
  int h = scale_size_120(surface_h, (uint32_t)scale);
  int offset = scale_offset_120(place.margin_x, (uint32_t)scale);
  uint8_t *full = calloc((size_t)full_w * (size_t)h, 4);
  uint8_t *small = calloc((size_t)small_w * (size_t)h, 4);
  TEST_ASSERT(full && small);
  agent_session_view_t sessions[5] = {0};
  for (int i = 0; i < 5; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = (agent_state_t)i;
    sessions[i].unread = i >= AGENT_STATE_DONE;
    strcpy(sessions[i].agent, i % 2 ? "codex" : "claude");
    strcpy(sessions[i].name, "project with a long name");
  }
  sign_input_t in = {.sessions = sessions,
                     .count = 5,
                     .style = config.sign_style,
                     .theme = config.sign_theme,
                     .animations = SIGN_ANIM_OFF,
                     .idle = SIGN_IDLE_ALWAYS,
                     .open = true,
                     .has_hover = true,
                     .hover_key = 1,
                     .cat_x = output_x,
                     .surface_width = output_width,
                     .cat_y = cat_y,
                     .cat_height = config.cat_height,
                     .now_ms = 100000,
                     .menu = mode == 1,
                     .menu_font_hot = true,
                     .typing = mode == 2,
                     .typing_key = 2,
                     .typing_until = 900000,
                     .desk_snap = true};
  signs_t old = {0}, narrow = {0};
  sign_frame_t old_frame, new_frame;
  signs_frame(&old, &in, &old_frame);
  in.cat_x = place.cat_x_in_surface;
  in.surface_width = narrow_width;
  signs_frame(&narrow, &in, &new_frame);
  TEST_ASSERT(new_frame.hit_count == old_frame.hit_count);
  for (int i = 0; i < new_frame.hit_count; i++) {
    TEST_ASSERT(new_frame.hits[i].x + place.margin_x == old_frame.hits[i].x);
    TEST_ASSERT(new_frame.hits[i].y == old_frame.hits[i].y);
  }
  if (mode == 1) {
    drag_rect_t a = {new_frame.menu_card.x, new_frame.menu_card.y,
                     new_frame.menu_card.w, new_frame.menu_card.h};
    drag_rect_t absolute =
        overlay_card_rect(a, place.margin_x, 40, false, 1080, surface_h);
    font_panel_box_t box = {absolute.x, absolute.y, absolute.width,
                            absolute.height};
    font_panel_box_t old_box = {old_frame.menu_card.x,
                                1080 - 40 - surface_h + old_frame.menu_card.y,
                                old_frame.menu_card.w, old_frame.menu_card.h};
    font_panel_size_t panel = {340, 420}, screen = {output_width, 1080};
    double x1, y1, x2, y2;
    font_panel_place(&box, &panel, &screen, 10, &x1, &y1);
    font_panel_place(&old_box, &panel, &screen, 10, &x2, &y2);
    TEST_ASSERT(x1 == x2 && y1 == y2);
  }
  animation_cache_frames(scale_size_120(cat_width, (uint32_t)scale),
                         scale_size_120(config.cat_height, (uint32_t)scale),
                         config.mirror_x, 0, 1);
  paint(full, full_w, h, scale, &old_frame, output_x, cat_y);
  paint(small, small_w, h, scale, &new_frame, place.cat_x_in_surface, cat_y);
  // Compare the entire visible output, including transparent pixels outside
  // the narrow surface. This separately covers clipping at both output edges.
  size_t ink = 0;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < full_w; x++) {
      const uint8_t *a = full + ((size_t)y * (size_t)full_w + (size_t)x) * 4;
      uint8_t empty[4] = {0};
      const uint8_t *b =
          x >= offset && x < offset + small_w
              ? small + ((size_t)y * (size_t)small_w + (size_t)(x - offset)) * 4
              : empty;
      if (memcmp(a, b, 4)) {
        fprintf(stderr,
                "style=%d theme=%d mirror=%d scale=%d out=%d x=%d "
                "mode=%d mismatch %d,%d margin=%d width=%d\n",
                config.sign_style, config.sign_theme, config.mirror_x, scale,
                output_width, output_x, mode, x, y, place.margin_x,
                narrow_width);
        TEST_ASSERT(false);
      }
      ink += a[3] != 0;
    }
  }
  TEST_ASSERT(ink > 0);
  free(full);
  free(small);
}
static void moving_bounds(void) {
  config_t config = {.cat_height = 110, .sign_style = SIGN_STYLE_FAN};
  overlay_extent_t extent = overlay_extent(&config, 2560);
  agent_session_view_t sessions[5] = {0};
  for (int i = 0; i < 5; i++) {
    sessions[i].key = (uint64_t)i + 1;
    sessions[i].order = (uint64_t)i + 1;
    sessions[i].state = AGENT_STATE_WAITING;
    strcpy(sessions[i].name, "project");
  }
  signs_t model = {0};
  sign_input_t in = {.sessions = sessions,
                     .count = 5,
                     .style = SIGN_STYLE_FAN,
                     .animations = SIGN_ANIM_FULL,
                     .open = true,
                     .cat_x = extent.cat_x_in_surface,
                     .cat_y = 186,
                     .cat_height = 110,
                     .has_hover = true,
                     .hover_key = 1};
  sign_frame_t frame;
  for (int t = 0; t < 6000; t += 16) {
    in.now_ms = t;
    if (t == 1600) {
      agent_session_view_t swap = sessions[0];
      sessions[0] = sessions[4];
      sessions[4] = swap;
    }
    signs_frame(&model, &in, &frame);
    if (t == 3200)
      signs_focus_failed(&model, 1, t);
    TEST_ASSERT(frame.bounds_x >= 0);
    TEST_ASSERT(frame.bounds_x + frame.bounds_w <= extent.width);
  }
}
int main(void) {
  config_t config = {.cat_height = 110, .overlay_height = 120};
  TEST_ASSERT(animation_init(&config) == HERDCAT_SUCCESS);
  TEST_ASSERT(text_init("sans") == 0);
  const int scales[] = {120, 150, 180, 240};
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    config.sign_style = (sign_style_t)style;
    for (int theme = 0; theme < 2; theme++) {
      config.sign_theme = (sign_theme_t)theme;
      for (int mirror = 0; mirror < 2; mirror++) {
        config.mirror_x = mirror;
        for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
          for (int mode = 0; mode < 3; mode++) {
            scene(config, scales[s], 2560, 1181, mode);
            scene(config, scales[s], 2560, 0, mode);
            scene(config, scales[s], 2560, 2362, mode);
            scene(config, scales[s], 320, 61, mode);
          }
        }
      }
    }
  }
  // Odd output widths must retain the last column at a fractional scale.
  config.sign_style = SIGN_STYLE_POST;
  scene(config, 150, 2561, 2561 - 198, 0);
  scene(config, 180, 641, 641 - 198, 1);
  // Additional sizes exercise reload-style geometry changes and sign-off.
  const int heights[] = {40, 200};
  for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
    config.cat_height = heights[i];
    for (int style = SIGN_STYLE_OFF; style <= SIGN_STYLE_FAN; style++) {
      config.sign_style = (sign_style_t)style;
      int cat_width = heights[i] * CAT_IMAGE_WIDTH / CAT_IMAGE_HEIGHT;
      scene(config, 150, 1024, 0, 0);
      scene(config, 180, 1024, 1024 - cat_width, 0);
      scene(config, 120, 320, (320 - cat_width) / 2, 0);
    }
  }
  moving_bounds();
  sign_draw_cleanup();
  text_cleanup();
  animation_cleanup();
  return 0;
}

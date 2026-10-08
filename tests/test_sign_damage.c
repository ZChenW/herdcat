#include "graphics/animation.h"
#include "graphics/sign_damage.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/input.h"
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

static void paint(uint8_t *pixels, int w, int h, int scale,
                  const sign_frame_t *frame, pixel_rect_t clip) {
  clip = pixel_rect_clip(clip, w, h);
  for (int y = clip.y; y < clip.y + clip.h; y++)
    memset(pixels + ((size_t)y * w + clip.x) * 4, 0, (size_t)clip.w * 4);
  sign_draw_clip(pixels, w, h, scale, frame, SIGN_DRAW_UNDER, clip);
  const cached_frame_t *cat = &anim_cached_frames[0];
  blit_cached_frame_clip(pixels, w, h, cat->data, cat->width, cat->height,
                         100 * scale / 120, 220 * scale / 120, clip);
  sign_draw_clip(pixels, w, h, scale, frame, SIGN_DRAW_OVER, clip);
}
static void capture(const char *dir, const char *kind, sign_style_t style,
                    int scale, int phase, const uint8_t *pixels, int w, int h) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s-%s-%d-%02d.ppm", dir, kind,
           style == SIGN_STYLE_FAN ? "fan" : "post", scale, phase);
  FILE *file = fopen(path, "wb");
  TEST_ASSERT(file);
  fprintf(file, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    const uint8_t *p = pixels + (size_t)i * 4;
    for (int k = 2; k >= 0; k--)
      fputc(p[k] + (255 - p[3]), file);
  }
  TEST_ASSERT(fclose(file) == 0);
}
static void cycle(sign_style_t style, int scale, sign_orientation_t side,
                  int theme, const char *dir) {
  int w = 700 * scale / 120, h = 420 * scale / 120;
  size_t bytes = (size_t)w * h * 4;
  uint8_t *buffers[2] = {calloc(bytes, 1), calloc(bytes, 1)};
  uint8_t *reference = calloc(bytes, 1);
  TEST_ASSERT(buffers[0] && buffers[1] && reference);
  animation_cache_frames(198 * scale / 120, 110 * scale / 120, 0, 0, 1);
  signs_t model = {0};
  agent_session_view_t sessions[2] = {
      {.key = 1, .state = AGENT_STATE_WAITING},
      {.key = 2, .order = 1, .state = AGENT_STATE_DONE, .unread = true}
  };
  strcpy(sessions[0].agent, "claude");
  strcpy(sessions[1].agent, "codex");
  strcpy(sessions[0].name, "等待中的项目 project");
  strcpy(sessions[1].name, "unchanged neighbor");
  sign_input_t in = {.sessions = sessions,
                     .count = 2,
                     .style = style,
                     .animations = SIGN_ANIM_FULL,
                     .orientation = side,
                     .theme = theme,
                     .cat_x = 100,
                     .cat_y = 220,
                     .cat_height = 110,
                     .surface_width = 700,
                     .surface_height = 420,
                     .font_size = 13};
  sign_frame_t before, after;
  for (in.now_ms = 0; in.now_ms < 1000; in.now_ms += 17)
    signs_frame(&model, &in, &before);
  in.now_ms = 6000;
  signs_frame(&model, &in, &before);
  pixel_rect_t full = {0, 0, w, h};
  pixel_rect_t pending[2] = {{0}, {0}};
  for (int b = 0; b < 2; b++)
    paint(buffers[b], w, h, scale, &before, full);
  for (int phase = 1; phase <= 48; phase++) {
    in.now_ms = 6000 + (phase * 1500 + 47) / 48;
    signs_frame(&model, &in, &after);
    TEST_ASSERT(!after.transitioning);
    pixel_rect_t logical = sign_damage(&before, &after);
    pixel_rect_t damage = {logical.x * scale / 120, logical.y * scale / 120,
                           (logical.w * scale + 119) / 120 + 1,
                           (logical.h * scale + 119) / 120 + 1};
    if (logical.w <= 0 || logical.h <= 0)
      damage = (pixel_rect_t){0};
    damage = pixel_rect_clip(damage, w, h);
    for (int b = 0; b < 2; b++)
      pending[b] = pixel_rect_union(pending[b], damage);
    // Unequal release cadence exercises accumulated damage on an older
    // buffer, rather than repainting only the immediately previous frame.
    int selected = phase % 5 ? 0 : 1;
    paint(buffers[selected], w, h, scale, &after, pending[selected]);
    pending[selected] = (pixel_rect_t){0};
    sign_draw_cache_disable(true);
    paint(reference, w, h, scale, &after, full);
    sign_draw_cache_disable(false);
    if (memcmp(buffers[selected], reference, bytes)) {
      fprintf(stderr,
              "damage mismatch style=%d scale=%d side=%d theme=%d "
              "phase=%d\n",
              style, scale, side, theme, phase);
      TEST_ASSERT(0);
    }
    if (dir && side == SIGN_ABOVE && theme == 0 && scale != 150) {
      capture(dir, "before", style, scale, phase - 1, reference, w, h);
      capture(dir, "after", style, scale, phase - 1, buffers[selected], w, h);
    }
    before = after;
  }
  free(buffers[0]);
  free(buffers[1]);
  free(reference);
  sign_draw_cleanup();
}
int main(int argc, char **argv) {
  config_t config = {.cat_height = 110, .overlay_height = 120};
  TEST_ASSERT(animation_init(&config) == HERDCAT_SUCCESS);
  const char *dir = argc == 2 ? argv[1] : NULL;
  const int scales[] = {120, 150, 240};
  const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST};
  for (int fonts = 0; fonts < 2; fonts++) {
    if (fonts)
      TEST_ASSERT(text_init("") == 0);
    for (size_t i = 0; i < 2; i++)
      for (size_t j = 0; j < 3; j++) {
        text_set_scale(scales[j]);
        for (int side = SIGN_ABOVE; side <= SIGN_BELOW; side++)
          for (int theme = 0; theme < 2; theme++)
            cycle(styles[i], scales[j], (sign_orientation_t)side, theme,
                  fonts ? dir : NULL);
      }
    text_cleanup();
  }
  animation_cleanup();
  puts("sign damage: all phases match full repaint, with and without fonts");
  return 0;
}

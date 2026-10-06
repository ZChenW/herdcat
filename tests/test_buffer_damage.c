#include "graphics/animation.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/input.h"
#include "platform/scale.h"
#include "platform/shm_buffer.h"
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

static shm_buffer_t buffer_of(int w, int h) {
  shm_buffer_t buffer = {
      .width = w,
      .height = h,
      .size = (size_t)w * (size_t)h * 4,
      .dirty = {0, 0, w, h}
  };
  buffer.pixels = malloc(buffer.size);
  TEST_ASSERT(buffer.pixels);
  memset(buffer.pixels, 0xa5, buffer.size);
  return buffer;
}
static pixel_rect_t physical(int x, int y, int w, int h, int scale) {
  return (pixel_rect_t){scale_offset_120(x, (uint32_t)scale),
                        scale_offset_120(y, (uint32_t)scale),
                        scale_size_120(w, (uint32_t)scale),
                        scale_size_120(h, (uint32_t)scale)};
}
static uint8_t *cat_of(int w, int h, int frame) {
  uint8_t *cat = malloc((size_t)w * (size_t)h * 4);
  TEST_ASSERT(cat);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t *p = cat + ((size_t)y * (size_t)w + (size_t)x) * 4;
      p[3] = (uint8_t)(((x + y) % 3) * 127);
      for (int c = 0; c < 3; c++)
        p[c] = (uint8_t)((unsigned)(x + y + c + frame) % (p[3] + 1U));
    }
  }
  return cat;
}
static void paint(shm_buffer_t *buffer, const sign_frame_t *signs,
                  const uint8_t *cat, pixel_rect_t cat_box, int scale,
                  bool hidden, bool full) {
  if (full)
    shm_buffer_damage(buffer,
                      (pixel_rect_t){0, 0, buffer->width, buffer->height});
  pixel_rect_t clip = shm_buffer_begin_draw(buffer);
  TEST_ASSERT(buffer->dirty.w == 0 && buffer->dirty.h == 0);
  if (hidden)
    return;
  pixel_rect_t bar = {0, buffer->height / 2, buffer->width,
                      buffer->height - buffer->height / 2};
  shm_buffer_fill(buffer, pixel_rect_intersect(bar, clip), 0x73000000);
  if (full) {
    sign_draw(buffer->pixels, buffer->width, buffer->height, scale, signs,
              SIGN_DRAW_UNDER);
    blit_cached_frame(buffer->pixels, buffer->width, buffer->height, cat,
                      cat_box.w, cat_box.h, cat_box.x, cat_box.y);
    sign_draw(buffer->pixels, buffer->width, buffer->height, scale, signs,
              SIGN_DRAW_OVER);
  } else {
    sign_draw_clip(buffer->pixels, buffer->width, buffer->height, scale, signs,
                   SIGN_DRAW_UNDER, clip);
    blit_cached_frame_clip(buffer->pixels, buffer->width, buffer->height, cat,
                           cat_box.w, cat_box.h, cat_box.x, cat_box.y, clip);
    sign_draw_clip(buffer->pixels, buffer->width, buffer->height, scale, signs,
                   SIGN_DRAW_OVER, clip);
  }
}
static void rectangles(void) {
  pixel_rect_t clip =
      pixel_rect_clip((pixel_rect_t){-10, -20, 30, 50}, 100, 80);
  TEST_ASSERT(clip.x == 0 && clip.y == 0 && clip.w == 20 && clip.h == 30);
  clip = pixel_rect_clip((pixel_rect_t){INT_MAX - 2, 0, 100, 1}, INT_MAX, 80);
  TEST_ASSERT(clip.x == INT_MAX - 2 && clip.w == 2);
  clip = pixel_rect_clip((pixel_rect_t){INT_MIN, 0, INT_MAX, 1}, 100, 80);
  TEST_ASSERT(clip.w == 0);
  shm_buffer_t buffer = buffer_of(100, 80);
  shm_buffer_begin_draw(&buffer);
  buffer.busy = true;
  shm_buffer_damage(&buffer, (pixel_rect_t){5, 6, 7, 8});
  shm_buffer_damage(&buffer, (pixel_rect_t){40, 50, 200, 200});
  TEST_ASSERT(shm_buffer_begin_draw(&buffer).w == 0);
  TEST_ASSERT(buffer.dirty.x == 5 && buffer.dirty.y == 6);
  TEST_ASSERT(buffer.dirty.w == 95 && buffer.dirty.h == 74);
  buffer.busy = false;
  TEST_ASSERT(shm_buffer_begin_draw(&buffer).w == 95);
  free(buffer.pixels);
}
static void sentinel(sign_style_t style, int scale) {
  int w = scale_size_120(700, (uint32_t)scale);
  int h = scale_size_120(420, (uint32_t)scale);
  shm_buffer_t buffer = buffer_of(w, h), reference = buffer_of(w, h);
  agent_session_view_t session = {.key = 1, .state = AGENT_STATE_WAITING};
  strcpy(session.agent, "claude");
  strcpy(session.name, "测试项目");
  sign_input_t input = {.sessions = &session,
                        .count = 1,
                        .style = style,
                        .animations = SIGN_ANIM_OFF,
                        .open = true,
                        .cat_x = 180,
                        .cat_y = 280,
                        .cat_height = 80};
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, &input, &frame);
  pixel_rect_t cat_box = physical(180, 280, 144, 80, scale);
  uint8_t *cat = cat_of(cat_box.w, cat_box.h, 0);
  paint(&buffer, &frame, cat, cat_box, scale, false, true);
  free(cat);
  // The second frame repaints only this small interior cut of the cat.
  pixel_rect_t cuts[] = {
      {cat_box.x + 9, cat_box.y + 7, 35, 23},
      physical(frame.bounds_x, frame.bounds_y, frame.bounds_w / 2,
               frame.bounds_h, scale)
  };
  cat = cat_of(cat_box.w, cat_box.h, 1);
  paint(&reference, &frame, cat, cat_box, scale, false, true);
  for (size_t cut = 0; cut < sizeof(cuts) / sizeof(cuts[0]); cut++) {
    pixel_rect_t dirty = pixel_rect_clip(cuts[cut], w, h);
    memset(buffer.pixels, 0xa5, buffer.size);
    shm_buffer_damage(&buffer, dirty);
    paint(&buffer, &frame, cat, cat_box, scale, false, false);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t offset = ((size_t)y * (size_t)w + (size_t)x) * 4;
        bool inside = x >= dirty.x && x < dirty.x + dirty.w && y >= dirty.y &&
                      y < dirty.y + dirty.h;
        for (size_t c = 0; c < 4; c++)
          TEST_ASSERT(buffer.pixels[offset + c] ==
                      (inside ? reference.pixels[offset + c] : 0xa5));
      }
    }
  }
  free(cat);
  free(buffer.pixels);
  free(reference.pixels);
}
static void alternating(sign_style_t style, int scale) {
  int w = scale_size_120(700, (uint32_t)scale);
  int h = scale_size_120(420, (uint32_t)scale);
  shm_buffer_t buffers[2] = {buffer_of(w, h), buffer_of(w, h)};
  shm_buffer_t reference = buffer_of(w, h);
  agent_session_view_t sessions[3] = {
      {.key = 1, .order = 1, .state = AGENT_STATE_WAITING},
      {.key = 2, .order = 2, .state = AGENT_STATE_WORKING},
      {.key = 3, .order = 3, .state = AGENT_STATE_DONE, .unread = true}
  };
  const char *agents[] = {"claude", "codex", "opencode"};
  for (int i = 0; i < 3; i++) {
    strcpy(sessions[i].agent, agents[i]);
    strcpy(sessions[i].name, "测试项目 ABC with a long name");
  }
  signs_t model = {0};
  sign_input_t input = {.sessions = sessions,
                        .count = 3,
                        .style = style,
                        .animations = SIGN_ANIM_FULL,
                        .open = true,
                        .cat_height = 80,
                        .typing_key = 1,
                        .typing_until = 3000};
  strcpy(input.desk_name, "输入中的项目");
  strcpy(input.menu_font, "Noto Sans");
  pixel_rect_t previous = {0};
  for (int i = 0; i < 18; i++) {
    if (i == 11) {
      // Resized buffers must start fully dirty, even with no previous ink.
      free(buffers[0].pixels);
      free(buffers[1].pixels);
      free(reference.pixels);
      w += 3;
      h += 1;
      buffers[0] = buffer_of(w, h);
      buffers[1] = buffer_of(w, h);
      reference = buffer_of(w, h);
    }
    input.now_ms = i * 180;
    input.cat_x = 150 + (i % 4) * 25;
    input.cat_y = 280;
    input.typing = i >= 7 && i <= 10;
    input.menu = i >= 12 && i <= 14;
    input.has_hover = i % 2;
    input.hover_key = 1;
    bool hidden = i == 5;
    sign_frame_t frame;
    signs_frame(&model, &input, &frame);
    pixel_rect_t cat_box = physical(
        (int)input.cat_x, (int)(input.cat_y - frame.cat_lift), 144, 80, scale);
    pixel_rect_t covered =
        pixel_rect_clip(physical(frame.bounds_x, frame.bounds_y, frame.bounds_w,
                                 frame.bounds_h, scale),
                        w, h);
    covered = pixel_rect_union(covered, pixel_rect_clip(cat_box, w, h));
    pixel_rect_t damage = pixel_rect_union(previous, covered);
    if (hidden || i == 6 || i == 11)
      damage = (pixel_rect_t){0, 0, w, h};
    for (int b = 0; b < 2; b++)
      shm_buffer_damage(&buffers[b], damage);
    previous = covered;
    uint8_t *cat = cat_of(cat_box.w, cat_box.h, i);
    int b = i % 6 < 3 ? i % 2 : 0;
    paint(&buffers[b], &frame, cat, cat_box, scale, hidden, false);
    paint(&reference, &frame, cat, cat_box, scale, hidden, true);
    if (memcmp(buffers[b].pixels, reference.pixels, reference.size)) {
      fprintf(stderr, "buffer mismatch: style=%d scale=%d frame=%d\n", style,
              scale, i);
      TEST_ASSERT(0);
    }
    free(cat);
  }
  free(buffers[0].pixels);
  free(buffers[1].pixels);
  free(reference.pixels);
}
int main(void) {
  TEST_ASSERT(text_init("") == 0);
  rectangles();
  const int scales[] = {120, 150, 180, 240};
  for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
    const sign_style_t styles[] = {SIGN_STYLE_FAN, SIGN_STYLE_POST,
                                   SIGN_STYLE_OFF};
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
      sign_style_t style = styles[i];
      sentinel(style, scales[s]);
      alternating(style, scales[s]);
      printf("style=%d scale=%d: sentinel and 18-frame comparison passed\n",
             style, scales[s]);
    }
  }
  sign_draw_cleanup();
  text_cleanup();
  puts("buffer damage: sentinel and alternating full-repaint comparisons "
       "passed");
  return 0;
}

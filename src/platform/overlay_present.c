#define _GNU_SOURCE
#include "config/config.h"
#include "core/herdcat.h"
#include "platform/wayland.h"
#include "utils/error.h"
#include "zwlr-layer-shell-v1-client-protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "graphics/animation.h"
#include "graphics/pixel_rect.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/drag.h"
#include "platform/focus.h"
#include "platform/font_panel.h"
#include "platform/fullscreen.h"
#include "platform/input.h"
#include "platform/outputs.h"
#include "platform/overlay_signs.h"
#include "platform/scale.h"
#include "platform/shm_buffer.h"
#include "viewporter-client-protocol.h"
#include "wlr-foreign-toplevel-management-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include "overlay_internal.h"

#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>

struct wl_callback *sign_frames[MAX_OUTPUTS];

static void update_input_region(overlay_t *overlay, bool invisible) {
  struct wl_region *region = wl_compositor_create_region(compositor);
  overlay->accepts_pointer =
      overlay->configured && overlay->config.cat_draggable && !invisible;
  if (!overlay->accepts_pointer && pointer_overlay == overlay) {
    finish_drag();
    overlay_signs_leave();
    pointer_overlay = NULL;
  }
  if (overlay->accepts_pointer) {
    overlay_signs_rect_t rects[OVERLAY_SIGNS_REGION_LIMIT];
    int count = overlay_signs_regions(
        (size_t)(overlay - overlays), &overlay->config, overlay->cat_x,
        cat_width(overlay), overlay->height, rects, OVERLAY_SIGNS_REGION_LIMIT);
    for (int i = 0; i < count; i++) {
      pixel_rect_t clipped = pixel_rect_clip(
          (pixel_rect_t){rects[i].x, rects[i].y, rects[i].w, rects[i].h},
          overlay->width, overlay->height);
      if (clipped.w > 0 && clipped.h > 0) {
        wl_region_add(region, clipped.x, clipped.y, clipped.w, clipped.h);
      }
    }
  }
  wl_surface_set_input_region(overlay->surface, region);
  wl_region_destroy(region);
}
static int clamp_offset(int64_t value) {
  if (value < INT_MIN) {
    return INT_MIN;
  }
  if (value > INT_MAX) {
    return INT_MAX;
  }
  return (int)value;
}
static void sign_frame_done(void *data, struct wl_callback *callback,
                            uint32_t time) {
  (void)time;
  overlay_t *overlay = data;
  size_t index = (size_t)(overlay - overlays);
  wl_callback_destroy(callback);
  if (index < MAX_OUTPUTS && sign_frames[index] == callback) {
    sign_frames[index] = NULL;
    overlay_signs_frame_wait(index, false);
  }
  overlay->redraw = true;
}
static const struct wl_callback_listener SIGN_FRAME_LISTENER = {
    .done = sign_frame_done};
static void arm_sign_frame(overlay_t *overlay) {
  size_t index = (size_t)(overlay - overlays);
  if (index >= MAX_OUTPUTS || sign_frames[index]) {
    return;
  }
  struct wl_callback *callback = wl_surface_frame(overlay->surface);
  if (!callback) {
    return;
  }
  sign_frames[index] = callback;
  overlay_signs_frame_wait(index, true);
  wl_callback_add_listener(callback, &SIGN_FRAME_LISTENER, overlay);
}
static void fill_bar(overlay_t *overlay, shm_buffer_t *buffer,
                     pixel_rect_t clip) {
  const config_t *config = &overlay->config;
  int bar = config->overlay_height;
  if (bar <= 0 || overlay->height <= 0 || overlay->physical_width <= 0 ||
      overlay->physical_height <= 0 || config->overlay_opacity <= 0) {
    return;
  }
  if (bar > overlay->height) {
    bar = overlay->height;
  }
  int y = config->overlay_position == POSITION_TOP ? 0 : overlay->height - bar;
  int py = scale_offset_120(y, overlay->scale);
  int ph = scale_size_120(bar, overlay->scale);
  if (py < 0) {
    ph += py;
    py = 0;
  }
  if (ph <= 0 || py >= overlay->physical_height) {
    return;
  }
  if (py > overlay->physical_height - ph) {
    ph = overlay->physical_height - py;
  }
  uint32_t color = (uint32_t)config->overlay_opacity << 24;
  shm_buffer_fill(buffer,
                  pixel_rect_intersect(
                      clip, (pixel_rect_t){0, py, overlay->physical_width, ph}),
                  color);
}
static pixel_rect_t damage_box(overlay_t *overlay, int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (w <= 0 || h <= 0 || x >= overlay->width || y >= overlay->height) {
    return (pixel_rect_t){0};
  }
  if (x > overlay->width - w) {
    w = overlay->width - x;
  }
  if (y > overlay->height - h) {
    h = overlay->height - y;
  }
  int px = scale_offset_120(x, overlay->scale);
  int py = scale_offset_120(y, overlay->scale);
  int pw = scale_size_120(w, overlay->scale);
  int ph = scale_size_120(h, overlay->scale);
  if (px < 0) {
    pw += px;
    px = 0;
  }
  if (py < 0) {
    ph += py;
    py = 0;
  }
  if (pw <= 0 || ph <= 0 || px >= overlay->physical_width ||
      py >= overlay->physical_height) {
    return (pixel_rect_t){0};
  }
  if (px > overlay->physical_width - pw) {
    pw = overlay->physical_width - px;
  }
  if (py > overlay->physical_height - ph) {
    ph = overlay->physical_height - py;
  }
  return (pixel_rect_t){px, py, pw, ph};
}
static pixel_rect_t frame_damage(overlay_t *overlay, bool invisible) {
  size_t index = (size_t)(overlay - overlays);
  overlay_signs_step_t last = overlay_signs_last(index);
  bool full = invisible || overlay->damage_all || last.damage_full ||
              last.damage_w <= 0 || last.damage_h <= 0;
  if (full) {
    return (pixel_rect_t){0, 0, overlay->physical_width,
                          overlay->physical_height};
  }
  return damage_box(overlay, last.damage_x, last.damage_y, last.damage_w,
                    last.damage_h);
}
void draw_bar(void) {
  overlay_t *overlay = active;
  if (!overlay || !overlay->configured || overlay->resize) {
    return;
  }
  bool invisible = overlay_hidden(overlay);
  pixel_rect_t damage = frame_damage(overlay, invisible);
  overlay->pending_damage = pixel_rect_union(overlay->pending_damage, damage);
  // A busy buffer also accumulates all changes until it is released. Keep
  // damage when both buffers are busy and a draw must be retried.
  for (size_t i = 0; i < 2; i++)
    shm_buffer_damage(overlay->buffers[i], damage);
  shm_buffer_t *buffer = NULL;
  for (size_t i = 0; i < 2; i++) {
    if (overlay->buffers[i] && !overlay->buffers[i]->busy) {
      buffer = overlay->buffers[i];
      break;
    }
  }
  if (!buffer) {
    overlay->redraw = true;
    return;
  }
  config_t *config = &overlay->config;
  size_t index = (size_t)(overlay - overlays);
  const sign_frame_t *signs = overlay_signs_frame(index);
  pixel_rect_t clip = shm_buffer_begin_draw(buffer);
  if (!invisible) {
    fill_bar(overlay, buffer, clip);
    if (signs) {
      sign_draw_clip(buffer->pixels, overlay->physical_width,
                     overlay->physical_height, (int)overlay->scale, signs,
                     SIGN_DRAW_UNDER, clip);
    }
    cached_frame_t *frame = &anim_cached_frames[anim_index];
    int64_t x = scale_offset_120(overlay->cat_x, overlay->scale);
    int logical_y = overlay_signs_cat_y_at(index, config, overlay->height);
    int64_t y = scale_offset_120(logical_y, overlay->scale);
    if (frame->data) {
      blit_cached_frame_clip(buffer->pixels, overlay->physical_width,
                             overlay->physical_height, frame->data,
                             frame->width, frame->height, clamp_offset(x),
                             clamp_offset(y), clip);
    }
    if (signs) {
      sign_draw_clip(buffer->pixels, overlay->physical_width,
                     overlay->physical_height, (int)overlay->scale, signs,
                     SIGN_DRAW_OVER, clip);
    }
  }
  set_margin(overlay);
  update_input_region(overlay, invisible);
  buffer->busy = true;
  wl_surface_attach(overlay->surface, buffer->object, 0, 0);
  // The compositor sees changes since the last commit, not the older
  // accumulated repaint rectangle of the selected buffer.
  damage = overlay->pending_damage;
  wl_surface_damage_buffer(overlay->surface, damage.x, damage.y, damage.w,
                           damage.h);
  overlay->pending_damage = (pixel_rect_t){0};
  overlay->damage_all = false;
  if (!invisible && overlay_signs_last(index).frame) {
    arm_sign_frame(overlay);
  }
  wl_surface_commit(overlay->surface);
  overlay->redraw = false;
}

#define _GNU_SOURCE
#include "config/config.h"
#include "config/sign_options.h"
#include "core/herdcat.h"
#include "graphics/signs.h"
#include "utils/error.h"
#include "zwlr-layer-shell-v1-client-protocol.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "platform/drag.h"
#include "platform/focus.h"
#include "platform/font_panel.h"
#include "platform/overlay_signs.h"
#include "viewporter-client-protocol.h"
#include "wlr-foreign-toplevel-management-v1-client-protocol.h"
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include "overlay_internal.h"

#include <limits.h>

struct wl_seat *seat;
static struct wl_pointer *pointer;
struct wp_cursor_shape_manager_v1 *cursor_manager;
struct wp_cursor_shape_device_v1 *cursor_device;
uint32_t seat_id, cursor_manager_id;
static uint32_t pointer_serial, last_cursor_serial, last_cursor_shape;
overlay_t *pointer_overlay;
static bool dragging, drag_moved;
static double pointer_x, pointer_y, origin_x, origin_y;
static int origin_cat_x, origin_margin, origin_margin_x;
static int origin_margin_surface, origin_cat_y, origin_height;
static void drag_follow(overlay_t *overlay);
static struct wl_callback *drag_frame;

void cursor_shape(uint32_t shape) {
  if (cursor_device && (pointer_overlay || font_panel_surface_armed()) &&
      (pointer_serial != last_cursor_serial || shape != last_cursor_shape)) {
    wp_cursor_shape_device_v1_set_shape(cursor_device, pointer_serial, shape);
    last_cursor_serial = pointer_serial;
    last_cursor_shape = shape;
  }
}

void overlay_pointer_rebase(overlay_t *overlay, int dx, int dy) {
  if (pointer_overlay == overlay && !dragging) {
    pointer_x += dx;
    pointer_y += dy;
  }
  overlay_signs_rebase((size_t)(overlay - overlays), dx, dy);
}

void finish_drag(void) {
  if (dragging && drag_moved && pointer_overlay) {
    // Flush the latest press-relative motion even if its frame is pending.
    if (drag_frame) {
      wl_callback_destroy(drag_frame);
      drag_frame = NULL;
    }
    drag_follow(pointer_overlay);
    clamp_position(pointer_overlay);
    if (drag_position_save(pointer_overlay->name, pointer_overlay->output_x,
                           pointer_overlay->position_y) < 0) {
      herdcat_log_warning("Cannot save drag position for %s",
                          pointer_overlay->name);
    }
  }
  if (drag_frame) {
    wl_callback_destroy(drag_frame);
    drag_frame = NULL;
  }
  if (dragging && pointer_overlay)
    pointer_x += origin_margin_x - pointer_overlay->margin_x;
  if (dragging && pointer_overlay &&
      (pointer_overlay->orientation == SIGN_BELOW ||
       pointer_overlay->cat_y != origin_cat_y ||
       pointer_overlay->height != origin_height)) {
    // Rebase pointer coordinates after a new vertical placement during grab.
    bool top = pointer_overlay->config.overlay_position == POSITION_TOP;
    pointer_y += top ? origin_margin_surface - pointer_overlay->margin_y
                     : pointer_overlay->margin_y - origin_margin_surface +
                           pointer_overlay->height - origin_height;
  }
  dragging = drag_moved = false;
  cursor_shape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
}
static void drag_frame_done(void *data, struct wl_callback *callback,
                            uint32_t time);
static const struct wl_callback_listener DRAG_FRAME_LISTENER = {
    .done = drag_frame_done};
// The compositor reports pointer coordinates against the surface position at
// button press for the whole implicit grab, so they do not shift as the margin
// moves the surface. The margin is therefore absolute from the press state,
// and at most one is submitted per frame.
static void drag_follow(overlay_t *overlay) {
  if (drag_frame) {
    return;
  }
  int x, y;
  drag_follow_position(
      origin_margin_x + origin_cat_x, origin_margin, origin_x, origin_y,
      pointer_x, pointer_y, overlay->config.overlay_position == POSITION_TOP,
      overlay->config.screen_width, cat_width(overlay), overlay->output_height,
      overlay->config.sign_style != SIGN_STYLE_OFF &&
              overlay->config.overlay_opacity == 0 &&
              overlay->height <= overlay->output_height
          ? (overlay->config.overlay_position == POSITION_TOP
                 ? overlay->config.cat_height
                 : overlay_signs_height(&overlay->config) -
                       overlay_signs_resting_y(
                           &overlay->config,
                           overlay_signs_height(&overlay->config)))
          : overlay->height,
      &x, &y);
  if (x == overlay->output_x && y == overlay->position_y) {
    return;
  }
  int last_cat_x = overlay->cat_x;
  int last_cat_y = overlay->cat_y;
  sign_orientation_t last_orientation = overlay->orientation;
  overlay->output_x = x;
  overlay->position_y = y;
  clamp_position(overlay);
  drag_frame = wl_surface_frame(overlay->surface);
  wl_callback_add_listener(drag_frame, &DRAG_FRAME_LISTENER, NULL);
  if (last_cat_x != overlay->cat_x || last_cat_y != overlay->cat_y ||
      last_orientation != overlay->orientation || overlay->redraw) {
    // Rebuild signs, pixels and input together before committing an edge move.
    overlay->redraw = true;
  } else {
    set_margin(overlay);
    wl_surface_commit(overlay->surface);
  }
}
static void drag_frame_done(void *data, struct wl_callback *callback,
                            uint32_t time) {
  (void)data;
  (void)time;
  wl_callback_destroy(callback);
  drag_frame = NULL;
  if (dragging && drag_moved && pointer_overlay) {
    drag_follow(pointer_overlay);
  }
}
static void pointer_enter(void *data, struct wl_pointer *object,
                          uint32_t serial, struct wl_surface *target,
                          wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)object;
  finish_drag();
  pointer_serial = serial;
  pointer_x = wl_fixed_to_double(x);
  pointer_y = wl_fixed_to_double(y);
  pointer_overlay = NULL;
  if (font_panel_surface_enter(target, pointer_x, pointer_y)) {
    cursor_shape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
    return;
  }
  overlay_signs_leave();
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (overlays[i].surface == target && overlays[i].accepts_pointer &&
        !hidden) {
      pointer_overlay = &overlays[i];
      bool on_sign = overlay_signs_pointer(i, pointer_x, pointer_y);
      cursor_shape(on_sign ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                           : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
      break;
    }
  }
}
static void pointer_leave(void *data, struct wl_pointer *object,
                          uint32_t serial, struct wl_surface *target) {
  (void)data;
  (void)object;
  (void)serial;
  (void)target;
  finish_drag();
  overlay_signs_leave();
  pointer_overlay = NULL;
}
static void pointer_motion(void *data, struct wl_pointer *object, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)object;
  (void)time;
  pointer_x = wl_fixed_to_double(x);
  pointer_y = wl_fixed_to_double(y);
  if (font_panel_surface_motion(pointer_x, pointer_y))
    return;
  overlay_t *overlay = pointer_overlay;
  if (overlay && !dragging) {
    size_t index = (size_t)(overlay - overlays);
    bool on_sign = overlay_signs_pointer(index, pointer_x, pointer_y);
    cursor_shape(on_sign ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                         : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
  }
  if (!dragging || !overlay) {
    return;
  }
  if (!drag_moved) {
    if (!drag_exceeds_threshold(origin_x, origin_y, pointer_x, pointer_y)) {
      return;
    }
    drag_moved = true;
    overlay->has_position = true;
    cursor_shape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING);
  }
  drag_follow(overlay);
}
static void pointer_button(void *data, struct wl_pointer *object,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  (void)data;
  (void)object;
  (void)serial;
  (void)time;
  if (overlay_signs_button(button, state)) {
    return;
  }
  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    bool moved = drag_moved;
    size_t index = 0;
    pid_t pid = 0;
    uint64_t key = 0;
    bool click = overlay_signs_release(moved, &index, &pid, &key);
    finish_drag();
    if (click && index < MAX_OUTPUTS) {
      if (pid > 0 && focus_session_window(pid) < 0) {
        overlay_signs_fail(index, key, overlay_signs_now());
      } else if (pid > 0) {
        overlay_signs_arm_focus(index, key);
      }
      overlays[index].redraw = true;
    }
    if (pointer_overlay) {
      size_t hover = (size_t)(pointer_overlay - overlays);
      bool on_sign = overlay_signs_pointer(hover, pointer_x, pointer_y);
      cursor_shape(on_sign ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                           : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
    }
  } else if (pointer_overlay && pointer_overlay->accepts_pointer && !hidden) {
    finish_drag();
    bool on_sign = overlay_signs_press((size_t)(pointer_overlay - overlays));
    cursor_shape(on_sign ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                         : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
    dragging = !overlay_signs_blocks_drag();
    origin_x = pointer_x;
    origin_y = pointer_y;
    origin_cat_x = pointer_overlay->cat_x;
    origin_cat_y = pointer_overlay->cat_y;
    origin_height = pointer_overlay->height;
    origin_margin_x = pointer_overlay->margin_x;
    origin_margin = pointer_overlay->position_y;
    origin_margin_surface = pointer_overlay->margin_y;
  }
}
static void pointer_axis(void *data, struct wl_pointer *object, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
  (void)data;
  (void)object;
  (void)time;
  (void)axis;
  (void)value;
}
static void pointer_frame(void *data, struct wl_pointer *object) {
  (void)data;
  (void)object;
}
static void pointer_axis_source(void *data, struct wl_pointer *object,
                                uint32_t source) {
  (void)data;
  (void)object;
  (void)source;
}
static void pointer_axis_stop(void *data, struct wl_pointer *object,
                              uint32_t time, uint32_t axis) {
  (void)data;
  (void)object;
  (void)time;
  (void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *object,
                                  uint32_t axis, int32_t discrete) {
  (void)data;
  (void)object;
  // Axis 0 is vertical. The font panel keeps pointer_overlay empty.
  if (!axis && discrete && (pointer_overlay || font_panel_surface_armed()))
    overlay_signs_scroll(discrete);
}
static const struct wl_pointer_listener POINTER_LISTENER = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete};
void setup_cursor(void) {
  if (cursor_manager && pointer && !cursor_device) {
    cursor_device =
        wp_cursor_shape_manager_v1_get_pointer(cursor_manager, pointer);
  }
}
static void release_pointer(void) {
  finish_drag();
  overlay_signs_leave();
  pointer_overlay = NULL;
  if (cursor_device) {
    wp_cursor_shape_device_v1_destroy(cursor_device);
    cursor_device = NULL;
    last_cursor_serial = last_cursor_shape = 0;
  }
  if (pointer) {
    if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
      wl_pointer_release(pointer);
    } else {
      wl_pointer_destroy(pointer);
    }
    pointer = NULL;
  }
}
void release_seat(void) {
  release_pointer();
  if (seat) {
    if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
      wl_seat_release(seat);
    } else {
      wl_seat_destroy(seat);
    }
    seat = NULL;
  }
  seat_id = 0;
}
static void seat_capabilities(void *data, struct wl_seat *object,
                              uint32_t capabilities) {
  (void)data;
  if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)) {
    release_pointer();
  } else if (!pointer) {
    pointer = wl_seat_get_pointer(object);
    wl_pointer_add_listener(pointer, &POINTER_LISTENER, NULL);
    setup_cursor();
  }
}
static void seat_name(void *data, struct wl_seat *object, const char *name) {
  (void)data;
  (void)object;
  (void)name;
}
const struct wl_seat_listener SEAT_LISTENER = {
    .capabilities = seat_capabilities, .name = seat_name};

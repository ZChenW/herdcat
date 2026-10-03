#define _GNU_SOURCE
#include "platform/wayland.h"

#include "config/config.h"
#include "core/bongocat.h"
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
#include "platform/drag.h"
#include "platform/fullscreen.h"
#include "platform/input.h"
#include "platform/outputs.h"
#include "platform/scale.h"
#include "platform/shm_buffer.h"
#include "viewporter-client-protocol.h"
#include "wlr-foreign-toplevel-management-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>

struct wl_display *display;
struct wl_compositor *compositor;
struct wl_shm *shm;
struct zwlr_layer_shell_v1 *layer_shell;
struct wl_output *output;
struct wl_surface *surface;
struct zwlr_layer_surface_v1 *layer_surface;
atomic_bool configured;
atomic_bool fullscreen_detected;
static struct wl_registry *registry;
static struct zxdg_output_manager_v1 *xdg_manager;
static struct wp_viewporter *viewporter;
static struct wp_fractional_scale_manager_v1 *fractional_manager;
static config_t *global_config;
static void (*tick_callback)(void);
static int (*runtime_fds)(int *, size_t);
static int (*runtime_timeout)(void);
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct wp_cursor_shape_manager_v1 *cursor_manager;
static struct wp_cursor_shape_device_v1 *cursor_device;
static uint32_t seat_id, cursor_manager_id, pointer_serial;
static bool hidden;
static bool reconcile_pending;

typedef struct {
  uint32_t output_id;
  struct wl_output *output;
  char name[128];
  config_t config;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer;
  struct wp_viewport *viewport;
  struct wp_fractional_scale_v1 *fractional;
  shm_buffer_t *buffers[2];
  void *animation;
  uint32_t scale;
  int width, height, physical_width, physical_height;
  int cat_x, margin_y, output_height;
  bool has_position, accepts_pointer;
  bool configured, redraw, resize, closed;
} overlay_t;
static overlay_t overlays[MAX_OUTPUTS];
static overlay_t *active;
static overlay_t *pointer_overlay;

static bool dragging, drag_moved;
static double pointer_x, pointer_y, origin_x, origin_y;
static int origin_cat_x, origin_margin;
static struct wl_callback *drag_frame;
static void finish_drag(void);

static int cat_width(const overlay_t *overlay) {
  return (int)((int64_t)overlay->config.cat_height * CAT_IMAGE_WIDTH /
               CAT_IMAGE_HEIGHT);
}
static void clamp_position(overlay_t *overlay) {
  if (!overlay->has_position) {
    overlay->cat_x =
        drag_default_x(&overlay->config, overlay->width, cat_width(overlay));
    overlay->margin_y = 0;
    return;
  }
  drag_clamp(&overlay->cat_x, &overlay->margin_y, overlay->width,
             cat_width(overlay), overlay->output_height, overlay->height);
}
// Called with this overlay active; input and pixels share one commit.
static void update_input_region(overlay_t *overlay, bool invisible) {
  struct wl_region *region = wl_compositor_create_region(compositor);
  overlay->accepts_pointer =
      overlay->configured && overlay->config.cat_draggable && !invisible;
  if (!overlay->accepts_pointer && pointer_overlay == overlay) {
    finish_drag();
    pointer_overlay = NULL;
  }
  if (overlay->accepts_pointer) {
    drag_rect_t rect =
        drag_cat_rect(overlay->cat_x, &overlay->config, cat_width(overlay),
                      overlay->config.cat_height, overlay->height);
    wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
  }
  wl_surface_set_input_region(overlay->surface, region);
  wl_region_destroy(region);
}
static void cursor_shape(uint32_t shape) {
  if (cursor_device && pointer_overlay) {
    wp_cursor_shape_device_v1_set_shape(cursor_device, pointer_serial, shape);
  }
}
static void set_margin(overlay_t *overlay) {
  bool top = overlay->config.overlay_position == POSITION_TOP;
  zwlr_layer_surface_v1_set_margin(overlay->layer, top ? overlay->margin_y : 0,
                                   0, top ? 0 : overlay->margin_y, 0);
}
static void finish_drag(void) {
  if (dragging && drag_moved && pointer_overlay) {
    clamp_position(pointer_overlay);
    if (drag_position_save(pointer_overlay->name, pointer_overlay->cat_x,
                           pointer_overlay->margin_y) < 0) {
      bongocat_log_warning("Cannot save drag position for %s",
                           pointer_overlay->name);
    }
  }
  if (drag_frame) {
    wl_callback_destroy(drag_frame);
    drag_frame = NULL;
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
static void drag_follow_y(overlay_t *overlay) {
  if (drag_frame) {
    return;
  }
  int x = overlay->cat_x;
  int y = drag_margin_follow(origin_margin, origin_y, pointer_y,
                             overlay->config.overlay_position == POSITION_TOP);
  drag_clamp(&x, &y, overlay->width, cat_width(overlay), overlay->output_height,
             overlay->height);
  if (y == overlay->margin_y) {
    return;
  }
  bongocat_log_debug("Drag margin %d -> %d (pointer y %.1f, grab y %.1f)",
                     overlay->margin_y, y, pointer_y, origin_y);
  overlay->margin_y = y;
  set_margin(overlay);
  drag_frame = wl_surface_frame(overlay->surface);
  wl_callback_add_listener(drag_frame, &DRAG_FRAME_LISTENER, NULL);
  wl_surface_commit(overlay->surface);
}
static void drag_frame_done(void *data, struct wl_callback *callback,
                            uint32_t time) {
  (void)data;
  (void)time;
  wl_callback_destroy(callback);
  drag_frame = NULL;
  if (dragging && drag_moved && pointer_overlay) {
    drag_follow_y(pointer_overlay);
  }
}
static void pointer_enter(void *data, struct wl_pointer *object,
                          uint32_t serial, struct wl_surface *target,
                          wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)object;
  finish_drag();
  pointer_overlay = NULL;
  pointer_serial = serial;
  pointer_x = wl_fixed_to_double(x);
  pointer_y = wl_fixed_to_double(y);
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (overlays[i].surface == target && overlays[i].accepts_pointer &&
        !hidden) {
      pointer_overlay = &overlays[i];
      cursor_shape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB);
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
  pointer_overlay = NULL;
}
static void pointer_motion(void *data, struct wl_pointer *object, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)object;
  (void)time;
  pointer_x = wl_fixed_to_double(x);
  pointer_y = wl_fixed_to_double(y);
  overlay_t *overlay = pointer_overlay;
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
  int64_t desired_x = (int64_t)origin_cat_x + llround(pointer_x - origin_x);
  int next_x = desired_x < 0         ? 0
               : desired_x > INT_MAX ? INT_MAX
                                     : (int)desired_x;
  int unused_y = overlay->margin_y;
  drag_clamp(&next_x, &unused_y, overlay->width, cat_width(overlay),
             overlay->output_height, overlay->height);
  if (next_x != overlay->cat_x) {
    overlay->cat_x = next_x;
    overlay->redraw = true;
  }
  drag_follow_y(overlay);
}
static void pointer_button(void *data, struct wl_pointer *object,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
  (void)data;
  (void)object;
  (void)serial;
  (void)time;
  if (button != BTN_LEFT) {
    return;
  }
  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    finish_drag();
  } else if (pointer_overlay && pointer_overlay->accepts_pointer && !hidden) {
    finish_drag();
    dragging = true;
    origin_x = pointer_x;
    origin_y = pointer_y;
    origin_cat_x = pointer_overlay->cat_x;
    origin_margin = pointer_overlay->margin_y;
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
  (void)axis;
  (void)discrete;
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
static void setup_cursor(void) {
  if (cursor_manager && pointer && !cursor_device) {
    cursor_device =
        wp_cursor_shape_manager_v1_get_pointer(cursor_manager, pointer);
  }
}
static void release_pointer(void) {
  finish_drag();
  pointer_overlay = NULL;
  if (cursor_device) {
    wp_cursor_shape_device_v1_destroy(cursor_device);
    cursor_device = NULL;
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
static void release_seat(void) {
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
static const struct wl_seat_listener SEAT_LISTENER = {
    .capabilities = seat_capabilities, .name = seat_name};

static void activate(overlay_t *overlay) {
  active = overlay;
  output = overlay->output;
  surface = overlay->surface;
  layer_surface = overlay->layer;
  atomic_store(&configured, overlay->configured);
  animation_overlay_activate(overlay->animation, &overlay->config);
  fullscreen_recompute();
}
int wayland_phys_dim(int logical) {
  return scale_size_120(logical, active ? active->scale : 120);
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
void draw_bar(void) {
  overlay_t *overlay = active;
  if (!overlay || !overlay->configured || overlay->resize) {
    return;
  }
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
  bool invisible = (hidden || (config->layer != LAYER_OVERLAY &&
                               !config->disable_fullscreen_hide &&
                               atomic_load(&fullscreen_detected))) != 0;
  memset(buffer->pixels, 0, buffer->size);
  if (!invisible) {
    uint32_t *pixels = (uint32_t *)buffer->pixels;
    for (size_t i = 0; i < buffer->size / 4; i++) {
      pixels[i] = (uint32_t)config->overlay_opacity << 24;
    }
    cached_frame_t *frame = &anim_cached_frames[anim_index];
    int64_t x = scale_offset_120(overlay->cat_x, overlay->scale);
    int64_t logical_y = ((int64_t)overlay->height - config->cat_height) / 2 +
                        config->cat_y_offset;
    int64_t y = scale_offset_120(clamp_offset(logical_y), overlay->scale);
    if (frame->data) {
      blit_cached_frame(buffer->pixels, overlay->physical_width,
                        overlay->physical_height, frame->data, frame->width,
                        frame->height, clamp_offset(x), clamp_offset(y));
    }
  }
  update_input_region(overlay, invisible);
  buffer->busy = true;
  wl_surface_attach(overlay->surface, buffer->object, 0, 0);
  wl_surface_damage_buffer(overlay->surface, 0, 0, overlay->physical_width,
                           overlay->physical_height);
  wl_surface_commit(overlay->surface);
  overlay->redraw = false;
}
void wayland_request_redraw(void) {
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    overlays[i].redraw = true;
  }
}
void wayland_request_current_redraw(void) {
  if (active) {
    active->redraw = true;
  }
}
int wayland_reset_position(void) {
  finish_drag();
  if (drag_position_reset(NULL) < 0) {
    return 1;
  }
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    overlay_t *overlay = &overlays[i];
    if (overlay->surface) {
      overlay->has_position = false;
      clamp_position(overlay);
      set_margin(overlay);
      overlay->redraw = true;
    }
  }
  return 0;
}
void wayland_set_hidden(bool value) {
  hidden = value;
  wayland_request_redraw();
}
static void teardown(overlay_t *overlay) {
  if (pointer_overlay == overlay) {
    finish_drag();
    pointer_overlay = NULL;
  }
  if (overlay->layer) {
    zwlr_layer_surface_v1_destroy(overlay->layer);
  }
  if (overlay->fractional) {
    wp_fractional_scale_v1_destroy(overlay->fractional);
  }
  if (overlay->viewport) {
    wp_viewport_destroy(overlay->viewport);
  }
  if (overlay->surface) {
    wl_surface_destroy(overlay->surface);
  }
  for (size_t i = 0; i < 2; i++) {
    shm_buffer_retire(overlay->buffers[i]);
  }
  animation_overlay_destroy(overlay->animation);
  if (active == overlay) {
    active = NULL;
    output = NULL;
    surface = NULL;
    layer_surface = NULL;
  }
  *overlay = (overlay_t){0};
}
static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height) {
  overlay_t *overlay = data;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (width > INT_MAX || height > INT_MAX) {
    return;
  }
  int w = width ? (int)width : overlay->config.screen_width;
  int h = height ? (int)height : overlay->config.overlay_height;
  if (overlay->width != w || overlay->height != h) {
    overlay->width = w;
    overlay->height = h;
    overlay->resize = true;
  }
  clamp_position(overlay);
  set_margin(overlay);
  overlay->configured = true;
  overlay->redraw = true;
}
static void closed(void *data, struct zwlr_layer_surface_v1 *layer) {
  (void)layer;
  overlay_t *overlay = data;
  overlay->configured = false;
  overlay->closed = true;
  reconcile_pending = true;
}
static const struct zwlr_layer_surface_v1_listener LAYER_LISTENER = {
    .configure = configure, .closed = closed};
static void preferred_scale(void *data, struct wp_fractional_scale_v1 *object,
                            uint32_t scale) {
  (void)object;
  overlay_t *overlay = data;
  if (scale && scale != overlay->scale) {
    overlay->scale = scale;
    overlay->resize = true;
    overlay->redraw = true;
  }
}
static const struct wp_fractional_scale_v1_listener FRACTIONAL_LISTENER = {
    .preferred_scale = preferred_scale};
static uint32_t layer_value(layer_type_t layer) {
  switch (layer) {
  case LAYER_BACKGROUND:
    return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  case LAYER_BOTTOM:
    return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
  case LAYER_OVERLAY:
    return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  default:
    return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  }
}
static void properties(overlay_t *overlay) {
  uint32_t anchor =
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  anchor |= overlay->config.overlay_position == POSITION_TOP
                ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  zwlr_layer_surface_v1_set_anchor(overlay->layer, anchor);
  set_margin(overlay);
  zwlr_layer_surface_v1_set_size(overlay->layer, 0,
                                 overlay->config.overlay_height);
  zwlr_layer_surface_v1_set_exclusive_zone(overlay->layer, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(overlay->layer, 0);
}
static bool create(overlay_t *overlay, output_ref_t *ref) {
  overlay->output_id = ref->name;
  overlay->output = ref->wl_output;
  snprintf(overlay->name, sizeof(overlay->name), "%s", ref->name_str);
  config_for_monitor(global_config, ref->name_str, &overlay->config);
  overlay->config.screen_width = ref->screen_width;
  overlay->scale = ref->wl_scale > 0 && ref->wl_scale <= INT_MAX / 120
                       ? (uint32_t)ref->wl_scale * 120
                       : 120;
  overlay->output_height = ref->screen_height;
  overlay->width = ref->screen_width;
  overlay->height = overlay->config.overlay_height;
  int position =
      drag_position_load(overlay->name, &overlay->cat_x, &overlay->margin_y);
  overlay->has_position = position == 0;
  if (position < 0) {
    bongocat_log_warning("Cannot load drag position for %s", overlay->name);
  }
  clamp_position(overlay);
  overlay->animation = animation_overlay_create(&overlay->config);
  if (!overlay->animation) {
    teardown(overlay);
    return false;
  }
  overlay->surface = wl_compositor_create_surface(compositor);
  overlay->layer = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, overlay->surface, ref->wl_output,
      layer_value(overlay->config.layer), "bongocat-overlay");
  zwlr_layer_surface_v1_add_listener(overlay->layer, &LAYER_LISTENER, overlay);
  struct wl_region *region = wl_compositor_create_region(compositor);
  wl_surface_set_input_region(overlay->surface, region);
  wl_region_destroy(region);
  if (viewporter) {
    overlay->viewport =
        wp_viewporter_get_viewport(viewporter, overlay->surface);
    if (fractional_manager) {
      overlay->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(
          fractional_manager, overlay->surface);
      wp_fractional_scale_v1_add_listener(overlay->fractional,
                                          &FRACTIONAL_LISTENER, overlay);
    }
  }
  properties(overlay);
  overlay->resize = true;
  overlay->redraw = true;
  wl_surface_commit(overlay->surface);
  return true;
}
static bool selected(output_ref_t *ref, bool first) {
  if (global_config->output_name && global_config->num_output_names <= 1) {
    return strcmp(ref->name_str, global_config->output_name) == 0;
  }
  if (global_config->num_output_names) {
    for (int i = 0; i < global_config->num_output_names; i++) {
      if (strcmp(ref->name_str, global_config->output_names[i]) == 0) {
        return true;
      }
    }
    return false;
  }
  return first;
}
static void reconcile(void) {
  bool first = true;
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    output_ref_t *ref = &outputs[i];
    overlay_t *overlay = &overlays[i];
    bool wanted =
        (ref->wl_output && ref->screen_width > 0 && selected(ref, first)) != 0;
    if (ref->wl_output && ref->screen_width > 0) {
      first = false;
    }
    if (overlay->surface &&
        (!wanted || overlay->output_id != ref->name || overlay->closed)) {
      teardown(overlay);
    }
    if (!wanted) {
      continue;
    }
    if (!overlay->surface) {
      create(overlay, ref);
      continue;
    }
    config_t effective;
    config_for_monitor(global_config, ref->name_str, &effective);
    effective.screen_width = ref->screen_width;
    if (effective.layer != overlay->config.layer) {
      teardown(overlay);
      create(overlay, ref);
      continue;
    }
    bool size_changed =
        (effective.overlay_height != overlay->config.overlay_height ||
         effective.screen_width != overlay->config.screen_width) != 0;
    overlay->config = effective;
    if (!overlay->fractional) {
      uint32_t scale = ref->wl_scale > 0 && ref->wl_scale <= INT_MAX / 120
                           ? (uint32_t)ref->wl_scale * 120
                           : 120;
      if (scale != overlay->scale) {
        overlay->scale = scale;
        overlay->resize = true;
      }
    }
    if (size_changed) {
      overlay->width = effective.screen_width;
      overlay->height = effective.overlay_height;
      overlay->resize = true;
    }
    overlay->output_height = ref->screen_height;
    clamp_position(overlay);
    properties(overlay);
    overlay->redraw = true;
    wl_surface_commit(overlay->surface);
  }
  reconcile_pending = false;
}
static bool resize_buffers(overlay_t *overlay) {
  int width = scale_size_120(overlay->width, overlay->scale);
  int height = scale_size_120(overlay->height, overlay->scale);
  shm_buffer_t *a = shm_buffer_create(shm, width, height);
  shm_buffer_t *b = a ? shm_buffer_create(shm, width, height) : NULL;
  if (!a || !b) {
    shm_buffer_retire(a);
    shm_buffer_retire(b);
    return false;
  }
  for (size_t i = 0; i < 2; i++) {
    shm_buffer_retire(overlay->buffers[i]);
  }
  overlay->buffers[0] = a;
  overlay->buffers[1] = b;
  overlay->physical_width = width;
  overlay->physical_height = height;
  if (overlay->viewport) {
    wl_surface_set_buffer_scale(overlay->surface, 1);
    wp_viewport_set_destination(overlay->viewport, overlay->width,
                                overlay->height);
  } else {
    {
      wl_surface_set_buffer_scale(overlay->surface,
                                  (int)(overlay->scale / 120));
    }
  }
  overlay->resize = false;
  return true;
}
static void global(void *data, struct wl_registry *object, uint32_t id,
                   const char *interface, uint32_t version) {
  (void)data;
  if (strcmp(interface, wl_compositor_interface.name) == 0 && version >= 4) {
    {
      compositor = wl_registry_bind(object, id, &wl_compositor_interface, 4);
    }
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    { shm = wl_registry_bind(object, id, &wl_shm_interface, 1); }
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    {
      layer_shell = wl_registry_bind(object, id, &zwlr_layer_shell_v1_interface,
                                     version < 4 ? version : 4);
    }
  } else if (strcmp(interface, wl_seat_interface.name) == 0 && !seat) {
    seat_id = id;
    seat = wl_registry_bind(object, id, &wl_seat_interface,
                            version < 5 ? version : 5);
    wl_seat_add_listener(seat, &SEAT_LISTENER, NULL);
  } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) ==
                 0 &&
             !cursor_manager) {
    cursor_manager_id = id;
    cursor_manager =
        wl_registry_bind(object, id, &wp_cursor_shape_manager_v1_interface, 1);
    setup_cursor();
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    { outputs_add(object, id, version); }
  } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0 &&
             version >= 2) {
    xdg_manager =
        wl_registry_bind(object, id, &zxdg_output_manager_v1_interface,
                         version < 3 ? version : 3);
    outputs_set_manager(xdg_manager);
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
    { viewporter = wl_registry_bind(object, id, &wp_viewporter_interface, 1); }
  } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) ==
             0) {
    {
      fractional_manager = wl_registry_bind(
          object, id, &wp_fractional_scale_manager_v1_interface, 1);
    }
  } else if (strcmp(interface,
                    zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
    struct zwlr_foreign_toplevel_manager_v1 *manager = wl_registry_bind(
        object, id, &zwlr_foreign_toplevel_manager_v1_interface,
        version < 3 ? version : 3);
    fullscreen_init(manager);
  }
}
static void removed(void *data, struct wl_registry *object, uint32_t id) {
  (void)data;
  (void)object;
  if (id == seat_id) {
    release_seat();
  }
  if (id == cursor_manager_id) {
    if (cursor_device) {
      wp_cursor_shape_device_v1_destroy(cursor_device);
      cursor_device = NULL;
    }
    wp_cursor_shape_manager_v1_destroy(cursor_manager);
    cursor_manager = NULL;
    cursor_manager_id = 0;
  }
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (overlays[i].surface && overlays[i].output_id == id) {
      teardown(&overlays[i]);
    }
  }
  fullscreen_output_removed(id);
  outputs_remove(id);
  reconcile_pending = true;
}
static const struct wl_registry_listener REGISTRY_LISTENER = {
    .global = global, .global_remove = removed};
static int discover(void) {
  display = wl_display_connect(NULL);
  if (!display) {
    return -1;
  }
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &REGISTRY_LISTENER, NULL);
  for (int pass = 0; pass < 2; pass++) {
    if (wl_display_roundtrip(display) < 0) {
      return -1;
    }
  }
  if (wl_display_get_error(display) != 0) {
    return -1;
  }
  return 0;
}
int wayland_list_monitors(bool doctor) {
  if (discover() < 0) {
    fprintf(
        stderr,
        "Wayland: cannot connect; check WAYLAND_DISPLAY and XDG_RUNTIME_DIR\n");
    wayland_cleanup();
    return 1;
  }
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    if (outputs[i].wl_output) {
      printf("%s: %dx%d scale=%d\n", outputs[i].name_str,
             outputs[i].screen_width, outputs[i].screen_height,
             outputs[i].wl_scale);
    }
  }
  int failure = !compositor || !shm || !layer_shell;
  if (doctor) {
    printf("Protocols: compositor-v4=%s shm=%s layer-shell=%s "
           "viewporter=%s fractional-scale=%s fullscreen=%s cursor-shape=%s\n",
           compositor ? "yes" : "missing", shm ? "yes" : "missing",
           layer_shell ? "yes" : "missing", viewporter ? "yes" : "no",
           fractional_manager ? "yes" : "no",
           (int)fs_detector_available() ? "yes" : "no",
           cursor_manager ? "yes" : "no");
  }
  wayland_cleanup();
  return failure;
}
bongocat_error_t wayland_init(config_t *config) {
  global_config = config;
  if (discover() < 0 || !compositor || !shm || !layer_shell) {
    bongocat_log_error(
        "Wayland requires wl_compositor v4, wl_shm and layer-shell");
    wayland_cleanup();
    return BONGOCAT_ERROR_WAYLAND;
  }
  reconcile_pending = true;
  return BONGOCAT_SUCCESS;
}
void wayland_update_config(config_t *config) {
  finish_drag();
  global_config = config;
  reconcile_pending = true;
}
bongocat_error_t wayland_run(const volatile sig_atomic_t *running) {
  bool flush_blocked = false;
  while (*running && display) {
    if (wl_display_dispatch_pending(display) < 0) {
      return BONGOCAT_ERROR_WAYLAND;
    }
    if (tick_callback) {
      tick_callback();
    }
    if (!*running) {
      break;
    }
    if (outputs_take_changed()) {
      reconcile_pending = true;
    }
    if (flush_blocked) {
      if (wl_display_flush(display) >= 0) {
        flush_blocked = false;
      } else if (errno != EAGAIN) {
        return BONGOCAT_ERROR_WAYLAND;
      }
    }
    if (reconcile_pending && !flush_blocked) {
      reconcile();
    }
    input_process_events();
    unsigned paws =
        !flush_blocked && pending_paws ? atomic_exchange(pending_paws, 0) : 0;
    int timeout = -1;
    for (size_t i = 0; !flush_blocked && i < MAX_OUTPUTS; i++) {
      overlay_t *overlay = &overlays[i];
      if (!overlay->surface) {
        continue;
      }
      activate(overlay);
      if (overlay->resize && !resize_buffers(overlay)) {
        return BONGOCAT_ERROR_MEMORY;
      }
      int cat_h = scale_size_120(overlay->config.cat_height, overlay->scale);
      int64_t cat_w = scale_size_120(cat_width(overlay), overlay->scale);
      if (!cat_h || cat_w > INT_MAX) {
        return BONGOCAT_ERROR_MEMORY;
      }
      animation_overlay_cache((int)cat_w, cat_h);
      for (int frame = 0; frame < NUM_FRAMES; frame++) {
        if (!anim_cached_frames[frame].data) {
          return BONGOCAT_ERROR_MEMORY;
        }
      }
      int next = animation_tick(paws);
      if (next >= 0 && (timeout < 0 || next < timeout)) {
        timeout = next;
      }
      if (overlay->redraw) {
        draw_bar();
      }
    }
    int next_runtime = runtime_timeout ? runtime_timeout() : -1;
    if (next_runtime >= 0 && (timeout < 0 || next_runtime < timeout)) {
      timeout = next_runtime;
    }
    struct pollfd fds[8] = {
        {.fd = wl_display_get_fd(display), .events = POLLIN}
    };
    int external[7];
    int count = runtime_fds ? runtime_fds(external, 7) : 0;
    for (int i = 0; i < count; i++) {
      fds[i + 1] = (struct pollfd){.fd = external[i], .events = POLLIN};
    }
    while (wl_display_prepare_read(display) != 0) {
      if (wl_display_dispatch_pending(display) < 0) {
        return BONGOCAT_ERROR_WAYLAND;
      }
    }
    if (wl_display_flush(display) < 0) {
      if (errno != EAGAIN) {
        wl_display_cancel_read(display);
        return BONGOCAT_ERROR_WAYLAND;
      }
      flush_blocked = true;
      fds[0].events |= POLLOUT;
    }
    int result = poll(fds, (nfds_t)count + 1, timeout);
    if (result > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(display) < 0) {
        return BONGOCAT_ERROR_WAYLAND;
      }
    } else {
      { wl_display_cancel_read(display); }
    }
    if (result < 0 && errno != EINTR) {
      return BONGOCAT_ERROR_WAYLAND;
    }
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return BONGOCAT_ERROR_WAYLAND;
    }
  }
  return BONGOCAT_SUCCESS;
}
struct wl_output *wayland_get_current_screen_output(void) {
  return output;
}
void wayland_set_tick_callback(void (*callback)(void)) {
  tick_callback = callback;
}
void wayland_set_runtime_fds(int (*callback)(int *, size_t)) {
  runtime_fds = callback;
}
void wayland_set_runtime_timeout(int (*callback)(void)) {
  runtime_timeout = callback;
}
void wayland_cleanup(void) {
  release_seat();
  if (cursor_manager) {
    wp_cursor_shape_manager_v1_destroy(cursor_manager);
    cursor_manager = NULL;
    cursor_manager_id = 0;
  }
  for (size_t i = 0; i < MAX_OUTPUTS; i++) {
    teardown(&overlays[i]);
  }
  shm_buffers_cleanup();
  fullscreen_cleanup();
  outputs_cleanup();
  if (xdg_manager) {
    zxdg_output_manager_v1_destroy(xdg_manager);
  }
  if (fractional_manager) {
    wp_fractional_scale_manager_v1_destroy(fractional_manager);
  }
  if (viewporter) {
    wp_viewporter_destroy(viewporter);
  }
  if (layer_shell) {
    zwlr_layer_shell_v1_destroy(layer_shell);
  }
  if (shm) {
    wl_shm_destroy(shm);
  }
  if (compositor) {
    wl_compositor_destroy(compositor);
  }
  if (registry) {
    wl_registry_destroy(registry);
  }
  if (display) {
    wl_display_disconnect(display);
  }
  display = NULL;
  registry = NULL;
  compositor = NULL;
  shm = NULL;
  layer_shell = NULL;
  xdg_manager = NULL;
  fractional_manager = NULL;
  viewporter = NULL;
}

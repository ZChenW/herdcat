#define _GNU_SOURCE
#include "platform/wayland.h"

#include "config/config.h"
#include "core/herdcat.h"
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
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/drag.h"
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
#include "platform/overlay_geometry.h"

#include <limits.h>

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
bool hidden;
static bool reconcile_pending;

overlay_t overlays[MAX_OUTPUTS];
overlay_t *active;

static void activate(overlay_t *overlay) {
  active = overlay;
  output = overlay->output;
  surface = overlay->surface;
  layer_surface = overlay->layer;
  atomic_store(&configured, overlay->configured);
  animation_overlay_activate(overlay->animation, &overlay->config);
  fullscreen_recompute();
}
static void teardown(overlay_t *overlay) {
  size_t index = (size_t)(overlay - overlays);
  font_panel_surface_output_gone(index);
  if (index < MAX_OUTPUTS && sign_frames[index]) {
    wl_callback_destroy(sign_frames[index]);
    sign_frames[index] = NULL;
    overlay_signs_frame_wait(index, false);
  }
  if (pointer_overlay == overlay) {
    finish_drag();
    overlay_signs_leave();
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
  overlay_signs_output_gone(index);
  *overlay = (overlay_t){0};
}
static void configure(void *data, struct zwlr_layer_surface_v1 *layer,
                      uint32_t serial, uint32_t width, uint32_t height) {
  overlay_t *overlay = data;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (width > INT_MAX || height > INT_MAX) {
    return;
  }
  int w = width ? (int)width : overlay->requested_width;
  int h = height ? (int)height : overlay->requested_height;
  // The compositor's dimensions are final, including constrained outputs.
  // Allocate at this size before promoting capacity in the event loop.
  overlay->await_configure = false;
  int old_cat_x = overlay->cat_x, old_cat_y = overlay->cat_y;
  if (overlay->width != w || overlay->height != h) {
    overlay->width = w;
    overlay->height = h;
    overlay->resize = true;
  }
  clamp_position(overlay);
  overlay_pointer_rebase(overlay, overlay->cat_x - old_cat_x,
                         overlay->cat_y - old_cat_y);
  // Margins and viewport destination join the next painted buffer commit.
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
    reconcile_pending = true;
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
  uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  anchor |= overlay->config.overlay_position == POSITION_TOP
                ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  zwlr_layer_surface_v1_set_anchor(overlay->layer, anchor);
  set_margin(overlay);
  zwlr_layer_surface_v1_set_size(overlay->layer,
                                 (uint32_t)overlay->requested_width,
                                 (uint32_t)overlay->requested_height);
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
  overlay->tiers.capacity = surface_tier_capacity(&overlay->config, 0, false);
  surface_size_t size =
      surface_tier_size(&overlay->config, overlay->tiers.capacity,
                        ref->screen_width, overlay->scale);
  overlay->width = overlay->requested_width = size.width;
  overlay->height = overlay->requested_height = size.height;
  overlay->await_configure = true;
  overlay_signs_capacity((size_t)(overlay - overlays), overlay->tiers.capacity);
  int position = drag_position_load(overlay->name, &overlay->output_x,
                                    &overlay->position_y);
  overlay->has_position = position == 0;
  if (position < 0) {
    herdcat_log_warning("Cannot load drag position for %s", overlay->name);
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
      layer_value(overlay->config.layer), "herdcat-overlay");
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
static void request_size(overlay_t *overlay, int capacity) {
  surface_size_t size = surface_tier_size(
      &overlay->config, capacity, overlay->config.screen_width, overlay->scale);
  overlay->requested_width = size.width;
  overlay->requested_height = size.height;
  if (size.width == overlay->width && size.height == overlay->height) {
    surface_tier_ready(&overlay->tiers);
    overlay_signs_capacity((size_t)(overlay - overlays),
                           overlay->tiers.capacity);
    clamp_position(overlay);
    return;
  }
  overlay->await_configure = true;
  uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  anchor |= overlay->config.overlay_position == POSITION_TOP
                ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  zwlr_layer_surface_v1_set_anchor(overlay->layer, anchor);
  zwlr_layer_surface_v1_set_size(overlay->layer, (uint32_t)size.width,
                                 (uint32_t)size.height);
  // Keep the attached buffer and committed margins. Submit new margins,
  // viewport, pixels and input together only after configure + allocation.
  wl_surface_commit(overlay->surface);
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
    int capacity = overlay->tiers.pending ? overlay->tiers.requested
                                          : overlay->tiers.capacity;
    if (effective.sign_style == SIGN_STYLE_OFF ||
        effective.overlay_opacity > 0 || capacity > effective.sign_max)
      capacity = effective.sign_max;
    overlay->tiers.requested = capacity;
    overlay->tiers.pending = true;
    overlay->tiers.shrink_at = 0;
    request_size(overlay, capacity);
    overlay->output_height = ref->screen_height;
    clamp_position(overlay);
    // Configure will submit changed dimensions with the new buffer.
    if (!overlay->await_configure)
      properties(overlay);
    overlay->damage_all = true;
    overlay->redraw = true;
    // Presentation commits the new placement with its pixels and input.
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
  overlay->pending_damage = (pixel_rect_t){0};
  overlay->resize = false;
  overlay->damage_all = true;
  return true;
}
static void global(void *data, struct wl_registry *object, uint32_t id,
                   const char *interface, uint32_t version) {
  (void)data;
  if (strcmp(interface, wl_compositor_interface.name) == 0 && version >= 4) {
    compositor = wl_registry_bind(object, id, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    { shm = wl_registry_bind(object, id, &wl_shm_interface, 1); }
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    layer_shell = wl_registry_bind(object, id, &zwlr_layer_shell_v1_interface,
                                   version < 4 ? version : 4);
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
    fractional_manager = wl_registry_bind(
        object, id, &wp_fractional_scale_manager_v1_interface, 1);
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
herdcat_error_t wayland_init(config_t *config) {
  global_config = config;
  if (text_init(config->sign_font) != 0) {
    herdcat_log_warning("Sign text unavailable; boards will omit labels");
  }
  if (discover() < 0 || !compositor || !shm || !layer_shell) {
    herdcat_log_error(
        "Wayland requires wl_compositor v4, wl_shm and layer-shell");
    wayland_cleanup();
    return HERDCAT_ERROR_WAYLAND;
  }
  reconcile_pending = true;
  return HERDCAT_SUCCESS;
}
void wayland_update_config(config_t *config) {
  finish_drag();
  global_config = config;
  reconcile_pending = true;
}
herdcat_error_t wayland_run(const volatile sig_atomic_t *running) {
  bool flush_blocked = false;
  bool runtime_ready = true;
  while (*running && display) {
    if (wl_display_dispatch_pending(display) < 0) {
      return HERDCAT_ERROR_WAYLAND;
    }
    // Display dispatch handles releases and frame callbacks itself. Runtime
    // jobs need a tick on their descriptors or a deadline, not every release.
    if (tick_callback &&
        (runtime_ready || (runtime_timeout && runtime_timeout() == 0))) {
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
        return HERDCAT_ERROR_WAYLAND;
      }
    }
    if (reconcile_pending && !flush_blocked) {
      reconcile();
    }
    input_process_events();
    unsigned paws =
        !flush_blocked && pending_paws ? atomic_exchange(pending_paws, 0) : 0;
    int64_t sign_now = overlay_signs_now();
    int timeout = -1;
    for (size_t i = 0; !flush_blocked && i < MAX_OUTPUTS; i++) {
      overlay_t *overlay = &overlays[i];
      if (!overlay->surface) {
        continue;
      }
      activate(overlay);
      if (overlay->await_configure || !overlay->configured)
        continue;
      if (overlay->resize && !resize_buffers(overlay)) {
        return HERDCAT_ERROR_MEMORY;
      }
      if (overlay->tiers.pending) {
        surface_tier_ready(&overlay->tiers);
        overlay_signs_capacity(i, overlay->tiers.capacity);
        // Flip at the promoted capacity before admitting any new sign frame.
        int old_cat_x = overlay->cat_x, old_cat_y = overlay->cat_y;
        clamp_position(overlay);
        overlay_pointer_rebase(overlay, overlay->cat_x - old_cat_x,
                               overlay->cat_y - old_cat_y);
      }
      int cat_h = scale_size_120(overlay->config.cat_height, overlay->scale);
      int64_t cat_w = scale_size_120(cat_width(overlay), overlay->scale);
      if (!cat_h || cat_w > INT_MAX) {
        return HERDCAT_ERROR_MEMORY;
      }
      animation_overlay_cache((int)cat_w, cat_h);
      for (int frame = 0; frame < NUM_FRAMES; frame++) {
        if (!anim_cached_frames[frame].data) {
          return HERDCAT_ERROR_MEMORY;
        }
      }
      int next = animation_tick(paws);
      if (next >= 0 && (timeout < 0 || next < timeout)) {
        timeout = next;
      }
      bool concealed = overlay_hidden(overlay) || !overlay->configured;
      font_panel_surface_margin(overlay->margin_x, overlay->margin_y);
      text_set_scale((int)overlay->scale);
      overlay_signs_width(i, overlay->width);
      overlay_signs_step_t sign_step = overlay_signs_step(
          i, &overlay->config, overlay->cat_x, cat_width(overlay),
          overlay->height, concealed, sign_now);
      const sign_frame_t *sign_frame = overlay_signs_frame(i);
      int request = surface_tier_update(
          &overlay->tiers, sign_step.required_capacity,
          sign_step.shrink_blocked, sign_frame && sign_frame->transitioning,
          sign_now);
      if (request >= 0) {
        request_size(overlay, request);
        overlay->damage_all = true;
        overlay->redraw = true;
        if (overlay->await_configure)
          continue;
        // Equal geometry can still promote capacity. Rebuild at readiness.
        timeout = 0;
        continue;
      }
      if (overlay->tiers.shrink_at > sign_now) {
        int64_t wait = overlay->tiers.shrink_at - sign_now;
        if (wait > INT_MAX)
          wait = INT_MAX;
        if (timeout < 0 || wait < timeout)
          timeout = (int)wait;
      }
      if (sign_step.redraw) {
        overlay->redraw = true;
      }
      if (sign_step.timeout_ms > 0 &&
          (timeout < 0 || sign_step.timeout_ms < timeout)) {
        timeout = sign_step.timeout_ms;
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
        return HERDCAT_ERROR_WAYLAND;
      }
    }
    if (wl_display_flush(display) < 0) {
      if (errno != EAGAIN) {
        wl_display_cancel_read(display);
        return HERDCAT_ERROR_WAYLAND;
      }
      flush_blocked = true;
      fds[0].events |= POLLOUT;
    }
    int result = poll(fds, (nfds_t)count + 1, timeout);
    runtime_ready = result <= 0;
    for (int i = 1; i <= count; i++)
      runtime_ready = runtime_ready || fds[i].revents != 0;
    if (result > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(display) < 0) {
        return HERDCAT_ERROR_WAYLAND;
      }
    } else
      wl_display_cancel_read(display);
    if (result < 0 && errno != EINTR) {
      return HERDCAT_ERROR_WAYLAND;
    }
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return HERDCAT_ERROR_WAYLAND;
    }
  }
  return HERDCAT_SUCCESS;
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
  if (xdg_manager)
    zxdg_output_manager_v1_destroy(xdg_manager);
  if (fractional_manager)
    wp_fractional_scale_manager_v1_destroy(fractional_manager);
  if (viewporter)
    wp_viewporter_destroy(viewporter);
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
  overlay_signs_cleanup();
  sign_draw_cleanup();
  text_cleanup();
  display = NULL;
  registry = NULL;
  compositor = NULL;
  shm = NULL;
  layer_shell = NULL;
  xdg_manager = NULL;
  fractional_manager = NULL;
  viewporter = NULL;
}
struct wp_viewport *wayland_viewport_for(struct wl_surface *target) {
  if (!viewporter || !target)
    return NULL;
  return wp_viewporter_get_viewport(viewporter, target);
}

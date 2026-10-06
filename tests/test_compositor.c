// Small protocol fixture; intentionally has no renderer or input dependencies.
#define _GNU_SOURCE
#include "fullscreen-server.h"
#include "layer-server.h"
#include "scale-server.h"
#include "test_compositor_internal.h"
#include "viewport-server.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server.h>

struct wl_display *server;
static struct wl_event_loop *loop;
static unsigned commits, surfaces, live;
static struct wl_resource *toplevel;
static bool drag_mode, measure_mode;
static int measure_frame_ms = 17, measure_release_ms = 16;
static struct wl_resource *fractional_objects[32];
struct monitor monitors[2] = {
    {.width = 800,  .height = 600, .scale = 1, .name = "TEST-1"},
    {.width = 1024, .height = 768, .scale = 1, .name = "TEST-2"}
};
static void measure_event(const char *event, const char *name) {
  if (!measure_mode)
    return;
  struct timespec now;
  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  printf("%s %s %.9f\n", event, name,
         (double)now.tv_sec + (double)now.tv_nsec / 1e9);
  fflush(stdout);
}
void destroy_request(struct wl_client *client, struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}
static void buffer_gone(struct wl_listener *listener, void *data) {
  (void)data;
  struct test_surface *surface =
      wl_container_of(listener, surface, buffer_destroy);
  surface->buffer = NULL;
  wl_list_remove(&listener->link);
}
static int release_buffer(void *data) {
  struct test_surface *surface = data;
  if (surface->buffer) {
    wl_buffer_send_release(surface->buffer);
    wl_list_remove(&surface->buffer_destroy.link);
    surface->buffer = NULL;
  }
  return 0;
}
struct delayed_release {
  struct wl_resource *buffer;
  struct wl_listener destroyed;
  struct wl_event_source *timer;
  char name[32];
};
static void delayed_gone(struct wl_listener *listener, void *data) {
  (void)data;
  struct delayed_release *release =
      wl_container_of(listener, release, destroyed);
  wl_event_source_remove(release->timer);
  wl_list_remove(&listener->link);
  free(release);
}
static int delayed_send(void *data) {
  struct delayed_release *release = data;
  measure_event("release", release->name);
  wl_buffer_send_release(release->buffer);
  wl_list_remove(&release->destroyed.link);
  wl_event_source_remove(release->timer);
  free(release);
  return 0;
}
static void delay_release(struct test_surface *surface) {
  struct delayed_release *release = calloc(1, sizeof(*release));
  assert(release);
  release->buffer = surface->buffer;
  snprintf(release->name, sizeof(release->name), "%s", surface->monitor->name);
  wl_list_remove(&surface->buffer_destroy.link);
  surface->buffer = NULL;
  release->destroyed.notify = delayed_gone;
  wl_resource_add_destroy_listener(release->buffer, &release->destroyed);
  release->timer = wl_event_loop_add_timer(loop, delayed_send, release);
  // Measurement must not inherit the regression fixture's buffer starvation.
  wl_event_source_timer_update(release->timer,
                               measure_mode ? measure_release_ms : 200);
}
static void surface_destroyed(struct wl_resource *resource) {
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (surface->buffer)
    wl_list_remove(&surface->buffer_destroy.link);
  wl_event_source_remove(surface->release_timer);
  if (surface->layer)
    wl_resource_set_user_data(surface->layer, NULL);
  if (surface->monitor && surface->monitor->surface == surface)
    surface->monitor->surface = NULL;
  if (surface->monitor && surface->monitor->extra == surface)
    surface->monitor->extra = NULL;
  if (surface->layered && live)
    live--;
  if (drag_mode && surface->layered) {
    printf("gone %s\n", surface->ns);
    printf("live %u\n", live);
    fflush(stdout);
  }
  free(surface);
}
static void attach(struct wl_client *client, struct wl_resource *resource,
                   struct wl_resource *buffer, int32_t x, int32_t y) {
  (void)client;
  (void)x;
  (void)y;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  // The release delay deliberately exhausts both client buffers.
  assert(!surface->buffer);
  surface->buffer = buffer;
  if (buffer) {
    surface->buffer_destroy.notify = buffer_gone;
    wl_resource_add_destroy_listener(buffer, &surface->buffer_destroy);
  }
}
static void rectangle(struct wl_client *client, struct wl_resource *resource,
                      int32_t x, int32_t y, int32_t width, int32_t height) {
  (void)client;
  (void)resource;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}
struct delayed_frame {
  struct wl_resource *callback;
  struct wl_event_source *timer;
  const char *name;
};
static void frame_destroyed(struct wl_resource *resource) {
  struct delayed_frame *pending = wl_resource_get_user_data(resource);
  wl_event_source_remove(pending->timer);
  free(pending);
}
static int frame_done(void *data) {
  struct delayed_frame *pending = data;
  measure_event("frame-done", pending->name);
  wl_callback_send_done(pending->callback, 0);
  wl_resource_destroy(pending->callback);
  return 0;
}
static void frame(struct wl_client *client, struct wl_resource *resource,
                  uint32_t id) {
  struct test_surface *surface = wl_resource_get_user_data(resource);
  struct wl_resource *callback =
      wl_resource_create(client, &wl_callback_interface, 1, id);
  if (measure_mode) {
    // Immediate callbacks would turn continuous transitions into a busy loop.
    struct delayed_frame *pending = calloc(1, sizeof(*pending));
    assert(pending);
    pending->callback = callback;
    pending->name = surface->monitor ? surface->monitor->name : "unassigned";
    measure_event("frame-request", pending->name);
    pending->timer = wl_event_loop_add_timer(loop, frame_done, pending);
    wl_resource_set_implementation(callback, NULL, pending, frame_destroyed);
    wl_event_source_timer_update(pending->timer, measure_frame_ms);
    return;
  }
  wl_callback_send_done(callback, 0);
  wl_resource_destroy(callback);
}
struct test_region {
  struct test_rect first, last;
  unsigned count;
};
static void input_region(struct wl_client *client, struct wl_resource *resource,
                         struct wl_resource *region) {
  (void)client;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  struct test_region *rects = region ? wl_resource_get_user_data(region) : NULL;
  surface->input = rects ? rects->last : (struct test_rect){0};
  surface->cat_input = rects ? rects->first : (struct test_rect){0};
  surface->input_count = rects ? rects->count : 0;
}
static void region_request(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *region) {
  (void)client;
  (void)resource;
  (void)region;
}
static void commit(struct wl_client *client, struct wl_resource *resource) {
  (void)client;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (surface->layer && surface->configure_pending) {
    zwlr_layer_surface_v1_send_configure(
        surface->layer, wl_display_next_serial(server),
        surface->width ? surface->width : (uint32_t)surface->monitor->width,
        surface->height ? surface->height : 50);
    surface->configure_pending = false;
  }
  if (drag_mode && surface->monitor) {
    printf("input-count %s %u\n", surface->monitor->name, surface->input_count);
    printf("input %s %d %d %d %d\n", surface->monitor->name, surface->input.x,
           surface->input.y, surface->input.width, surface->input.height);
    if (!strcmp(surface->ns, "herdcat-overlay") && surface->buffer) {
      printf("snapshot %s %d %d %d %u %u %d %d %d %d %d %d %d %d\n",
             surface->monitor->name, surface->margin_top,
             surface->margin_bottom, surface->margin_left, surface->width,
             surface->height, surface->cat_input.x, surface->cat_input.y,
             surface->cat_input.width, surface->cat_input.height,
             surface->input.x, surface->input.y, surface->input.width,
             surface->input.height);
    }
    fflush(stdout);
  }
  if (surface->buffer) {
    struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->buffer);
    assert(buffer && wl_shm_buffer_get_width(buffer) > 0);
    commits++;
    measure_event("submit", surface->monitor->name);
    wl_shm_buffer_begin_access(buffer);
    uint32_t *pixels = wl_shm_buffer_get_data(buffer);
    size_t count = (size_t)wl_shm_buffer_get_stride(buffer) *
                   wl_shm_buffer_get_height(buffer) / 4;
    bool visible = false;
    for (size_t i = 0; i < count; i++)
      visible |= pixels[i] != 0;
    wl_shm_buffer_end_access(buffer);
    printf("visible %s %d\n", surface->monitor->name, visible);
    printf("commit %s %dx%d\n", surface->monitor->name,
           wl_shm_buffer_get_width(buffer), wl_shm_buffer_get_height(buffer));
    fflush(stdout);
    // Send release now for surface reuse, while exercising a queued event.
    // Separate client buffers remain busy until it dispatches this event.
    delay_release(surface);
  }
}
static void integer_request(struct wl_client *client,
                            struct wl_resource *resource, int32_t value) {
  (void)client;
  (void)resource;
  (void)value;
}
static const struct wl_surface_interface surface_impl = {
    .destroy = destroy_request,
    .attach = attach,
    .damage = rectangle,
    .frame = frame,
    .set_opaque_region = region_request,
    .set_input_region = input_region,
    .commit = commit,
    .set_buffer_transform = integer_request,
    .set_buffer_scale = integer_request,
    .damage_buffer = rectangle};
static void create_surface(struct wl_client *client,
                           struct wl_resource *resource, uint32_t id) {
  struct test_surface *surface = calloc(1, sizeof(*surface));
  assert(surface);
  surface->resource = wl_resource_create(client, &wl_surface_interface,
                                         wl_resource_get_version(resource), id);
  surface->release_timer =
      wl_event_loop_add_timer(loop, release_buffer, surface);
  wl_resource_set_implementation(surface->resource, &surface_impl, surface,
                                 surface_destroyed);
  surfaces++;
}
static void add_region(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t width, int32_t height) {
  (void)client;
  struct test_region *rects = wl_resource_get_user_data(resource);
  if (!rects->first.width)
    rects->first = (struct test_rect){x, y, width, height};
  rects->last = (struct test_rect){x, y, width, height};
  rects->count++;
}
static void region_destroyed(struct wl_resource *resource) {
  free(wl_resource_get_user_data(resource));
}
static const struct wl_region_interface region_impl = {
    .destroy = destroy_request, .add = add_region, .subtract = rectangle};
static void create_region(struct wl_client *client,
                          struct wl_resource *resource, uint32_t id) {
  (void)resource;
  struct wl_resource *region =
      wl_resource_create(client, &wl_region_interface, 1, id);
  struct test_region *rect = calloc(1, sizeof(*rect));
  assert(rect);
  wl_resource_set_implementation(region, &region_impl, rect, region_destroyed);
}
static const struct wl_compositor_interface compositor_impl = {
    .create_surface = create_surface, .create_region = create_region};
static void bind_compositor(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
  (void)data;
  struct wl_resource *resource =
      wl_resource_create(client, &wl_compositor_interface, (int)version, id);
  wl_resource_set_implementation(resource, &compositor_impl, NULL, NULL);
}
static void output_events(struct wl_resource *resource,
                          struct monitor *monitor) {
  wl_output_send_geometry(resource, 0, 0, 300, 200, 0, "test", "fixture", 0);
  wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
                      monitor->width * monitor->scale,
                      monitor->height * monitor->scale, 60000);
  if (wl_resource_get_version(resource) >= 2)
    wl_output_send_scale(resource, monitor->scale);
  if (wl_resource_get_version(resource) >= 4) {
    wl_output_send_name(resource, monitor->name);
    wl_output_send_description(resource, "Bongo Cat regression output");
  }
  if (wl_resource_get_version(resource) >= 2)
    wl_output_send_done(resource);
}
static void output_destroyed(struct wl_resource *resource) {
  struct monitor *monitor = wl_resource_get_user_data(resource);
  for (size_t i = 0; i < 32; i++)
    if (monitor->resources[i] == resource)
      monitor->resources[i] = NULL;
}
static const struct wl_output_interface output_impl = {.release =
                                                           destroy_request};
static void bind_output(struct wl_client *client, void *data, uint32_t version,
                        uint32_t id) {
  struct monitor *monitor = data;
  struct wl_resource *resource =
      wl_resource_create(client, &wl_output_interface, (int)version, id);
  wl_resource_set_implementation(resource, &output_impl, monitor,
                                 output_destroyed);
  for (size_t i = 0; i < 32; i++)
    if (!monitor->resources[i]) {
      monitor->resources[i] = resource;
      break;
    }
  output_events(resource, monitor);
}
static void layer_size(struct wl_client *client, struct wl_resource *resource,
                       uint32_t width, uint32_t height) {
  (void)client;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (surface->height != height || surface->width != width)
    surface->configure_pending = true;
  surface->height = height;
  surface->width = width;
}
static void unsigned_request(struct wl_client *client,
                             struct wl_resource *resource, uint32_t value) {
  (void)client;
  (void)resource;
  (void)value;
}
static void margin(struct wl_client *client, struct wl_resource *resource,
                   int32_t top, int32_t right, int32_t bottom, int32_t left) {
  (void)client;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  surface->margin_top = top;
  surface->margin_bottom = bottom;
  surface->margin_left = left;
  if (drag_mode) {
    printf("margin %s %d %d\n", surface->monitor->name, top, bottom);
    printf("placement %s %s %d %d %d %d %u %u\n", surface->monitor->name,
           surface->ns, top, right, bottom, left, surface->width,
           surface->height);
    fflush(stdout);
  }
}
static void popup(struct wl_client *client, struct wl_resource *resource,
                  struct wl_resource *object) {
  (void)client;
  (void)resource;
  (void)object;
}
static void layer_destroyed(struct wl_resource *resource) {
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (surface)
    surface->layer = NULL;
}
static const struct zwlr_layer_surface_v1_interface layer_impl = {
    .set_size = layer_size,
    .set_anchor = unsigned_request,
    .set_exclusive_zone = integer_request,
    .set_margin = margin,
    .set_keyboard_interactivity = unsigned_request,
    .get_popup = popup,
    .ack_configure = unsigned_request,
    .destroy = destroy_request,
    .set_layer = unsigned_request};
static void get_layer(struct wl_client *client, struct wl_resource *resource,
                      uint32_t id, struct wl_resource *surface_resource,
                      struct wl_resource *output_resource, uint32_t layer,
                      const char *namespace) {
  (void)layer;
  (void)namespace;
  struct test_surface *surface = wl_resource_get_user_data(surface_resource);
  assert(output_resource);
  surface->monitor = wl_resource_get_user_data(output_resource);
  snprintf(surface->ns, sizeof(surface->ns), "%s", namespace ? namespace : "");
  surface->layered = true;
  live++;
  if (!surface->monitor->surface)
    surface->monitor->surface = surface;
  else if (!surface->monitor->extra)
    surface->monitor->extra = surface;
  surface->configure_pending = true;
  surface->layer = wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                                      wl_resource_get_version(resource), id);
  wl_resource_set_implementation(surface->layer, &layer_impl, surface,
                                 layer_destroyed);
  printf("overlay %s %s\n", surface->monitor->name, surface->ns);
  fflush(stdout);
}
static const struct zwlr_layer_shell_v1_interface shell_impl = {
    .get_layer_surface = get_layer, .destroy = destroy_request};
static void bind_shell(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id) {
  (void)data;
  struct wl_resource *resource = wl_resource_create(
      client, &zwlr_layer_shell_v1_interface, (int)version, id);
  wl_resource_set_implementation(resource, &shell_impl, NULL, NULL);
}
static void viewport_source(struct wl_client *client,
                            struct wl_resource *resource, wl_fixed_t x,
                            wl_fixed_t y, wl_fixed_t width, wl_fixed_t height) {
  (void)client;
  (void)resource;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}
static void viewport_destination(struct wl_client *client,
                                 struct wl_resource *resource, int32_t width,
                                 int32_t height) {
  (void)client;
  (void)resource;
  assert(width > 0 && height > 0);
}
static const struct wp_viewport_interface viewport_impl = {
    .destroy = destroy_request,
    .set_source = viewport_source,
    .set_destination = viewport_destination};
static void get_viewport(struct wl_client *client, struct wl_resource *resource,
                         uint32_t id, struct wl_resource *surface_resource) {
  (void)resource;
  (void)surface_resource;
  struct wl_resource *viewport =
      wl_resource_create(client, &wp_viewport_interface, 1, id);
  wl_resource_set_implementation(viewport, &viewport_impl, NULL, NULL);
}
static const struct wp_viewporter_interface viewporter_impl = {
    .destroy = destroy_request, .get_viewport = get_viewport};
static void bind_viewporter(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
  (void)data;
  struct wl_resource *resource =
      wl_resource_create(client, &wp_viewporter_interface, (int)version, id);
  wl_resource_set_implementation(resource, &viewporter_impl, NULL, NULL);
}
static void scale_destroyed(struct wl_resource *resource) {
  for (size_t i = 0; i < 32; i++)
    if (fractional_objects[i] == resource)
      fractional_objects[i] = NULL;
}
static const struct wp_fractional_scale_v1_interface scale_impl = {
    .destroy = destroy_request};
static void get_scale(struct wl_client *client, struct wl_resource *resource,
                      uint32_t id, struct wl_resource *surface_resource) {
  (void)resource;
  struct test_surface *surface = wl_resource_get_user_data(surface_resource);
  struct wl_resource *scale =
      wl_resource_create(client, &wp_fractional_scale_v1_interface, 1, id);
  wl_resource_set_implementation(scale, &scale_impl, surface->monitor,
                                 scale_destroyed);
  for (size_t i = 0; i < 32; i++)
    if (!fractional_objects[i]) {
      fractional_objects[i] = scale;
      break;
    }
  wp_fractional_scale_v1_send_preferred_scale(
      scale, measure_mode                                ? 120
             : !strcmp(surface->monitor->name, "TEST-1") ? 150
                                                         : 240);
}
static const struct wp_fractional_scale_manager_v1_interface
    scale_manager_impl = {.destroy = destroy_request,
                          .get_fractional_scale = get_scale};
static void bind_scale(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id) {
  (void)data;
  struct wl_resource *resource = wl_resource_create(
      client, &wp_fractional_scale_manager_v1_interface, (int)version, id);
  wl_resource_set_implementation(resource, &scale_manager_impl, NULL, NULL);
}
static void toplevel_destroyed(struct wl_resource *resource) {
  if (toplevel == resource)
    toplevel = NULL;
}
static const struct zwlr_foreign_toplevel_handle_v1_interface toplevel_impl = {
    .destroy = destroy_request};
static void stop_manager(struct wl_client *client,
                         struct wl_resource *resource) {
  (void)client;
  zwlr_foreign_toplevel_manager_v1_send_finished(resource);
}
static const struct zwlr_foreign_toplevel_manager_v1_interface manager_impl = {
    .stop = stop_manager};
static void bind_fullscreen(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {
  (void)data;
  struct wl_resource *resource = wl_resource_create(
      client, &zwlr_foreign_toplevel_manager_v1_interface, (int)version, id);
  wl_resource_set_implementation(resource, &manager_impl, NULL, NULL);
  toplevel = wl_resource_create(
      client, &zwlr_foreign_toplevel_handle_v1_interface, (int)version, 0);
  wl_resource_set_implementation(toplevel, &toplevel_impl, NULL,
                                 toplevel_destroyed);
  zwlr_foreign_toplevel_manager_v1_send_toplevel(resource, toplevel);
  for (size_t m = 0; m < 2; m++)
    for (size_t i = 0; i < 32; i++)
      if (monitors[m].resources[i] &&
          wl_resource_get_client(monitors[m].resources[i]) == client)
        zwlr_foreign_toplevel_handle_v1_send_output_enter(
            toplevel, monitors[m].resources[i]);
  struct wl_array state;
  wl_array_init(&state);
  uint32_t *values = wl_array_add(&state, 2 * sizeof(*values));
  assert(values);
  values[0] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
  values[1] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
  zwlr_foreign_toplevel_handle_v1_send_state(toplevel, &state);
  zwlr_foreign_toplevel_handle_v1_send_done(toplevel);
  wl_array_release(&state);
}
int step(void *data) {
  static unsigned phase;
  (void)data;
  phase++;
  if (phase == 1) {
    if (toplevel) {
      for (size_t i = 0; i < 32; i++)
        if (monitors[0].resources[i] &&
            wl_resource_get_client(monitors[0].resources[i]) ==
                wl_resource_get_client(toplevel))
          zwlr_foreign_toplevel_handle_v1_send_output_leave(
              toplevel, monitors[0].resources[i]);
      zwlr_foreign_toplevel_handle_v1_send_done(toplevel);
    }
    for (size_t i = 0; i < 32; i++)
      if (fractional_objects[i] &&
          wl_resource_get_user_data(fractional_objects[i]) == &monitors[0])
        wp_fractional_scale_v1_send_preferred_scale(fractional_objects[i], 180);
    monitors[0].width = 640;
    monitors[0].scale = 2;
    for (size_t i = 0; i < 32; i++)
      if (monitors[0].resources[i])
        output_events(monitors[0].resources[i], &monitors[0]);
  } else if (phase == 2) {
    wl_global_destroy(monitors[1].global);
    monitors[1].global = NULL;
  } else if (phase == 3)
    monitors[1].global = wl_global_create(server, &wl_output_interface, 4,
                                          &monitors[1], bind_output);
  else if (phase == 4) {
    // Exceed the socket send buffer and force the client to drain pending
    // events.
    char description[4000];
    memset(description, 'x', sizeof(description) - 1);
    description[3999] = '\0';
    for (size_t i = 0; i < 32; i++)
      if (monitors[0].resources[i])
        for (size_t n = 0; n < 512; n++)
          wl_output_send_description(monitors[0].resources[i], description);
  }
  printf("phase %u\n", phase);
  fflush(stdout);
  return 0;
}
static int stop(int signal, void *data) {
  (void)signal;
  (void)data;
  wl_display_terminate(server);
  return 0;
}
int main(void) {
  drag_mode = getenv("HERDCAT_TEST_DRAG") != NULL;
  measure_mode = getenv("HERDCAT_TEST_MEASURE") != NULL;
  if (measure_mode) {
    const char *frame_ms = getenv("HERDCAT_TEST_FRAME_MS");
    const char *release_ms = getenv("HERDCAT_TEST_RELEASE_MS");
    if (frame_ms)
      measure_frame_ms = atoi(frame_ms);
    if (release_ms)
      measure_release_ms = atoi(release_ms);
    assert(measure_frame_ms > 0 && measure_frame_ms <= 1000);
    assert(measure_release_ms > 0 && measure_release_ms <= 1000);
    printf("measure timing frame=%d release=%d\n", measure_frame_ms,
           measure_release_ms);
    monitors[0].width = 2560;
    monitors[0].height = 1440;
  }
  server = wl_display_create();
  assert(server);
  // Added in libwayland 1.23. Older libraries keep their fixed 4096-byte
  // buffer, which the queue pressure phase also fills.
#if WAYLAND_VERSION_MAJOR > 1 || \
    (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR >= 23)
  wl_display_set_default_max_buffer_size(server, 4 * 1024 * 1024);
#else
  // The runtime test skips its queue pressure phase when it sees this: a
  // stopped server with the fixed buffer simply drops the client.
  printf("small-buffers\n");
#endif
  loop = wl_display_get_event_loop(server);
  assert(wl_display_add_socket(server, "wayland-test") == 0);
  assert(wl_display_init_shm(server) == 0);
  wl_global_create(server, &wl_compositor_interface, 4, NULL, bind_compositor);
  wl_global_create(server, &zwlr_layer_shell_v1_interface, 4, NULL, bind_shell);
  wl_global_create(server, &wp_viewporter_interface, 1, NULL, bind_viewporter);
  wl_global_create(server, &wp_fractional_scale_manager_v1_interface, 1, NULL,
                   bind_scale);
  for (size_t i = 0; i < 2; i++)
    monitors[i].global = wl_global_create(server, &wl_output_interface, 4,
                                          &monitors[i], bind_output);
  struct wl_event_source *timer = wl_event_loop_add_timer(loop, step, NULL);
  wl_event_source_remove(timer);
  struct wl_event_source *steps[4];
  for (size_t i = 0; !drag_mode && !measure_mode && i < 4; i++) {
    steps[i] = wl_event_loop_add_timer(loop, step, NULL);
    wl_event_source_timer_update(steps[i], (int)(i + 1) * 700);
  }
  wl_event_loop_add_signal(loop, SIGTERM, stop, NULL);
  wl_global_create(server, &zwlr_foreign_toplevel_manager_v1_interface, 3, NULL,
                   bind_fullscreen);
  if (drag_mode) {
    wl_global_create(server, &wl_seat_interface, 5, NULL, bind_seat);
    wl_event_loop_add_fd(loop, STDIN_FILENO, WL_EVENT_READABLE, fixture_command,
                         NULL);
  }
  puts("ready");
  fflush(stdout);
  wl_display_run(server);
  printf("totals surfaces=%u commits=%u\n", surfaces, commits);
  fflush(stdout);
  wl_display_destroy_clients(server);
  wl_display_destroy(server);
  return 0;
}

// Small protocol fixture; intentionally has no renderer or input dependencies.
#define _GNU_SOURCE
#include "fullscreen-server.h"
#include "layer-server.h"
#include "scale-server.h"
#include "viewport-server.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>

static struct wl_display *server;
static struct wl_event_loop *loop;
static unsigned commits, surfaces;
static struct wl_resource *toplevel;
static struct wl_resource *test_pointer, *test_seat;
static bool drag_mode;
struct test_rect {
  int x, y, width, height;
};
static struct wl_resource *fractional_objects[32];
struct monitor {
  struct wl_global *global;
  struct wl_resource *resources[32];
  int width, height, scale;
  const char *name;
  struct test_surface *surface;
};
static struct monitor monitors[2] = {
    {.width = 800,  .height = 600, .scale = 1, .name = "TEST-1"},
    {.width = 1024, .height = 768, .scale = 1, .name = "TEST-2"}
};
struct test_surface {
  struct wl_resource *resource, *layer, *buffer;
  struct wl_listener buffer_destroy;
  struct wl_event_source *release_timer;
  struct monitor *monitor;
  unsigned height;
  struct test_rect input;
  bool configure_pending;
};
static void destroy_request(struct wl_client *client,
                            struct wl_resource *resource) {
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
  wl_list_remove(&surface->buffer_destroy.link);
  surface->buffer = NULL;
  release->destroyed.notify = delayed_gone;
  wl_resource_add_destroy_listener(release->buffer, &release->destroyed);
  release->timer = wl_event_loop_add_timer(loop, delayed_send, release);
  wl_event_source_timer_update(release->timer, 200);
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
static void frame(struct wl_client *client, struct wl_resource *resource,
                  uint32_t id) {
  (void)resource;
  struct wl_resource *callback =
      wl_resource_create(client, &wl_callback_interface, 1, id);
  wl_callback_send_done(callback, 0);
  wl_resource_destroy(callback);
}
static void input_region(struct wl_client *client, struct wl_resource *resource,
                         struct wl_resource *region) {
  (void)client;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  surface->input = region
                       ? *(struct test_rect *)wl_resource_get_user_data(region)
                       : (struct test_rect){0};
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
        (uint32_t)surface->monitor->width,
        surface->height ? surface->height : 50);
    surface->configure_pending = false;
  }
  if (drag_mode && surface->monitor) {
    printf("input %s %d %d %d %d\n", surface->monitor->name, surface->input.x,
           surface->input.y, surface->input.width, surface->input.height);
    fflush(stdout);
  }
  if (surface->buffer) {
    struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->buffer);
    assert(buffer && wl_shm_buffer_get_width(buffer) > 0);
    commits++;
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
  *(struct test_rect *)wl_resource_get_user_data(resource) =
      (struct test_rect){x, y, width, height};
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
  struct test_rect *rect = calloc(1, sizeof(*rect));
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
  (void)width;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (surface->height != height)
    surface->configure_pending = true;
  surface->height = height;
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
  (void)right;
  (void)left;
  struct test_surface *surface = wl_resource_get_user_data(resource);
  if (drag_mode) {
    printf("margin %s %d %d\n", surface->monitor->name, top, bottom);
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
  surface->monitor->surface = surface;
  surface->configure_pending = true;
  surface->layer = wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                                      wl_resource_get_version(resource), id);
  wl_resource_set_implementation(surface->layer, &layer_impl, surface,
                                 layer_destroyed);
  printf("overlay %s\n", surface->monitor->name);
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
      scale, !strcmp(surface->monitor->name, "TEST-1") ? 150 : 240);
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
static int step(void *data) {
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
static void pointer_destroyed(struct wl_resource *resource) {
  if (test_pointer == resource)
    test_pointer = NULL;
}
static void cursor_request(struct wl_client *client,
                           struct wl_resource *resource, uint32_t serial,
                           struct wl_resource *surface, int32_t x, int32_t y) {
  (void)client;
  (void)resource;
  (void)serial;
  (void)surface;
  (void)x;
  (void)y;
}
static const struct wl_pointer_interface pointer_impl = {
    .set_cursor = cursor_request, .release = destroy_request};
static void get_pointer(struct wl_client *client, struct wl_resource *resource,
                        uint32_t id) {
  test_pointer = wl_resource_create(client, &wl_pointer_interface,
                                    wl_resource_get_version(resource), id);
  wl_resource_set_implementation(test_pointer, &pointer_impl, NULL,
                                 pointer_destroyed);
}
static const struct wl_seat_interface seat_impl = {.get_pointer = get_pointer,
                                                   .release = destroy_request};
static void seat_destroyed(struct wl_resource *resource) {
  if (test_seat == resource)
    test_seat = NULL;
}
static void bind_seat(struct wl_client *client, void *data, uint32_t version,
                      uint32_t id) {
  (void)data;
  test_seat = wl_resource_create(client, &wl_seat_interface, (int)version, id);
  wl_resource_set_implementation(test_seat, &seat_impl, NULL, seat_destroyed);
  wl_seat_send_capabilities(test_seat, WL_SEAT_CAPABILITY_POINTER);
  wl_seat_send_name(test_seat, "fixture");
}
static int fixture_command(int fd, uint32_t mask, void *data) {
  (void)mask;
  (void)data;
  char line[128] = {0}, name[32], action[32];
  int dx, dy;
  ssize_t bytes = read(fd, line, sizeof(line) - 1);
  assert(bytes > 0);
  if (!strncmp(line, "step", 4)) {
    step(NULL);
  } else if (!strncmp(line, "capabilities", 12)) {
    wl_seat_send_capabilities(test_seat, WL_SEAT_CAPABILITY_POINTER);
  } else if (sscanf(line, "%31s %31s %d %d", action, name, &dx, &dy) == 4) {
    struct test_surface *surface = NULL;
    for (size_t i = 0; i < 2; i++)
      if (!strcmp(name, monitors[i].name))
        surface = monitors[i].surface;
    assert(surface && test_pointer);
    int x = surface->input.x + surface->input.width / 2;
    int y = surface->input.y + surface->input.height / 2;
    uint32_t serial = wl_display_next_serial(server);
    wl_pointer_send_enter(test_pointer, serial, surface->resource,
                          wl_fixed_from_int(x), wl_fixed_from_int(y));
    wl_pointer_send_button(test_pointer, serial, 0, 272,
                           WL_POINTER_BUTTON_STATE_PRESSED);
    wl_pointer_send_motion(test_pointer, 1, wl_fixed_from_int(x + dx),
                           wl_fixed_from_int(y + dy));
    if (!strcmp(action, "leave")) {
      wl_pointer_send_leave(test_pointer, serial, surface->resource);
    } else if (!strcmp(action, "lost")) {
      wl_seat_send_capabilities(test_seat, 0);
    } else {
      wl_pointer_send_button(test_pointer, serial, 2, 272,
                             WL_POINTER_BUTTON_STATE_RELEASED);
    }
    wl_pointer_send_frame(test_pointer);
  } else {
    assert(false);
  }
  return 0;
}
static int stop(int signal, void *data) {
  (void)signal;
  (void)data;
  wl_display_terminate(server);
  return 0;
}
int main(void) {
  drag_mode = getenv("BONGOCAT_TEST_DRAG") != NULL;
  server = wl_display_create();
  assert(server);
  wl_display_set_default_max_buffer_size(server, 4 * 1024 * 1024);
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
  for (size_t i = 0; !drag_mode && i < 4; i++) {
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

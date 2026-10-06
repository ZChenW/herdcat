#include "test_compositor_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct wl_resource *test_pointer, *test_seat;

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
void bind_seat(struct wl_client *client, void *data, uint32_t version,
               uint32_t id) {
  (void)data;
  test_seat = wl_resource_create(client, &wl_seat_interface, (int)version, id);
  wl_resource_set_implementation(test_seat, &seat_impl, NULL, seat_destroyed);
  wl_seat_send_capabilities(test_seat, WL_SEAT_CAPABILITY_POINTER);
  wl_seat_send_name(test_seat, "fixture");
}
static struct monitor *monitor_named(const char *name) {
  for (size_t i = 0; i < 2; i++)
    if (!strcmp(name, monitors[i].name))
      return &monitors[i];
  return NULL;
}
static void click_at(struct test_surface *surface, int x, int y, int button) {
  assert(surface && test_pointer && button > 0);
  uint32_t serial = wl_display_next_serial(server);
  wl_pointer_send_enter(test_pointer, serial, surface->resource,
                        wl_fixed_from_int(x), wl_fixed_from_int(y));
  wl_pointer_send_button(test_pointer, serial, 0, (uint32_t)button,
                         WL_POINTER_BUTTON_STATE_PRESSED);
  wl_pointer_send_button(test_pointer, serial, 1, (uint32_t)button,
                         WL_POINTER_BUTTON_STATE_RELEASED);
  wl_pointer_send_frame(test_pointer);
}
int fixture_command(int fd, uint32_t mask, void *data) {
  (void)mask;
  (void)data;
  char line[128] = {0}, name[32], action[32];
  int dx, dy, x, y, button;
  ssize_t bytes = read(fd, line, sizeof(line) - 1);
  assert(bytes > 0);
  if (!strncmp(line, "step", 4)) {
    step(NULL);
  } else if (sscanf(line, "out %31s", name) == 1) {
    for (size_t i = 0; i < 2; i++) {
      struct test_surface *surface = monitors[i].surface;
      if (!strcmp(name, monitors[i].name) && surface && test_pointer) {
        wl_pointer_send_leave(test_pointer, wl_display_next_serial(server),
                              surface->resource);
        wl_pointer_send_frame(test_pointer);
      }
    }
  } else if (!strncmp(line, "capabilities", 12)) {
    wl_seat_send_capabilities(test_seat, WL_SEAT_CAPABILITY_POINTER);
  } else if (sscanf(line, "tap %31s %d %d %d", name, &x, &y, &button) == 4 ||
             sscanf(line, "panel %31s %d %d %d", name, &x, &y, &button) == 4) {
    struct monitor *monitor = monitor_named(name);
    struct test_surface *hit = NULL;
    if (monitor)
      hit = line[0] == 'p' ? monitor->extra : monitor->surface;
    click_at(hit, x, y, button);
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

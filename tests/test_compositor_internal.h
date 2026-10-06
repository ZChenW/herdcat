#ifndef HERDCAT_TEST_COMPOSITOR_INTERNAL_H
#define HERDCAT_TEST_COMPOSITOR_INTERNAL_H

#include <wayland-server.h>

struct test_rect {
  int x, y, width, height;
};
struct monitor {
  struct wl_global *global;
  struct wl_resource *resources[32];
  int width, height, scale;
  const char *name;
  struct test_surface *surface, *extra;
};
struct test_surface {
  struct wl_resource *resource, *layer, *buffer;
  struct wl_listener buffer_destroy;
  struct wl_event_source *release_timer;
  struct monitor *monitor;
  unsigned width, height;
  struct test_rect input, cat_input;
  unsigned input_count;
  int margin_top, margin_bottom, margin_left;
  bool configure_pending, layered;
  char ns[64];
};

extern struct wl_display *server;
extern struct monitor monitors[2];
void destroy_request(struct wl_client *client, struct wl_resource *resource);
int step(void *data);
void bind_seat(struct wl_client *client, void *data, uint32_t version,
               uint32_t id);
int fixture_command(int fd, uint32_t mask, void *data);

#endif

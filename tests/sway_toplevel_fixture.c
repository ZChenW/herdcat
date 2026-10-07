#define _POSIX_C_SOURCE 200809L
#include "xdg-shell-client.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

// One real xdg-toplevel per process; closing the window deliberately keeps
// its PID alive so the test distinguishes IPC removal from pidfd removal.
typedef struct {
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *wm;
  struct wl_surface *surface;
  struct xdg_surface *xdg;
  struct xdg_toplevel *toplevel;
  struct wl_buffer *buffer;
} client_t;

static void close_window(client_t *client) {
  if (!client->surface)
    return;
  xdg_toplevel_destroy(client->toplevel);
  xdg_surface_destroy(client->xdg);
  wl_surface_destroy(client->surface);
  client->toplevel = NULL;
  client->xdg = NULL;
  client->surface = NULL;
  puts("closed (process remains alive)");
  fflush(stdout);
}

static void ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener WM_LISTENER = {.ping = ping};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
  (void)version;
  client_t *client = data;
  if (!strcmp(interface, "wl_compositor"))
    client->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  else if (!strcmp(interface, "wl_shm"))
    client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  else if (!strcmp(interface, "xdg_wm_base")) {
    client->wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(client->wm, &WM_LISTENER, client);
  }
}
static void global_remove(void *data, struct wl_registry *registry,
                          uint32_t name) {
  (void)data;
  (void)registry;
  (void)name;
}
static const struct wl_registry_listener REGISTRY_LISTENER = {
    .global = global, .global_remove = global_remove};

static void configure(void *data, struct xdg_surface *xdg, uint32_t serial) {
  client_t *client = data;
  xdg_surface_ack_configure(xdg, serial);
  wl_surface_attach(client->surface, client->buffer, 0, 0);
  wl_surface_damage(client->surface, 0, 0, 160, 120);
  wl_surface_commit(client->surface);
}
static const struct xdg_surface_listener SURFACE_LISTENER = {.configure =
                                                                 configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states) {
  (void)data;
  (void)toplevel;
  (void)width;
  (void)height;
  (void)states;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
  (void)toplevel;
  close_window(data);
}
static const struct xdg_toplevel_listener TOPLEVEL_LISTENER = {
    .configure = toplevel_configure, .close = toplevel_close};

static struct wl_buffer *make_buffer(struct wl_shm *shm) {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  char path[256];
  if (!runtime || snprintf(path, sizeof(path), "%s/pixels-XXXXXX", runtime) >=
                      (int)sizeof(path))
    return NULL;
  int fd = mkstemp(path);
  if (fd < 0)
    return NULL;
  unlink(path);
  const size_t bytes = 160 * 120 * sizeof(uint32_t);
  if (ftruncate(fd, (off_t)bytes) < 0) {
    close(fd);
    return NULL;
  }
  uint32_t *pixels =
      mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (pixels == MAP_FAILED) {
    close(fd);
    return NULL;
  }
  for (size_t i = 0; i < bytes / sizeof(*pixels); i++)
    pixels[i] = UINT32_C(0xff245878);
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)bytes);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, 160, 120, 160 * 4, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  munmap(pixels, bytes);
  close(fd);
  return buffer;
}

static int run(client_t *client) {
  while (true) {
    if (wl_display_dispatch_pending(client->display) < 0)
      return 1;
    // Outgoing messages are tiny, but honor flush backpressure as well.
    short events = POLLIN;
    if (wl_display_flush(client->display) < 0) {
      if (errno != EAGAIN)
        return 1;
      events |= POLLOUT;
    }
    struct pollfd fds[] = {
        {.fd = wl_display_get_fd(client->display), .events = events},
        {.fd = STDIN_FILENO,                       .events = POLLIN}
    };
    if (poll(fds, 2, -1) < 0) {
      if (errno == EINTR)
        continue;
      return 1;
    }
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
      return 1;
    if ((fds[0].revents & POLLIN) && wl_display_dispatch(client->display) < 0)
      return 1;
    if (fds[1].revents & (POLLIN | POLLHUP)) {
      char command[128];
      if (!fgets(command, sizeof(command), stdin))
        return 0;
      command[strcspn(command, "\n")] = '\0';
      if (!strcmp(command, "quit"))
        return 0;
      if (!strcmp(command, "close"))
        close_window(client);
      else if (!strncmp(command, "title ", 6) && client->toplevel) {
        xdg_toplevel_set_title(client->toplevel, command + 6);
        printf("title %s\n", command + 6);
        fflush(stdout);
      } else {
        fprintf(stderr, "unknown fixture command: %s\n", command);
        return 1;
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s TITLE\n", argv[0]);
    return 1;
  }
  setvbuf(stdin, NULL, _IONBF, 0);
  client_t client = {0};
  client.display = wl_display_connect(NULL);
  if (!client.display) {
    perror("wl_display_connect");
    return 1;
  }
  struct wl_registry *registry = wl_display_get_registry(client.display);
  wl_registry_add_listener(registry, &REGISTRY_LISTENER, &client);
  if (wl_display_roundtrip(client.display) < 0 || !client.compositor ||
      !client.shm || !client.wm) {
    fprintf(stderr, "missing compositor/shm/xdg_wm_base or failed roundtrip\n");
    wl_display_disconnect(client.display);
    return 1;
  }
  client.buffer = make_buffer(client.shm);
  if (!client.buffer) {
    perror("make_buffer");
    wl_display_disconnect(client.display);
    return 1;
  }
  client.surface = wl_compositor_create_surface(client.compositor);
  client.xdg = xdg_wm_base_get_xdg_surface(client.wm, client.surface);
  xdg_surface_add_listener(client.xdg, &SURFACE_LISTENER, &client);
  client.toplevel = xdg_surface_get_toplevel(client.xdg);
  xdg_toplevel_add_listener(client.toplevel, &TOPLEVEL_LISTENER, &client);
  xdg_toplevel_set_app_id(client.toplevel, "herdcat-sway-test");
  xdg_toplevel_set_title(client.toplevel, argv[1]);
  wl_surface_commit(client.surface);
  int result = run(&client);
  if (result)
    fprintf(stderr, "Wayland fixture failed: display error %d (%s)\n",
            wl_display_get_error(client.display), strerror(errno));
  close_window(&client);
  wl_buffer_destroy(client.buffer);
  xdg_wm_base_destroy(client.wm);
  wl_shm_destroy(client.shm);
  wl_compositor_destroy(client.compositor);
  wl_registry_destroy(registry);
  wl_display_flush(client.display);
  wl_display_disconnect(client.display);
  return result;
}

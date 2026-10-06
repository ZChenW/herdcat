#define _GNU_SOURCE
#include "platform/compositor.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static unsigned socket_calls, connect_calls;
int __wrap_socket(int domain, int type, int protocol);
int __wrap_connect(int fd, const struct sockaddr *address, socklen_t length);
int __wrap_socket(int domain, int type, int protocol) {
  (void)domain;
  (void)type;
  (void)protocol;
  socket_calls++;
  errno = EPERM;
  return -1;
}
int __wrap_connect(int fd, const struct sockaddr *address, socklen_t length) {
  (void)fd;
  (void)address;
  (void)length;
  connect_calls++;
  errno = EPERM;
  return -1;
}
static int hypr(const char *text, focus_watch_event_t *event) {
  return compositor_hyprland_event(text, strlen(text), event);
}
static int sway(const char *text, focus_watch_event_t *event) {
  return compositor_sway_event(text, strlen(text), event);
}
static void detection(void) {
  unsetenv("NIRI_SOCKET");
  unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
  unsetenv("SWAYSOCK");
  TEST_ASSERT(!compositor_detect(false) && !compositor_detect(true));
  setenv("SWAYSOCK", "/unreachable-test-sway", 1);
  TEST_ASSERT(!compositor_detect(false));
  TEST_ASSERT(compositor_detect(true) == &COMPOSITOR_SWAY);
  TEST_ASSERT(COMPOSITOR_SWAY.connect() == 0);
  TEST_ASSERT(COMPOSITOR_SWAY.timeout() == -1);
  TEST_ASSERT(!focus_available());
  setenv("HYPRLAND_INSTANCE_SIGNATURE", "test-only", 1);
  TEST_ASSERT(!compositor_detect(false));
  TEST_ASSERT(compositor_detect(true) == &COMPOSITOR_HYPRLAND);
  TEST_ASSERT(COMPOSITOR_HYPRLAND.connect() == 0);
  TEST_ASSERT(COMPOSITOR_HYPRLAND.timeout() == -1);
  setenv("NIRI_SOCKET", "/unreachable-test-niri", 1);
  TEST_ASSERT(compositor_detect(false) == &COMPOSITOR_NIRI);
  TEST_ASSERT(compositor_detect(true) == &COMPOSITOR_NIRI);
  // No connect is called after enabling; all paths above test the gate only.
  setenv("XDG_RUNTIME_DIR", "/tmp", 1);
  char path[128];
  TEST_ASSERT(compositor_hyprland_path(path, sizeof(path)));
  TEST_ASSERT(!strcmp(path, "/tmp/hypr/test-only/.socket2.sock"));
  setenv("HYPRLAND_INSTANCE_SIGNATURE", "../bad", 1);
  TEST_ASSERT(!compositor_hyprland_path(path, sizeof(path)));
}
static void parsers(void) {
  focus_watch_event_t event;
  // Hyprland IPC documents EVENT>>DATA plus these exact payload layouts.
  TEST_ASSERT(hypr("workspace>>2", &event) == 0);
  TEST_ASSERT(hypr("activewindow>>kitty,notes, with comma", &event) == 2);
  TEST_ASSERT(!strcmp(event.title, "notes, with comma"));
  TEST_ASSERT(hypr("activewindowv2>>64cea2525760", &event) == 1);
  TEST_ASSERT(event.kind == FOCUS_WATCH_FOCUS &&
              event.id == UINT64_C(0x64cea2525760));
  TEST_ASSERT(hypr("activewindowv2>>", &event) == 1 && event.id_null);
  TEST_ASSERT(hypr("openwindow>>64cea2525760,2,kitty,notes", &event) == 2);
  TEST_ASSERT(event.id == UINT64_C(0x64cea2525760) &&
              !strcmp(event.title, "notes"));
  TEST_ASSERT(hypr("windowtitlev2>>64cea2525760,✳ notes", &event) == 2);
  TEST_ASSERT(hypr("closewindow>>64cea2525760", &event) == 1 &&
              event.kind == FOCUS_WATCH_CLOSE);
  TEST_ASSERT(hypr("openwindow>>1,missing", &event) == -1);
  TEST_ASSERT(hypr("activewindowv2>>bad;exec", &event) == -1);
  TEST_ASSERT(hypr("activewindowv2>>10000000000000000", &event) == -1);
  TEST_ASSERT(hypr("activewindowv2>", &event) == -1);
  focus_window_t windows[4];
  const char *clients = "[{\"address\":\"0x64cea2525760\",\"pid\":23959,"
                        "\"title\":\"✳ notes\",\"workspace\":{\"id\":2}}]";
  TEST_ASSERT(
      compositor_hyprland_windows(clients, strlen(clients), windows, 4) == 1);
  TEST_ASSERT(windows[0].pid == 23959 && windows[0].resting_since_ms == 1);
  TEST_ASSERT(
      compositor_hyprland_windows(clients, strlen(clients), windows, 0) == -1);
  for (size_t i = 0; i < strlen(clients); i++)
    TEST_ASSERT(compositor_hyprland_windows(clients, i, windows, 4) == -1);
  // Projection of sway-ipc(7)'s WINDOW new example (id 12, pid 19787).
  const char *new = "{\"change\":\"new\",\"container\":{\"id\":12,\"name\":"
                    "null,\"focused\":false,\"pid\":19787,\"type\":\"con\","
                    "\"nodes\":[],\"floating_nodes\":[]}}";
  TEST_ASSERT(sway(new, &event) == 1 && event.kind == FOCUS_WATCH_UPSERT);
  TEST_ASSERT(event.id == 12 && event.pid == 19787 && !event.title[0]);
  for (size_t i = 0; i < strlen(new); i++)
    TEST_ASSERT(compositor_sway_event(new, i, &event) == -1);
  TEST_ASSERT(sway("{\"change\":\"focus\",\"container\":{\"id\":12,\"pid\":"
                   "19787,\"name\":\"✳ notes\"}}",
                   &event) == 1);
  TEST_ASSERT(event.has_focused && event.focused == 12 && event.resting);
  TEST_ASSERT(
      sway("{\"change\":\"close\",\"container\":{\"id\":12}}", &event) == 1);
  TEST_ASSERT(event.kind == FOCUS_WATCH_CLOSE);
  TEST_ASSERT(sway("{\"change\":\"urgent\"}", &event) == 0);
  TEST_ASSERT(
      sway("{\"change\":\"focus\",\"container\":{\"id\":-1}}", &event) == -1);
  // GET_TREE example's root and urxvt node, plus a floating regression node.
  const char *tree =
      "{\"id\":1,\"name\":\"root\",\"focused\":false,\"nodes\":[{\"id\":5,"
      "\"name\":\"urxvt\",\"pid\":23959,\"nodes\":[]}],\"floating_nodes\":[{"
      "\"id\":12,\"pid\":19787,\"focused\":true}]}";
  TEST_ASSERT(compositor_sway_tree(tree, strlen(tree), windows, 4) == 2);
  TEST_ASSERT(windows[0].id == 5 && windows[0].pid == 23959);
  TEST_ASSERT(windows[1].id == 12 && windows[1].pid == 19787);
  TEST_ASSERT(sway(tree, &event) == 1 && event.id == 12);
  TEST_ASSERT(compositor_sway_tree(tree, strlen(tree), windows, 1) == -1);
  for (size_t i = 0; i < strlen(tree); i++)
    TEST_ASSERT(compositor_sway_tree(tree, i, windows, 4) == -1);
}
static void commands(void) {
  const char *args[8] = {0};
  char text[64];
  TEST_ASSERT(COMPOSITOR_NIRI.focus_window(42, args, text, sizeof(text)));
  TEST_ASSERT(!strcmp(args[0], "niri") && !strcmp(args[3], "focus-window") &&
              !strcmp(args[5], "42"));
  TEST_ASSERT(
      COMPOSITOR_HYPRLAND.focus_window(0x123abc, args, text, sizeof(text)));
  TEST_ASSERT(!strcmp(args[0], "hyprctl") &&
              !strcmp(args[3], "address:0x123abc") && !args[4]);
  TEST_ASSERT(COMPOSITOR_SWAY.focus_window(12, args, text, sizeof(text)));
  TEST_ASSERT(!strcmp(args[0], "swaymsg") &&
              !strcmp(args[2], "[con_id=12] focus") && !args[3]);
  TEST_ASSERT(!COMPOSITOR_SWAY.focus_window(12, args, text, 2));
  unsigned char frame[64];
  uint32_t length, type;
  size_t n = compositor_sway_message(2, "[\"window\"]", frame, sizeof(frame));
  TEST_ASSERT(n == 24 && !memcmp(frame, "i3-ipc", 6));
  for (size_t i = 0; i < 14; i++)
    TEST_ASSERT(compositor_sway_header(frame, i, &length, &type) == 0);
  TEST_ASSERT(compositor_sway_header(frame, n, &length, &type) == 1);
  TEST_ASSERT(type == 2 && length == 10 &&
              !memcmp(frame + 14, "[\"window\"]", 10));
  TEST_ASSERT(compositor_sway_message(4, "", frame, sizeof(frame)) == 14);
  TEST_ASSERT(compositor_sway_header(frame, 14, &length, &type) == 1 &&
              type == 4 && !length);
  length = 65537;
  memcpy(frame + 6, &length, 4);
  TEST_ASSERT(compositor_sway_header(frame, 14, &length, &type) == -1);
  frame[0] = 'x';
  TEST_ASSERT(compositor_sway_header(frame, 14, &length, &type) == -1);
}
int main(void) {
  detection();
  TEST_ASSERT(socket_calls == 0 && connect_calls == 0);
  parsers();
  commands();
  puts("Compositor parsers, commands and experimental gate passed");
  return 0;
}

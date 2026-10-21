#define _POSIX_C_SOURCE 200809L
#include "compositor_internal.h"
#include "platform/compositor.h"
#include "platform/focus.h"
#include "platform/focus_watch.h"
#include "utils/json.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static bool sway_window(json_span_t item, focus_window_t *out, bool *focused) {
  json_span_t v;
  uint64_t id, pid;
  if (!json_field(item, "id", &v) || !json_uint(v, &id) || !id)
    return false;
  *out = (focus_window_t){.id = id};
  if (json_field(item, "pid", &v)) {
    if (!json_uint(v, &pid) || pid > INT_MAX)
      return false;
    out->pid = (pid_t)pid;
  }
  if (json_field(item, "name", &v))
    json_text(v, out->title, sizeof(out->title));
  *focused = json_field(item, "focused", &v) && v.end - v.p == 4 &&
             !memcmp(v.p, "true", 4);
  return true;
}
static bool tree(json_span_t root, focus_window_t *out, size_t capacity,
                 size_t *count, unsigned depth) {
  if (depth > 32)
    return false;
  focus_window_t window;
  bool focused;
  if (!sway_window(root, &window, &focused))
    return false;
  if (window.pid) {
    if (*count == capacity)
      return false;
    out[(*count)++] = window;
  }
  const char *keys[] = {"nodes", "floating_nodes"};
  for (int k = 0; k < 2; k++) {
    json_span_t children, item;
    if (!json_field(root, keys[k], &children))
      continue;
    if (*children.p != '[')
      return false;
    for (size_t i = 0; json_item(children, i, &item); i++)
      if (!tree(item, out, capacity, count, depth + 1))
        return false;
  }
  return true;
}
int compositor_sway_tree(const char *text, size_t length, focus_window_t *out,
                         size_t capacity) {
  json_span_t root;
  size_t count = 0;
  if (!out || !json_document(text, length, &root) ||
      !tree(root, out, capacity, &count, 0))
    return -1;
  return (int)count;
}
// The full snapshot's focused field is represented as an ordinary focus event.
static bool focused_tree(json_span_t root, uint64_t *id) {
  focus_window_t window;
  bool focused;
  if (!sway_window(root, &window, &focused))
    return false;
  if (focused && window.pid) {
    *id = window.id;
    return true;
  }
  const char *keys[] = {"nodes", "floating_nodes"};
  for (int k = 0; k < 2; k++) {
    json_span_t children, item;
    if (!json_field(root, keys[k], &children))
      continue;
    for (size_t i = 0; json_item(children, i, &item); i++)
      if (focused_tree(item, id))
        return true;
  }
  return false;
}
int compositor_sway_event(const char *text, size_t length,
                          focus_watch_event_t *event) {
  json_span_t root, change, container;
  if (!event || !json_document(text, length, &root))
    return -1;
  *event = (focus_watch_event_t){0};
  if (!json_field(root, "change", &change)) {
    focus_window_t window;
    bool focused;
    if (!sway_window(root, &window, &focused))
      return -1;
    // GET_TREE includes focus; an unfocused tree clears it.
    event->kind = FOCUS_WATCH_FOCUS;
    event->id_null = !focused_tree(root, &event->id);
    return 1;
  }
  bool close = json_equal(change, "close");
  bool focus = json_equal(change, "focus");
  if (!close && !focus && !json_equal(change, "new") &&
      !json_equal(change, "title") && !json_equal(change, "move"))
    return 0;
  focus_window_t window;
  bool focused;
  if (!json_field(root, "container", &container) ||
      !sway_window(container, &window, &focused))
    return -1;
  event->kind = close ? FOCUS_WATCH_CLOSE : FOCUS_WATCH_UPSERT;
  event->id = window.id;
  event->pid = window.pid;
  memcpy(event->title, window.title, sizeof(event->title));
  event->has_focused = focus || focused;
  event->focused = event->has_focused ? window.id : 0;
  return 1;
}
size_t compositor_sway_message(uint32_t type, const char *payload, void *out,
                               size_t size) {
  size_t n = strlen(payload);
  if (n > 65536 || size < 14 + n)
    return 0;
  uint32_t length = (uint32_t)n;
  memcpy(out, "i3-ipc", 6);
  memcpy((char *)out + 6, &length, 4);
  memcpy((char *)out + 10, &type, 4);
  memcpy((char *)out + 14, payload, n);
  return n + 14;
}
int compositor_sway_header(const void *data, size_t size, uint32_t *length,
                           uint32_t *type) {
  if (size < 14)
    return 0;
  if (memcmp(data, "i3-ipc", 6))
    return -1;
  memcpy(length, (const char *)data + 6, 4);
  memcpy(type, (const char *)data + 10, 4);
  return *length <= 65536 ? 1 : -1;
}
static bool sway_detect(void) {
  const char *path = getenv("SWAYSOCK");
  struct stat st;
  return path && *path && stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
}
static int sway_connect(void) {
  if (compositor_selected() != &COMPOSITOR_SWAY)
    return 0;
  return compositor_stream_connect(true);
}
static void sway_windows(const char **args) {
  const char *a[] = {"swaymsg", "-r", "-t", "get_tree", NULL};
  memcpy((void *)args, (const void *)a, sizeof(a));
}
static bool sway_focus(uint64_t id, const char **args, char *text,
                       size_t size) {
  if (!id)
    return false;
  int n = snprintf(text, size, "[con_id=%" PRIu64 "] focus", id);
  if (n < 0 || (size_t)n >= size)
    return false;
  const char *a[] = {"swaymsg", "-r", text, NULL};
  memcpy((void *)args, (const void *)a, sizeof(a));
  return true;
}
const compositor_ops_t COMPOSITOR_SWAY = {.name = "Sway",
                                          .detect = sway_detect,
                                          .connect = sway_connect,
                                          .events = compositor_stream_events,
                                          .ready = compositor_stream_ready,
                                          .timeout = compositor_stream_timeout,
                                          .available =
                                              compositor_stream_available,
                                          .cleanup = compositor_stream_cleanup,
                                          .windows = sway_windows,
                                          .parse_windows = compositor_sway_tree,
                                          .focus_window = sway_focus};

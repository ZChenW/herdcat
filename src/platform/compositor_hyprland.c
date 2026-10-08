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

bool compositor_hyprland_address(const char *text, uint64_t *id) {
  if (!text || !id)
    return false;
  if (!strncmp(text, "0x", 2))
    text += 2;
  size_t n = strlen(text);
  if (!n || n > 16)
    return false;
  *id = 0;
  for (size_t i = 0; i < n; i++) {
    const char *d = strchr("0123456789abcdef", text[i]);
    if (!d)
      return false;
    *id = *id * 16 + (uint64_t)(d - "0123456789abcdef");
  }
  return true;
}
bool compositor_hyprland_path(char *path, size_t size) {
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  const char *signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!runtime || *runtime != '/' || !signature || !*signature ||
      strlen(signature) > 200 ||
      strspn(signature, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01"
                        "23456789_-.") != strlen(signature) ||
      !strcmp(signature, ".") || !strcmp(signature, ".."))
    return false;
  int n = snprintf(path, size, "%s/hypr/%s/.socket2.sock", runtime, signature);
  return n >= 0 && (size_t)n < size;
}
int compositor_hyprland_windows(const char *text, size_t length,
                                focus_window_t *out, size_t capacity) {
  json_span_t root, item, v;
  if (!out || !json_document(text, length, &root) || *root.p != '[')
    return -1;
  int count = 0;
  for (size_t i = 0; json_item(root, i, &item); i++) {
    char address[32];
    uint64_t id, pid;
    if (!json_field(item, "address", &v) ||
        !json_text(v, address, sizeof(address)) ||
        !compositor_hyprland_address(address, &id) || !id ||
        !json_field(item, "pid", &v) || !json_uint(v, &pid) || pid > INT_MAX)
      return -1;
    if (!pid)
      continue;
    if ((size_t)count == capacity)
      return -1;
    out[count] = (focus_window_t){.id = id, .pid = (pid_t)pid};
    if (json_field(item, "title", &v))
      json_text(v, out[count].title, sizeof(out[count].title));
    if (!strncmp(out[count].title, "\xe2\x9c\xb3", 3))
      out[count].resting_since_ms = 1;
    count++;
  }
  return count;
}
// Return 2 when an event requires a fresh client list (events omit PIDs).
int compositor_hyprland_event(const char *text, size_t length,
                              focus_watch_event_t *event) {
  if (!text || !event || length > 65536 || memchr(text, 0, length))
    return -1;
  const char *sep = NULL;
  for (size_t i = 0; i + 1 < length; i++)
    if (text[i] == '>' && text[i + 1] == '>') {
      sep = text + i;
      break;
    }
  if (!sep)
    return -1;
  size_t n = (size_t)(sep - text);
  char payload[65537];
  memcpy(payload, sep + 2, length - n - 2);
  payload[length - n - 2] = 0;
  *event = (focus_watch_event_t){0};
  bool focus = n == 14 && !memcmp(text, "activewindowv2", 14);
  bool close = n == 11 && !memcmp(text, "closewindow", 11);
  if (focus || close) {
    if (focus && !*payload) {
      event->id_null = true;
    } else if (!compositor_hyprland_address(payload, &event->id) || !event->id)
      return -1;
    event->kind = focus ? FOCUS_WATCH_FOCUS : FOCUS_WATCH_CLOSE;
    return 1;
  }
  if (n == 12 && !memcmp(text, "activewindow", 12)) {
    char *comma = strchr(payload, ',');
    if (!comma)
      return -1;
    snprintf(event->title, sizeof(event->title), "%s", comma + 1);
    return 2;
  }
  bool open = n == 10 && !memcmp(text, "openwindow", 10);
  bool title = n == 11 && !memcmp(text, "windowtitle", 11);
  bool title_v2 = n == 13 && !memcmp(text, "windowtitlev2", 13);
  if (open || title || title_v2) {
    char *rest = strchr(payload, ',');
    if (rest)
      *rest++ = 0;
    if (!compositor_hyprland_address(payload, &event->id) || !event->id)
      return -1;
    if (open) {
      for (int i = 0; i < 2; i++) {
        if (!rest)
          return -1;
        rest = strchr(rest, ',');
        if (rest)
          rest++;
      }
    }
    if ((open || title_v2) && !rest)
      return -1;
    event->kind = FOCUS_WATCH_UPSERT;
    if (rest)
      snprintf(event->title, sizeof(event->title), "%s", rest);
    return 2;
  }
  return 0;
}
static bool hypr_detect(void) {
  char path[108];
  struct stat st;
  return compositor_hyprland_path(path, sizeof(path)) && stat(path, &st) == 0 &&
         S_ISSOCK(st.st_mode);
}
static int hypr_connect(void) {
  if (compositor_selected() != &COMPOSITOR_HYPRLAND)
    return 0;
  return compositor_stream_connect(false);
}
static void hypr_windows(const char **args) {
  const char *a[] = {"hyprctl", "-j", "clients", NULL};
  memcpy((void *)args, (const void *)a, sizeof(a));
}
static bool hypr_focus(uint64_t id, const char **args, char *text,
                       size_t size) {
  if (!id)
    return false;
  int n = snprintf(text, size, "address:0x%" PRIx64, id);
  if (n < 0 || (size_t)n >= size)
    return false;
  const char *a[] = {"hyprctl", "dispatch", "focuswindow", text, NULL};
  memcpy((void *)args, (const void *)a, sizeof(a));
  return true;
}
const compositor_ops_t COMPOSITOR_HYPRLAND = {
    .name = "Hyprland (experimental)",
    .detect = hypr_detect,
    .connect = hypr_connect,
    .events = compositor_stream_events,
    .ready = compositor_stream_ready,
    .timeout = compositor_stream_timeout,
    .available = compositor_stream_available,
    .cleanup = compositor_stream_cleanup,
    .windows = hypr_windows,
    .parse_windows = compositor_hyprland_windows,
    .focus_window = hypr_focus};

#define _GNU_SOURCE
#include "platform/agent_terminal.h"
#include "platform/focus.h"
#include "platform/focus_watch.h"
#include "utils/json_string.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define STREAM_LINE 65536
typedef struct {
  const char *p, *end;
} json_t;
static void space(json_t *j) {
  while (j->p < j->end && strchr(" \n\r\t", *j->p)) {
    j->p++;
  }
}
static bool take(json_t *j, char c) {
  space(j);
  if (j->p == j->end || *j->p != c) {
    return false;
  }
  j->p++;
  return true;
}
static bool key_string(json_t *j, const char **start, size_t *length) {
  if (!take(j, '"')) {
    return false;
  }
  const char *s = j->p;
  while (j->p < j->end) {
    unsigned char c = (unsigned char)*j->p++;
    if (c == '"') {
      *start = s;
      *length = (size_t)(j->p - s - 1);
      return true;
    }
    if (c < 32) {
      return false;
    }
    if (c != '\\') {
      continue;
    }
    if (j->p == j->end) {
      return false;
    }
    c = (unsigned char)*j->p++;
    if (c == 'u') {
      for (int i = 0; i < 4; i++) {
        if (j->p == j->end)
          return false;
        char hex = *j->p++;
        if (!strchr("0123456789abcdefABCDEF", hex)) {
          return false;
        }
      }
    } else if (!strchr("\"\\/bfnrt", c)) {
      return false;
    }
  }
  return false;
}
static bool digit(json_t *j) {
  return j->p < j->end && *j->p >= '0' && *j->p <= '9';
}
static bool number(json_t *j) {
  if (j->p < j->end && *j->p == '-') {
    j->p++;
  }
  if (!digit(j)) {
    return false;
  }
  if (*j->p++ != '0') {
    while (digit(j)) {
      j->p++;
    }
  }
  if (j->p < j->end && *j->p == '.') {
    j->p++;
    if (!digit(j)) {
      return false;
    }
    while (digit(j)) {
      j->p++;
    }
  }
  if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
    j->p++;
    if (j->p < j->end && (*j->p == '+' || *j->p == '-')) {
      j->p++;
    }
    if (!digit(j)) {
      return false;
    }
    while (digit(j)) {
      j->p++;
    }
  }
  return true;
}
static bool value(json_t *j, unsigned depth);
static bool integer(json_t *j, uint64_t *out) {
  space(j);
  *out = 0;
  if (!digit(j)) {
    return false;
  }
  const char *start = j->p;
  do {
    unsigned d = (unsigned)(*j->p++ - '0');
    if (*out > (UINT64_MAX - d) / 10) {
      return false;
    }
    *out = *out * 10 + d;
  } while (digit(j));
  return j->p - start == 1 || *start != '0';
}
static bool value(json_t *j, unsigned depth) {
  space(j);
  if (depth > 32 || j->p == j->end) {
    return false;
  }
  char c = *j->p;
  if (c == '"') {
    const char *s;
    size_t n;
    return key_string(j, &s, &n);
  }
  if (c == '{') {
    j->p++;
    space(j);
    if (j->p < j->end && *j->p == '}') {
      j->p++;
      return true;
    }
    do {
      const char *s;
      size_t n;
      if (!key_string(j, &s, &n) || !take(j, ':') || !value(j, depth + 1)) {
        return false;
      }
      space(j);
      if (j->p < j->end && *j->p == '}') {
        j->p++;
        return true;
      }
      if (!take(j, ',')) {
        return false;
      }
    } while (true);
  }
  if (c == '[') {
    j->p++;
    space(j);
    if (j->p < j->end && *j->p == ']') {
      j->p++;
      return true;
    }
    do {
      if (!value(j, depth + 1)) {
        return false;
      }
      space(j);
      if (j->p < j->end && *j->p == ']') {
        j->p++;
        return true;
      }
      if (!take(j, ',')) {
        return false;
      }
    } while (true);
  }
  if (j->end - j->p >= 4 && !memcmp(j->p, "true", 4)) {
    j->p += 4;
    return true;
  }
  if (j->end - j->p >= 5 && !memcmp(j->p, "false", 5)) {
    j->p += 5;
    return true;
  }
  if (j->end - j->p >= 4 && !memcmp(j->p, "null", 4)) {
    j->p += 4;
    return true;
  }
  return number(j);
}
static bool same(const char *key, size_t n, const char *literal) {
  size_t length = strlen(literal);
  return n == length && !memcmp(key, literal, length);
}
static int window_fields(json_t *j, uint64_t *id, uint64_t *pid, bool *has_pid,
                         bool *is_focused, char *stored_title) {
  stored_title[0] = 0;
  *id = 0;
  *pid = 0;
  *has_pid = false;
  bool has_id = false;
  if (!take(j, '{')) {
    return -1;
  }
  space(j);
  if (j->p < j->end && *j->p == '}') {
    j->p++;
    return has_id ? 0 : -1;
  }
  do {
    const char *key;
    size_t n;
    if (!key_string(j, &key, &n) || !take(j, ':')) {
      return -1;
    }
    if (same(key, n, "id")) {
      if (has_id || !integer(j, id)) {
        return -1;
      }
      has_id = true;
    } else if (same(key, n, "pid")) {
      if (*has_pid) {
        return -1;
      }
      *has_pid = true;
      space(j);
      if (j->end - j->p >= 4 && !memcmp(j->p, "null", 4)) {
        j->p += 4;
      } else if (!integer(j, pid) || *pid > INT_MAX) {
        return -1;
      }
    } else if (same(key, n, "title")) {
      space(j);
      if (j->p < j->end && *j->p == '"') {
        const char *title;
        size_t length;
        if (!key_string(j, &title, &length))
          return -1;
        json_string_copy(title, length, stored_title,
                         AGENT_TERMINAL_TITLE_MAX + 1);
      } else if (!value(j, 1)) {
        return -1;
      }
    } else if (same(key, n, "is_focused")) {
      space(j);
      if (j->end - j->p >= 4 && !memcmp(j->p, "true", 4)) {
        j->p += 4;
        *is_focused = true;
      } else if (j->end - j->p >= 5 && !memcmp(j->p, "false", 5)) {
        j->p += 5;
      } else {
        return -1;
      }
    } else if (!value(j, 1)) {
      return -1;
    }
    space(j);
    if (j->p < j->end && *j->p == '}') {
      j->p++;
      break;
    }
    if (!take(j, ',')) {
      return -1;
    }
  } while (true);
  return has_id ? 0 : -1;
}
static int take_id_field(json_t *j, focus_watch_event_t *event) {
  if (!take(j, '{')) {
    return -1;
  }
  const char *key;
  size_t n;
  if (!key_string(j, &key, &n) || !same(key, n, "id") || !take(j, ':')) {
    return -1;
  }
  space(j);
  if (j->end - j->p >= 4 && !memcmp(j->p, "null", 4)) {
    j->p += 4;
    event->id_null = true;
  } else if (!integer(j, &event->id)) {
    return -1;
  }
  return take(j, '}') ? 0 : -1;
}

static int parse_windows_changed(json_t *j, focus_watch_event_t *event,
                                 focus_window_t *out, size_t capacity) {
  if (!take(j, '{')) {
    return -1;
  }
  const char *key;
  size_t kn;
  if (!key_string(j, &key, &kn) || !same(key, kn, "windows") || !take(j, ':') ||
      !take(j, '[')) {
    return -1;
  }
  int count = 0;
  space(j);
  if (!(j->p < j->end && *j->p == ']')) {
    do {
      uint64_t id = 0, pid = 0;
      bool has_pid = false, is_focused = false;
      char title[AGENT_TERMINAL_TITLE_MAX + 1];
      if (window_fields(j, &id, &pid, &has_pid, &is_focused, title) < 0) {
        return -1;
      }
      if (is_focused && id) {
        event->has_focused = true;
        event->focused = id;
      }
      if (has_pid && pid && out && (size_t)count < capacity) {
        out[count] = (focus_window_t){.id = id, .pid = (pid_t)pid};
        memcpy(out[count].title, title, sizeof(title));
      }
      if (has_pid && pid) {
        count++;
      }
      space(j);
      if (j->p < j->end && *j->p == ']') {
        j->p++;
        break;
      }
      if (!take(j, ',')) {
        return -1;
      }
    } while (true);
  } else {
    j->p++;
  }
  if (!take(j, '}')) {
    return -1;
  }
  if ((size_t)count > capacity) {
    return -1;
  }
  event->kind = FOCUS_WATCH_WINDOWS;
  event->count = count;
  return 1;
}

int focus_watch_parse(const char *line, size_t length,
                      focus_watch_event_t *event, focus_window_t *out,
                      size_t capacity) {
  if (!line || !event || length > STREAM_LINE || memchr(line, 0, length)) {
    return -1;
  }
  *event = (focus_watch_event_t){0};
  json_t j = {line, line + length};
  if (!take(&j, '{')) {
    return -1;
  }
  const char *name;
  size_t n;
  if (!key_string(&j, &name, &n) || !take(&j, ':')) {
    return -1;
  }
  int result = 0;
  if (same(name, n, "WindowFocusChanged")) {
    event->kind = FOCUS_WATCH_FOCUS;
    if (take_id_field(&j, event) < 0) {
      return -1;
    }
    result = 1;
  } else if (same(name, n, "WindowClosed")) {
    event->kind = FOCUS_WATCH_CLOSE;
    if (take_id_field(&j, event) < 0 || event->id_null) {
      return -1;
    }
    result = 1;
  } else if (same(name, n, "WindowOpenedOrChanged")) {
    if (!take(&j, '{')) {
      return -1;
    }
    const char *key;
    size_t kn;
    if (!key_string(&j, &key, &kn) || !same(key, kn, "window") ||
        !take(&j, ':')) {
      return -1;
    }
    uint64_t id = 0, pid = 0;
    bool has_pid = false, is_focused = false;
    if (window_fields(&j, &id, &pid, &has_pid, &is_focused, event->title) < 0 ||
        !take(&j, '}')) {
      return -1;
    }
    event->kind = FOCUS_WATCH_UPSERT;
    event->has_focused = is_focused;
    event->focused = is_focused ? id : 0;
    event->id = id;
    event->pid = has_pid ? (pid_t)pid : 0;
    result = 1;
  } else if (same(name, n, "WindowsChanged")) {
    result = parse_windows_changed(&j, event, out, capacity);
    if (result < 0)
      return -1;
  } else if (!value(&j, 0)) {
    return -1;
  }
  return take(&j, '}') ? result : -1;
}

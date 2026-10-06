#include "platform/focus.h"
#include "utils/json_string.h"

#include <limits.h>
#include <string.h>

typedef struct {
  const char *p, *end;
} json_t;
static void space(json_t *j) {
  while (j->p < j->end && strchr(" \n\r\t", *j->p))
    j->p++;
}
static bool take(json_t *j, char c) {
  space(j);
  if (j->p == j->end || *j->p != c)
    return false;
  j->p++;
  return true;
}
static bool string(json_t *j, const char **start, size_t *length) {
  if (!take(j, '"'))
    return false;
  const char *s = j->p;
  while (j->p < j->end) {
    unsigned char c = (unsigned char)*j->p++;
    if (c == '"') {
      *start = s;
      *length = (size_t)(j->p - s - 1);
      return true;
    }
    if (c < 32)
      return false;
    if (c != '\\')
      continue;
    if (j->p == j->end)
      return false;
    c = (unsigned char)*j->p++;
    if (c == 'u') {
      for (int i = 0; i < 4; i++) {
        if (j->p == j->end || !strchr("0123456789abcdefABCDEF", *j->p++))
          return false;
      }
    } else if (!strchr("\"\\/bfnrt", c))
      return false;
  }
  return false;
}
static bool digit(json_t *j) {
  return j->p < j->end && *j->p >= '0' && *j->p <= '9';
}
static bool number(json_t *j) {
  if (j->p < j->end && *j->p == '-')
    j->p++;
  if (!digit(j))
    return false;
  if (*j->p++ != '0')
    while (digit(j))
      j->p++;
  if (j->p < j->end && *j->p == '.') {
    j->p++;
    if (!digit(j))
      return false;
    while (digit(j))
      j->p++;
  }
  if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
    j->p++;
    if (j->p < j->end && (*j->p == '+' || *j->p == '-'))
      j->p++;
    if (!digit(j))
      return false;
    while (digit(j))
      j->p++;
  }
  return true;
}
static bool value(json_t *j, unsigned depth) {
  space(j);
  if (depth > 32 || j->p == j->end)
    return false;
  const char *s;
  size_t n;
  if (*j->p == '"')
    return string(j, &s, &n);
  if (*j->p == '{' || *j->p == '[') {
    bool object = *j->p++ == '{';
    char close = object ? '}' : ']';
    if (take(j, close))
      return true;
    do {
      if (object && (!string(j, &s, &n) || !take(j, ':')))
        return false;
      if (!value(j, depth + 1))
        return false;
      if (take(j, close))
        return true;
    } while (take(j, ','));
    return false;
  }
  const char *literals[] = {"true", "false", "null"};
  for (unsigned i = 0; i < 3; i++) {
    n = strlen(literals[i]);
    if ((size_t)(j->end - j->p) >= n && !memcmp(j->p, literals[i], n)) {
      j->p += n;
      return true;
    }
  }
  return number(j);
}
static bool integer(json_t *j, uint64_t *out) {
  space(j);
  *out = 0;
  if (!digit(j))
    return false;
  const char *start = j->p;
  do {
    unsigned d = (unsigned)(*j->p++ - '0');
    if (*out > (UINT64_MAX - d) / 10)
      return false;
    *out = *out * 10 + d;
  } while (digit(j));
  return j->p - start == 1 || *start != '0';
}
int focus_parse_windows(const char *json, size_t length, focus_window_t *out,
                        size_t capacity) {
  if (!json || !out || length > 65535 || memchr(json, 0, length))
    return -1;
  json_t j = {json, json + length};
  size_t count = 0;
  if (!take(&j, '['))
    return -1;
  if (!take(&j, ']')) {
    do {
      if (!take(&j, '{'))
        return -1;
      uint64_t id = 0, pid = 0;
      bool has_id = false, has_pid = false;
      char title[AGENT_TERMINAL_TITLE_MAX + 1] = {0};
      if (!take(&j, '}')) {
        do {
          const char *key;
          size_t n;
          if (!string(&j, &key, &n) || !take(&j, ':'))
            return -1;
          if (n == 2 && !memcmp(key, "id", 2)) {
            if (has_id || !integer(&j, &id))
              return -1;
            has_id = true;
          } else if (n == 3 && !memcmp(key, "pid", 3)) {
            if (has_pid)
              return -1;
            has_pid = true;
            space(&j);
            if (j.end - j.p >= 4 && !memcmp(j.p, "null", 4))
              j.p += 4;
            else if (!integer(&j, &pid) || pid > INT_MAX)
              return -1;
          } else if (n == 5 && !memcmp(key, "title", 5)) {
            space(&j);
            if (j.p < j.end && *j.p == '"') {
              const char *raw;
              size_t length_title;
              if (!string(&j, &raw, &length_title))
                return -1;
              json_string_copy(raw, length_title, title, sizeof(title));
            } else if (!value(&j, 0)) {
              return -1;
            }
          } else if (!value(&j, 0))
            return -1;
          if (take(&j, '}'))
            break;
          if (!take(&j, ','))
            return -1;
        } while (true);
      }
      if (!has_id || !has_pid)
        return -1;
      if (pid) {
        if (count >= capacity)
          return -1;
        out[count] = (focus_window_t){.id = id, .pid = (pid_t)pid};
        memcpy(out[count++].title, title, sizeof(title));
      }
      if (take(&j, ']'))
        break;
      if (!take(&j, ','))
        return -1;
    } while (true);
  }
  space(&j);
  return j.p == j.end ? (int)count : -1;
}

int focus_parse_wezterm(const char *json, size_t length, bool clients,
                        focus_wezterm_pane_t *out, size_t capacity) {
  if (!json || !out || length > 65535 || memchr(json, 0, length))
    return -1;
  json_t j = {json, json + length};
  size_t count = 0;
  if (!take(&j, '['))
    return -1;
  if (!take(&j, ']')) {
    do {
      if (!take(&j, '{'))
        return -1;
      focus_wezterm_pane_t pane = {0};
      bool has_pane = false, usable = false, has_title = false;
      if (!take(&j, '}')) {
        do {
          const char *key;
          size_t n;
          if (!string(&j, &key, &n) || !take(&j, ':'))
            return -1;
          const char *field = clients ? "focused_pane_id" : "pane_id";
          if (n == strlen(field) && !memcmp(key, field, n)) {
            if (has_pane)
              return -1;
            has_pane = true;
            space(&j);
            if (clients && j.end - j.p >= 4 && !memcmp(j.p, "null", 4)) {
              j.p += 4;
            } else {
              if (!integer(&j, &pane.pane))
                return -1;
              usable = true;
            }
          } else if (n == 9 && !memcmp(key, "window_id", 9)) {
            if (pane.has_window || !integer(&j, &pane.window))
              return -1;
            pane.has_window = true;
          } else if (n == 5 && !memcmp(key, "title", 5)) {
            const char *raw;
            size_t bytes;
            if (has_title || !string(&j, &raw, &bytes))
              return -1;
            json_string_copy(raw, bytes, pane.title, sizeof(pane.title));
            has_title = true;
          } else if (!value(&j, 0)) {
            return -1;
          }
          if (take(&j, '}'))
            break;
          if (!take(&j, ','))
            return -1;
        } while (true);
      }
      if (!has_pane)
        return -1;
      if (usable) {
        if (count >= capacity)
          return -1;
        out[count++] = pane;
      }
      if (take(&j, ']'))
        break;
      if (!take(&j, ','))
        return -1;
    } while (true);
  }
  space(&j);
  return j.p == j.end ? (int)count : -1;
}

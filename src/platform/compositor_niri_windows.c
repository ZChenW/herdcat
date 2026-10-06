#include "focus_json_internal.h"
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

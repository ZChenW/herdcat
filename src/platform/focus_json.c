#include "focus_json_internal.h"
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

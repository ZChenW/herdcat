#define _GNU_SOURCE
#include "platform/focus_watch.h"

#include "platform/agent_watch.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define STREAM_TOKEN 1U
#define STREAM_LINE  65536
#define WINDOW_MAX   128

typedef struct {
  const char *p, *end;
} json_t;
static int stream_fd = -1;
static bool connecting, disabled;
static int backoff_ms;
static int64_t retry_at;
static bool have_focus;
static uint64_t focused_id;
static focus_window_t windows[WINDOW_MAX];
static size_t window_count;
static char pending[STREAM_LINE];
static size_t pending_used;
static bool skipping;

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
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
        if (j->p == j->end || !strchr("0123456789abcdefABCDEF", *j->p++)) {
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
                         bool *is_focused) {
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
    if (window_fields(&j, &id, &pid, &has_pid, &is_focused) < 0 ||
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
    if (!take(&j, '{')) {
      return -1;
    }
    const char *key;
    size_t kn;
    if (!key_string(&j, &key, &kn) || !same(key, kn, "windows") ||
        !take(&j, ':') || !take(&j, '[')) {
      return -1;
    }
    int count = 0;
    space(&j);
    if (!(j.p < j.end && *j.p == ']')) {
      do {
        uint64_t id = 0, pid = 0;
        bool has_pid = false, is_focused = false;
        if (window_fields(&j, &id, &pid, &has_pid, &is_focused) < 0) {
          return -1;
        }
        if (is_focused && id) {
          event->has_focused = true;
          event->focused = id;
        }
        if (has_pid && pid && out && (size_t)count < capacity) {
          out[count] = (focus_window_t){id, (pid_t)pid};
        }
        if (has_pid && pid) {
          count++;
        }
        space(&j);
        if (j.p < j.end && *j.p == ']') {
          j.p++;
          break;
        }
        if (!take(&j, ',')) {
          return -1;
        }
      } while (true);
    } else {
      j.p++;
    }
    if (!take(&j, '}')) {
      return -1;
    }
    if ((size_t)count > capacity) {
      return -1;
    }
    event->kind = FOCUS_WATCH_WINDOWS;
    event->count = count;
    result = 1;
  } else if (!value(&j, 0)) {
    return -1;
  }
  return take(&j, '}') ? result : -1;
}

static void schedule_retry(void) {
  if (stream_fd >= 0) {
    agent_watch_unlisten(stream_fd);
    close(stream_fd);
    stream_fd = -1;
  }
  connecting = false;
  have_focus = false;
  focused_id = 0;
  window_count = 0;
  pending_used = 0;
  skipping = false;
  if (backoff_ms < 1000) {
    backoff_ms = 1000;
  } else if (backoff_ms < 30000) {
    backoff_ms *= 2;
  }
  retry_at = now_ms() + backoff_ms;
}
static void apply_event(const focus_watch_event_t *event,
                        const focus_window_t *parsed) {
  if (event->kind == FOCUS_WATCH_FOCUS) {
    have_focus = !event->id_null && event->id;
    focused_id = have_focus ? event->id : 0;
  } else if (event->kind == FOCUS_WATCH_CLOSE) {
    size_t kept = 0;
    for (size_t i = 0; i < window_count; i++) {
      if (windows[i].id != event->id) {
        windows[kept++] = windows[i];
      }
    }
    window_count = kept;
    if (have_focus && focused_id == event->id) {
      have_focus = false;
      focused_id = 0;
    }
  } else if (event->kind == FOCUS_WATCH_UPSERT && event->pid > 0) {
    bool replaced = false;
    for (size_t i = 0; i < window_count; i++) {
      if (windows[i].id == event->id) {
        windows[i].pid = event->pid;
        replaced = true;
        break;
      }
    }
    if (!replaced && window_count < WINDOW_MAX) {
      windows[window_count++] = (focus_window_t){event->id, event->pid};
    }
    if (event->has_focused) {
      have_focus = true;
      focused_id = event->focused;
    }
  } else if (event->kind == FOCUS_WATCH_WINDOWS) {
    window_count = 0;
    for (int i = 0; i < event->count && window_count < WINDOW_MAX; i++) {
      windows[window_count++] = parsed[i];
    }
    // The first line of a new stream is the full window list. No separate
    // focus event follows it, so this is the only source of the initial focus.
    have_focus = event->has_focused;
    focused_id = have_focus ? event->focused : 0;
  }
}
static void consume_lines(void) {
  char chunk[2048];
  ssize_t count;
  while ((count = read(stream_fd, chunk, sizeof(chunk))) > 0) {
    for (ssize_t i = 0; i < count; i++) {
      char c = chunk[i];
      if (c != '\n') {
        if (pending_used + 1 >= STREAM_LINE) {
          skipping = true;
        } else if (!skipping) {
          pending[pending_used++] = c;
        }
        continue;
      }
      if (!skipping && pending_used) {
        focus_watch_event_t event;
        focus_window_t parsed[WINDOW_MAX];
        int parsed_count = focus_watch_parse(pending, pending_used, &event,
                                             parsed, WINDOW_MAX);
        if (parsed_count > 0) {
          apply_event(&event, parsed);
        }
      }
      pending_used = 0;
      skipping = false;
    }
  }
  if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
    schedule_retry();
  }
}
static int send_request(int fd) {
  const char request[] = "\"EventStream\"\n";
  size_t sent = 0;
  while (sent < sizeof(request) - 1) {
    ssize_t n =
        send(fd, request + sent, sizeof(request) - 1 - sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && errno == EAGAIN) {
      return 0;
    }
    if (n <= 0) {
      return -1;
    }
    sent += (size_t)n;
  }
  backoff_ms = 1000;
  return 0;
}
static void on_ready(uint32_t token) {
  if (token != STREAM_TOKEN || stream_fd < 0) {
    return;
  }
  if (connecting) {
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(stream_fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 ||
        error) {
      schedule_retry();
      return;
    }
    connecting = false;
    agent_watch_unlisten(stream_fd);
    if (send_request(stream_fd) < 0 ||
        agent_watch_listen(stream_fd, STREAM_TOKEN, EPOLLIN) < 0) {
      schedule_retry();
      return;
    }
  }
  consume_lines();
}
static int open_stream(void) {
  const char *path = getenv("NIRI_SOCKET");
  if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    return -1;
  }
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  memcpy(address.sun_path, path, strlen(path) + 1);
  int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
  connecting = result < 0 && errno == EINPROGRESS;
  if (result < 0 && !connecting) {
    close(fd);
    return -1;
  }
  uint32_t events = connecting ? EPOLLOUT : EPOLLIN;
  if (!connecting && send_request(fd) < 0) {
    close(fd);
    return -1;
  }
  if (agent_watch_listen(fd, STREAM_TOKEN, events) < 0) {
    close(fd);
    return -1;
  }
  stream_fd = fd;
  return 0;
}

int focus_watch_init(void) {
  agent_watch_on_ready(on_ready);
  if (agent_watch_fd() < 0 || !focus_available()) {
    disabled = true;
    return 0;
  }
  if (open_stream() < 0) {
    schedule_retry();
  }
  return 0;
}
void focus_watch_cleanup(void) {
  if (stream_fd >= 0) {
    agent_watch_unlisten(stream_fd);
    close(stream_fd);
    stream_fd = -1;
  }
  connecting = false;
  disabled = false;
  have_focus = false;
  focused_id = 0;
  window_count = 0;
  pending_used = 0;
  skipping = false;
}
void focus_watch_poll(void) {
  if (disabled || stream_fd >= 0 || now_ms() < retry_at) {
    return;
  }
  if (open_stream() < 0) {
    schedule_retry();
  }
}
int focus_watch_timeout(void) {
  if (disabled || stream_fd >= 0) {
    return -1;
  }
  int64_t remaining = retry_at - now_ms();
  if (remaining < 1) {
    return 1;
  }
  return remaining > INT_MAX ? INT_MAX : (int)remaining;
}
bool focus_watch_available(void) {
  return !disabled && stream_fd >= 0 && !connecting;
}
uint64_t focus_watch_focused_id(void) {
  return have_focus ? focused_id : 0;
}
uint64_t focus_watch_match(uint64_t focused, const focus_window_t *wins,
                           size_t windows_count,
                           const agent_session_view_t *sessions, size_t count) {
  if (!focused || !wins || !sessions) {
    return 0;
  }
  uint64_t best = 0;
  int64_t updated = -1;
  for (size_t i = 0; i < count; i++) {
    uint64_t id = 0;
    if (sessions[i].pid <= 1 ||
        !focus_find_window(sessions[i].pid, wins, windows_count, &id) ||
        id != focused) {
      continue;
    }
    if (sessions[i].updated_ms >= updated) {
      best = sessions[i].key;
      updated = sessions[i].updated_ms;
    }
  }
  return best;
}
int focus_watch_matching(uint64_t focused, const focus_window_t *wins,
                         size_t windows_count,
                         const agent_session_view_t *sessions, size_t count,
                         uint64_t *keys, size_t capacity) {
  if (!focused || !wins || !sessions || !keys || !capacity) {
    return 0;
  }
  size_t written = 0;
  for (size_t i = 0; i < count && written < capacity; i++) {
    uint64_t id = 0;
    if (sessions[i].pid <= 1 ||
        !focus_find_window(sessions[i].pid, wins, windows_count, &id) ||
        id != focused) {
      continue;
    }
    keys[written++] = sessions[i].key;
  }
  return (int)written;
}
int focus_watch_focused_keys(const agent_session_view_t *sessions, size_t count,
                             uint64_t *keys, size_t capacity) {
  if (!focus_watch_available() || !have_focus) {
    return 0;
  }
  return focus_watch_matching(focused_id, windows, window_count, sessions,
                              count, keys, capacity);
}
uint64_t focus_watch_focused_session(const agent_session_view_t *sessions,
                                     size_t count) {
  if (!focus_watch_available() || !have_focus) {
    return 0;
  }
  return focus_watch_match(focused_id, windows, window_count, sessions, count);
}

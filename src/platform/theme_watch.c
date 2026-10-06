#define _GNU_SOURCE
#include "platform/theme_watch.h"

#include "graphics/sign_palette.h"
#include "platform/agent_watch.h"
#include "platform/command_job.h"
#include "utils/json.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define THEME_TOKEN 4U
static command_job_t initial = {.fd = -1}, monitor = {.fd = -1};
static bool enabled, fallback, skipping;
static int backoff;
static int64_t retry_at;
static char line[65536];
static size_t used;
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
bool theme_parse(const char *text, size_t length, bool signal,
                 sign_theme_t *theme) {
  json_span_t root, data, type, v;
  if (!theme || !json_document(text, length, &root))
    return false;
  if (signal) {
    json_span_t member, interface, payload, kind;
    if (!json_field(root, "type", &kind) || !json_equal(kind, "signal") ||
        !json_field(root, "member", &member) ||
        !json_equal(member, "SettingChanged") ||
        !json_field(root, "interface", &interface) ||
        !json_equal(interface, "org.freedesktop.portal.Settings") ||
        !json_field(root, "payload", &payload) ||
        !json_field(payload, "data", &data) || !json_item(data, 0, &v) ||
        !json_equal(v, "org.freedesktop.appearance") ||
        !json_item(data, 1, &v) || !json_equal(v, "color-scheme") ||
        !json_item(data, 2, &root) || json_item(data, 3, &v))
      return false;
  } else {
    // ReadOne returns v; older Read adds one extra v wrapper.
    for (int i = 0; i < 2; i++) {
      if (!json_field(root, "type", &type))
        return false;
      if (!json_equal(type, "v"))
        break;
      if (!json_field(root, "data", &data) || !json_item(data, 0, &v))
        return false;
      root = v;
    }
  }
  uint64_t scheme;
  if (!json_field(root, "type", &type) || !json_equal(type, "u") ||
      !json_field(root, "data", &data) || !json_uint(data, &scheme) ||
      scheme > 2)
    return false;
  *theme = scheme == 1 ? SIGN_THEME_DARK : SIGN_THEME_LIGHT;
  return true;
}
bool theme_tool_available(void) {
  const char *path = getenv("PATH");
  if (!path)
    return false;
  for (const char *p = path; p;) {
    const char *end = strchr(p, ':');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    char file[PATH_MAX];
    if (n + 8 < sizeof(file)) {
      memcpy(file, p, n);
      snprintf(file + n, sizeof(file) - n, "%sbusctl", n ? "/" : "");
      struct stat st;
      if (access(file, X_OK) == 0 && stat(file, &st) == 0 &&
          S_ISREG(st.st_mode))
        return true;
    }
    p = end ? end + 1 : NULL;
  }
  return false;
}
static void stop_jobs(void) {
  agent_watch_unlisten(initial.fd);
  agent_watch_unlisten(monitor.fd);
  job_cleanup(&initial);
  job_cleanup(&monitor);
  used = 0;
  skipping = false;
}
void theme_watch_cleanup(void) {
  stop_jobs();
  enabled = false;
  retry_at = 0;
  backoff = 0;
  sign_theme_system(SIGN_THEME_LIGHT);
}
static void retry(void) {
  stop_jobs();
  backoff = backoff ? (backoff < 16000 ? backoff * 2 : 30000) : 1000;
  retry_at = now_ms() + backoff;
}
static bool read_start(bool old) {
  const char *args[] = {"busctl",
                        "--user",
                        "--json=short",
                        "call",
                        "org.freedesktop.portal.Desktop",
                        "/org/freedesktop/portal/desktop",
                        "org.freedesktop.portal.Settings",
                        old ? "Read" : "ReadOne",
                        "ss",
                        "org.freedesktop.appearance",
                        "color-scheme",
                        NULL};
  if (job_start(&initial, args) < 0)
    return false;
  if (agent_watch_listen(initial.fd, THEME_TOKEN, EPOLLIN) < 0) {
    job_cleanup(&initial);
    return false;
  }
  return true;
}
static void start(void) {
  fallback = false;
  const char *args[] = {"busctl",
                        "--user",
                        "monitor",
                        "--json=short",
                        "--match",
                        "type='signal',interface='org.freedesktop.portal."
                        "Settings',member='SettingChanged'",
                        NULL};
  if (job_start(&monitor, args) < 0 ||
      agent_watch_listen(monitor.fd, THEME_TOKEN, EPOLLIN) < 0 ||
      !read_start(false))
    retry();
}
void theme_watch_configure(sign_theme_t choice) {
  bool want = choice == SIGN_THEME_AUTO && theme_tool_available();
  if (want == enabled)
    return;
  theme_watch_cleanup();
  enabled = want;
  if (enabled)
    start();
}
static void monitor_read(void) {
  char chunk[2048];
  ssize_t n = -1;
  // Bound each wake so a noisy bus cannot starve the renderer.
  for (int batch = 0; batch < 32; batch++) {
    n = read(monitor.fd, chunk, sizeof(chunk));
    if (n <= 0)
      break;
    for (ssize_t i = 0; i < n; i++) {
      if (chunk[i] == '\n') {
        sign_theme_t theme;
        if (!skipping && theme_parse(line, used, true, &theme)) {
          sign_theme_system(theme);
          // A signal newer than the pending initial read wins.
          agent_watch_unlisten(initial.fd);
          job_cleanup(&initial);
        }
        used = 0;
        skipping = false;
      } else if (!skipping) {
        if (used == sizeof(line))
          skipping = true;
        else
          line[used++] = chunk[i];
      }
    }
  }
  if (!n || (n < 0 && errno != EAGAIN && errno != EINTR))
    retry();
}
void theme_watch_poll(void) {
  if (!enabled)
    return;
  if (!monitor.pid) {
    if (now_ms() >= retry_at)
      start();
    return;
  }
  if (initial.pid) {
    int fd = initial.fd;
    int done = job_process(&initial);
    if (done) {
      agent_watch_unlisten(fd);
      sign_theme_t theme;
      if (done > 0 &&
          theme_parse(initial.buffer, initial.used, false, &theme)) {
        sign_theme_system(theme);
        backoff = 0;
      } else if (!fallback) {
        fallback = true;
        if (!read_start(true))
          retry();
      } else {
        retry();
      }
    } else if (initial.eof)
      agent_watch_unlisten(fd);
  }
  if (monitor.pid)
    monitor_read();
}
void theme_watch_ready(uint32_t token) {
  if (token == THEME_TOKEN)
    theme_watch_poll();
}
int theme_watch_timeout(void) {
  if (!enabled)
    return -1;
  int64_t left;
  if (!monitor.pid)
    left = retry_at - now_ms();
  else if (initial.pid) {
    left = initial.deadline - now_ms();
    if (initial.eof && left > 10)
      left = 10;
  } else
    return -1;
  return left < 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}

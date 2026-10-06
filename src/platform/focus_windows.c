#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "platform/focus.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef TEST_BUILD
static unsigned stat_reads;
void focus_reset_stat_reads(void) {
  stat_reads = 0;
}
unsigned focus_stat_reads(void) {
  return stat_reads;
}
#endif
static const char *without_tab_prefix(const char *title) {
  if (*title != '[')
    return title;
  const char *p = title + 1;
  if (*p < '0' || *p > '9')
    return title;
  while (*p >= '0' && *p <= '9')
    p++;
  if (*p != '/')
    return title;
  p++;
  if (*p < '0' || *p > '9')
    return title;
  while (*p >= '0' && *p <= '9')
    p++;
  return p[0] == ']' && p[1] == ' ' ? p + 2 : title;
}
bool focus_pick_window(pid_t pid, const focus_window_t *windows, size_t count,
                       const char *title, bool contains, uint64_t *id) {
  if (!windows || !id)
    return false;
  size_t first = count, candidates = 0;
  for (size_t i = 0; i < count; i++) {
    if (windows[i].pid == pid) {
      if (!candidates)
        first = i;
      candidates++;
    }
  }
  if (!candidates)
    return false;
  if (candidates == 1 || !title || !*title) {
    *id = windows[first].id;
    return true;
  }
  size_t best = first, matches = 0;
  int rank = 0;
  for (size_t i = 0; i < count; i++) {
    if (windows[i].pid != pid)
      continue;
    const char *candidate = without_tab_prefix(windows[i].title);
    size_t a = strlen(candidate), b = strlen(title);
    int score = 0;
    if (contains) {
      if (strstr(candidate, title)) {
        matches++;
        best = i;
      }
    } else {
      if (!strcmp(candidate, title))
        score = 2;
      else if (a >= b && (!memcmp(candidate, title, b) ||
                          !memcmp(candidate + a - b, title, b)))
        score = 1;
      if (score > rank) {
        rank = score;
        best = i;
      }
    }
  }
  *id = windows[(contains ? matches == 1 : rank > 0) ? best : first].id;
  return true;
}
bool focus_find_window_title(pid_t pid, const focus_window_t *windows,
                             size_t count, const char *title, bool contains,
                             uint64_t *id) {
  for (int depth = 0; depth < 16 && pid > 1; depth++) {
    if (focus_pick_window(pid, windows, count, title, contains, id))
      return true;
    char path[64], buffer[1024], comm[256];
    pid_t parent;
    snprintf(path, sizeof(path), "/proc/%jd/stat", (intmax_t)pid);
#ifdef TEST_BUILD
    stat_reads++;
#endif
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      return false;
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0)
      return false;
    buffer[n] = 0;
    if (agent_hook_parse_stat(buffer, comm, sizeof(comm), &parent) < 0 ||
        parent == pid)
      return false;
    pid = parent;
  }
  return false;
}
bool focus_find_window(pid_t pid, const focus_window_t *windows, size_t count,
                       uint64_t *id) {
  return focus_find_window_title(pid, windows, count, NULL, false, id);
}
static bool terminal_window_fallback(pid_t pid, const agent_terminal_t *t,
                                     const char *name,
                                     const focus_window_t *windows,
                                     size_t count, uint64_t *id) {
  if (t && t->kind == TERMINAL_WEZTERM && t->window) {
    for (size_t i = 0; i < count; i++)
      if (windows[i].id == t->window &&
          focus_find_window(pid, &windows[i], 1, id))
        return true;
  }
  if (t && t->kind == TERMINAL_TMUX)
    return focus_find_window(t->client_pid, windows, count, id);
  return focus_find_window_title(pid, windows, count,
                                 t && t->kind == TERMINAL_GHOSTTY ? name
                                 : t                              ? t->title
                                                                  : NULL,
                                 t && t->kind == TERMINAL_GHOSTTY, id);
}

bool focus_terminal_window_title(pid_t pid, const agent_terminal_t *t,
                                 const char *name, const char *title,
                                 const focus_window_t *windows, size_t count,
                                 uint64_t *id) {
  if (t && (t->kind == TERMINAL_GHOSTTY || t->kind == TERMINAL_WEZTERM) &&
      title && *title) {
    uint64_t first;
    if (focus_find_window(pid, windows, count, &first)) {
      pid_t owner = 0;
      for (size_t i = 0; i < count; i++)
        if (windows[i].id == first)
          owner = windows[i].pid;
      size_t matches = 0;
      for (size_t i = 0; i < count; i++)
        if (windows[i].pid == owner && strstr(windows[i].title, title)) {
          *id = windows[i].id;
          matches++;
        }
      if (matches == 1)
        return true;
    }
  }
  return terminal_window_fallback(pid, t, name, windows, count, id);
}
bool focus_terminal_window(pid_t pid, const agent_terminal_t *t,
                           const char *name, const focus_window_t *windows,
                           size_t count, uint64_t *id) {
  return focus_terminal_window_title(pid, t, name, NULL, windows, count, id);
}

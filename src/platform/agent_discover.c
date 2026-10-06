#define _GNU_SOURCE
#include "platform/agent_discover.h"

#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "platform/agent_terminal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool pid_text(const char *text, pid_t *pid) {
  if (!text || *text < '1' || *text > '9')
    return false;
  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 10);
  if (errno || !end || *end || value <= 1 || value > 4194304L)
    return false;
  *pid = (pid_t)value;
  return true;
}

static uint64_t fnv_text(uint64_t hash, const char *text) {
  while (*text) {
    hash ^= (unsigned char)*text++;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

// Distinct from a hook key, which hashes "agent:" plus the session id.
static uint64_t discover_key(const char *agent, pid_t pid) {
  char number[32];
  snprintf(number, sizeof(number), "%ld", (long)pid);
  uint64_t hash = fnv_text(14695981039346656037ULL, "proc:");
  hash = fnv_text(hash, agent);
  hash = fnv_text(hash, ":");
  hash = fnv_text(hash, number);
  return hash ? hash : 1;
}

static bool known_pid(pid_t pid) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].pid == pid)
      return true;
  return false;
}

static bool known_key(uint64_t key) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].key == key)
      return true;
  return false;
}

static const agent_adapter_t *match_comm(const char *comm) {
  for (size_t i = 0; i < agent_adapter_count(); i++) {
    const agent_adapter_t *adapter = agent_adapter_at(i);
    if (adapter->process_name && !strcmp(adapter->process_name, comm))
      return adapter;
  }
  return NULL;
}

static int open_pid(int root, pid_t pid) {
  char name[32];
  snprintf(name, sizeof(name), "%ld", (long)pid);
  return openat(root, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static bool read_comm(int root, pid_t pid, char *comm, size_t capacity) {
  int dir = open_pid(root, pid);
  if (dir < 0)
    return false;
  int fd = openat(dir, "comm", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  close(dir);
  if (fd < 0)
    return false;
  char buffer[32];
  ssize_t n = read(fd, buffer, sizeof(buffer));
  close(fd);
  if (n <= 0 || n >= (ssize_t)sizeof(buffer))
    return false;
  if (buffer[n - 1] == '\n')
    n--;
  if (n <= 0 || (size_t)n >= capacity)
    return false;
  memcpy(comm, buffer, (size_t)n);
  comm[n] = '\0';
  return true;
}

static bool read_parent(int root, pid_t pid, pid_t *parent) {
  int dir = open_pid(root, pid);
  if (dir < 0)
    return false;
  int fd = openat(dir, "stat", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  close(dir);
  if (fd < 0)
    return false;
  char buffer[1024], comm[64];
  ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (n <= 0)
    return false;
  buffer[n] = '\0';
  return agent_hook_parse_stat(buffer, comm, sizeof(comm), parent) == 0 &&
         *parent != pid;
}

static bool listed(pid_t pid, const focus_window_t *windows, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (windows[i].pid == pid)
      return true;
  return false;
}

static bool controlling_tty(int root, pid_t pid) {
  int dir = open_pid(root, pid);
  if (dir < 0)
    return false;
  int fd = openat(dir, "stat", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  close(dir);
  if (fd < 0)
    return false;
  char buffer[1024];
  ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (n <= 0)
    return false;
  buffer[n] = '\0';
  unsigned long tty = 0;
  return agent_hook_stat_tty(buffer, &tty) == 0 && tty != 0;
}

static bool in_window(int root, pid_t pid, const focus_window_t *windows,
                      size_t count) {
  for (int depth = 0; depth < 16 && pid > 1; depth++) {
    if (listed(pid, windows, count))
      return true;
    pid_t parent = 0;
    if (!read_parent(root, pid, &parent))
      return false;
    pid = parent;
  }
  return false;
}

static bool cwd_name(int root, pid_t pid, char *name, size_t capacity) {
  int dir = open_pid(root, pid);
  if (dir < 0)
    return false;
  char target[4096];
  ssize_t n = readlinkat(dir, "cwd", target, sizeof(target));
  close(dir);
  if (n <= 0 || n >= (ssize_t)sizeof(target))
    return false;
  target[n] = '\0';
  if (capacity < 41)
    return false;
  return agent_hook_place_name(target, name);
}

int agent_discover_scan(const char *proc_root, const focus_window_t *windows,
                        size_t window_count, int max_processes, int max_new,
                        int64_t now_ms, int done_timeout_s) {
  if (!proc_root || proc_root[0] != '/' || max_processes <= 0 || max_new <= 0 ||
      now_ms < 0 || done_timeout_s < 0 || (window_count && !windows))
    return 0;
  int root = open(proc_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root < 0)
    return 0;
  DIR *directory = fdopendir(root);
  if (!directory) {
    close(root);
    return 0;
  }
  int seen = 0, created = 0;
  while (seen < max_processes && created < max_new) {
    errno = 0;
    struct dirent *entry = readdir(directory);
    if (!entry)
      break;
    pid_t pid = 0;
    if (!pid_text(entry->d_name, &pid))
      continue;
    seen++;
    if (agent_sessions_count() >= AGENT_SESSIONS_MAX)
      break;
    if (known_pid(pid))
      continue;
    char comm[16];
    if (!read_comm(root, pid, comm, sizeof(comm)))
      continue;
    const agent_adapter_t *adapter = match_comm(comm);
    if (!adapter || !controlling_tty(root, pid) ||
        !in_window(root, pid, windows, window_count))
      continue;
    char name[48];
    uint64_t key;
    if (!cwd_name(root, pid, name, sizeof(name)))
      continue;
    key = discover_key(adapter->name, pid);
    if (known_key(key))
      continue;
    if (agent_sessions_apply(key, adapter->name, AGENT_EVENT_START, pid, now_ms,
                             done_timeout_s, NULL) < 0)
      continue;
    if (agent_sessions_set_name(key, name) < 0) {
      agent_sessions_apply(key, adapter->name, AGENT_EVENT_END, 0, now_ms,
                           done_timeout_s, NULL);
      continue;
    }
    agent_sessions_process(key, pid, true, proc_root);
    agent_sessions_set_provisional(key);
    uint64_t window = 0;
    char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
    pid_t kitty_pid = 0;
    if (agent_terminal_read(root, pid, &window, listen, sizeof(listen),
                            &kitty_pid))
      agent_sessions_set_kitty(key, kitty_pid, window, listen);
    created++;
  }
  closedir(directory);
  return created;
}

#define _GNU_SOURCE
#include "platform/agent_terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
  WINDOW_DIGITS_MAX = 20,
  ENVIRON_MAX = 256 * 1024
};

bool agent_terminal_listen_ok(const char *listen) {
  if (!listen)
    return false;
  size_t n = 0;
  while (listen[n]) {
    unsigned char c = (unsigned char)listen[n];
    if (c < 0x20 || c == 0x7f)
      return false;
    if (++n > AGENT_TERMINAL_LISTEN_MAX)
      return false;
  }
  return n >= 5 && !strncmp(listen, "unix:", 5);
}

static bool parse_window(const char *text, uint64_t *out) {
  if (!text || !text[0])
    return false;
  uint64_t value = 0;
  size_t n = 0;
  while (text[n]) {
    if (n >= WINDOW_DIGITS_MAX || text[n] < '0' || text[n] > '9')
      return false;
    uint64_t digit = (uint64_t)(text[n] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
    n++;
  }
  *out = value;
  return true;
}

static bool parse_kitty_pid(const char *text, pid_t *out) {
  if (!text || text[0] < '1' || text[0] > '9')
    return false;
  uint64_t value = 0;
  size_t n = 0;
  while (text[n]) {
    if (n >= 7 || text[n] < '0' || text[n] > '9')
      return false;
    value = value * 10 + (uint64_t)(text[n] - '0');
    n++;
  }
  if (!n || value <= 1 || value > 4194304)
    return false;
  *out = (pid_t)value;
  return true;
}

bool agent_terminal_accept(const char *window_text, const char *listen,
                           uint64_t *window, char *socket, size_t capacity) {
  uint64_t id = 0;
  if (!window || !socket || !parse_window(window_text, &id) ||
      !agent_terminal_listen_ok(listen))
    return false;
  size_t n = strlen(listen);
  if (n >= capacity)
    return false;
  memcpy(socket, listen, n + 1);
  *window = id;
  return true;
}

static bool entry_value(const char *entry, size_t length, const char *key,
                        const char **value) {
  size_t key_len = strlen(key);
  if (length <= key_len || entry[key_len] != '=' || memcmp(entry, key, key_len))
    return false;
  *value = entry + key_len + 1;
  return true;
}

bool agent_terminal_parse(const char *data, size_t length, uint64_t *window,
                          char *listen, size_t capacity, pid_t *kitty_pid) {
  if (!data || !window || !listen)
    return false;
  const char *id = NULL;
  const char *socket = NULL;
  const char *pid_text = NULL;
  size_t index = 0;
  while (index < length) {
    size_t start = index;
    while (index < length && data[index] != '\0')
      index++;
    if (index >= length)
      break;
    const char *value = NULL;
    if (entry_value(data + start, index - start, "KITTY_WINDOW_ID", &value))
      id = value;
    else if (entry_value(data + start, index - start, "KITTY_LISTEN_ON",
                         &value))
      socket = value;
    else if (entry_value(data + start, index - start, "KITTY_PID", &value))
      pid_text = value;
    index++;
  }
  if (!id || !socket ||
      !agent_terminal_accept(id, socket, window, listen, capacity))
    return false;
  if (kitty_pid) {
    pid_t parsed = 0;
    if (!pid_text || !parse_kitty_pid(pid_text, &parsed))
      parsed = 0;
    *kitty_pid = parsed;
  }
  return true;
}

bool agent_terminal_read(int proc_root, pid_t pid, uint64_t *window,
                         char *listen, size_t capacity, pid_t *kitty_pid) {
  if (proc_root < 0 || pid <= 0)
    return false;
  char path[32];
  int wrote = snprintf(path, sizeof(path), "%jd/environ", (intmax_t)pid);
  if (wrote < 0 || (size_t)wrote >= sizeof(path))
    return false;
  int fd = openat(proc_root, path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return false;
  char *buffer = malloc(ENVIRON_MAX);
  if (!buffer) {
    close(fd);
    return false;
  }
  size_t used = 0;
  while (used < ENVIRON_MAX) {
    ssize_t count = read(fd, buffer + used, ENVIRON_MAX - used);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      explicit_bzero(buffer, used);
      free(buffer);
      close(fd);
      return false;
    }
    if (!count)
      break;
    used += (size_t)count;
  }
  close(fd);
  bool ok =
      agent_terminal_parse(buffer, used, window, listen, capacity, kitty_pid);
  explicit_bzero(buffer, ENVIRON_MAX);
  free(buffer);
  return ok;
}

bool agent_terminal_lookup(const char *proc_root, pid_t pid, uint64_t *window,
                           char *listen, size_t capacity, pid_t *kitty_pid) {
  if (!proc_root || proc_root[0] != '/')
    return false;
  int root = open(proc_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (root < 0)
    return false;
  bool ok = agent_terminal_read(root, pid, window, listen, capacity, kitty_pid);
  close(root);
  return ok;
}

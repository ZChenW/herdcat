#define _GNU_SOURCE
#include "platform/agent_terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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

bool agent_terminal_number(const char *text, bool tmux, uint64_t *pane) {
  if (!text || !pane)
    return false;
  if (tmux) {
    if (*text != '%')
      return false;
    text++;
  }
  return parse_window(text, pane);
}

bool agent_terminal_socket_ok(const char *path) {
  if (!path || path[0] != '/' || strlen(path) > AGENT_TERMINAL_LISTEN_MAX)
    return false;
  int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    return false;
  const char *part = path + 1;
  bool ok = false;
  while (*part) {
    const char *slash = strchr(part, '/');
    size_t n = slash ? (size_t)(slash - part) : strlen(part);
    if (!n || (n == 1 && part[0] == '.') || (n == 2 && !memcmp(part, "..", 2)))
      break;
    char component[AGENT_TERMINAL_LISTEN_MAX + 1];
    memcpy(component, part, n);
    component[n] = 0;
    for (size_t i = 0; i < n; i++)
      if ((unsigned char)component[i] < 32 || component[i] == 127)
        goto finished;
    // O_PATH accepts sockets without connecting to them.
    int next = openat(fd, component, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0)
      break;
    struct stat st;
    bool valid = fstat(next, &st) == 0 && !S_ISLNK(st.st_mode) &&
                 (slash ? S_ISDIR(st.st_mode) : st.st_uid == getuid());
    close(fd);
    fd = next;
    if (!valid)
      break;
    if (!slash) {
      ok = true;
      break;
    }
    part = slash + 1;
  }
finished:
  close(fd);
  return ok;
}

bool agent_terminal_parse_all(const char *data, size_t length,
                              agent_terminal_t *terminal) {
  if (!data || !terminal)
    return false;
  const char *tmux = NULL, *tmux_pane = NULL, *wez = NULL, *wez_socket = NULL;
  const char *program = NULL;
  for (size_t at = 0; at < length;) {
    const char *entry = data + at;
    const char *end = memchr(entry, 0, length - at);
    if (!end)
      break;
    size_t n = (size_t)(end - entry);
    const char *v;
    if (entry_value(entry, n, "TMUX", &v))
      tmux = v;
    else if (entry_value(entry, n, "TMUX_PANE", &v))
      tmux_pane = v;
    else if (entry_value(entry, n, "WEZTERM_PANE", &v))
      wez = v;
    else if (entry_value(entry, n, "WEZTERM_UNIX_SOCKET", &v))
      wez_socket = v;
    else if (entry_value(entry, n, "TERM_PROGRAM", &v))
      program = v;
    at += n + 1;
  }
  agent_terminal_t t = {.kind = TERMINAL_NONE};
  if (tmux && tmux_pane) {
    // TMUX ends with ,server-pid,index; a socket may itself contain commas.
    const char *last = strrchr(tmux, ',');
    const char *previous = NULL;
    if (last)
      for (const char *c = tmux; c < last; c++)
        if (*c == ',')
          previous = c;
    if (!previous || (size_t)(previous - tmux) > AGENT_TERMINAL_LISTEN_MAX ||
        !agent_terminal_number(tmux_pane, true, &t.pane))
      return false;
    uint64_t server, index;
    char pid_text[32];
    size_t n = (size_t)(last - previous - 1);
    if (!n || n >= sizeof(pid_text))
      return false;
    memcpy(pid_text, previous + 1, n);
    pid_text[n] = 0;
    if (!parse_window(pid_text, &server) || server <= 1 || server > 4194304 ||
        !parse_window(last + 1, &index))
      return false;
    memcpy(t.socket, tmux, (size_t)(previous - tmux));
    t.kind = TERMINAL_TMUX;
  } else if (wez && wez_socket) {
    if (!agent_terminal_number(wez, false, &t.pane) ||
        strlen(wez_socket) > AGENT_TERMINAL_LISTEN_MAX)
      return false;
    memcpy(t.socket, wez_socket, strlen(wez_socket) + 1);
    t.kind = TERMINAL_WEZTERM;
  } else if (program && !strcmp(program, "ghostty")) {
    t.kind = TERMINAL_GHOSTTY;
  } else {
    return false;
  }
  if (t.kind != TERMINAL_GHOSTTY && !agent_terminal_socket_ok(t.socket))
    return false;
  *terminal = t;
  return true;
}

bool agent_terminal_environment(agent_terminal_t *terminal) {
  const char *keys[] = {"TMUX", "TMUX_PANE", "WEZTERM_PANE",
                        "WEZTERM_UNIX_SOCKET", "TERM_PROGRAM"};
  char data[1024];
  size_t used = 0;
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    const char *v = getenv(keys[i]);
    if (!v)
      continue;
    int n = snprintf(data + used, sizeof(data) - used, "%s=%s", keys[i], v);
    if (n < 0 || (size_t)n >= sizeof(data) - used)
      return false;
    used += (size_t)n + 1;
  }
  bool ok = agent_terminal_parse_all(data, used, terminal);
  explicit_bzero(data, sizeof(data));
  return ok;
}

bool agent_terminal_lookup_all(const char *root, pid_t pid,
                               agent_terminal_t *terminal) {
  if (!root || *root != '/' || pid <= 1)
    return false;
  char path[256];
  int n = snprintf(path, sizeof(path), "%s/%jd/environ", root, (intmax_t)pid);
  if (n < 0 || (size_t)n >= sizeof(path))
    return false;
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return false;
  char *data = malloc(ENVIRON_MAX);
  if (!data) {
    close(fd);
    return false;
  }
  size_t used = 0;
  bool ok = false;
  while (used < ENVIRON_MAX) {
    ssize_t got = read(fd, data + used, ENVIRON_MAX - used);
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0)
      break;
    if (!got) {
      ok = agent_terminal_parse_all(data, used, terminal);
      break;
    }
    used += (size_t)got;
  }
  explicit_bzero(data, ENVIRON_MAX);
  free(data);
  close(fd);
  return ok;
}

static const char *const TERMINAL_NAMES[] = {"kitty", "tmux", "wezterm",
                                             "ghostty"};
bool agent_terminal_message(char *out, size_t capacity, uint64_t key,
                            const agent_terminal_t *t) {
  if (!out || !t || t->kind < TERMINAL_TMUX || t->kind > TERMINAL_GHOSTTY)
    return false;
  char encoded[AGENT_TERMINAL_LISTEN_MAX * 2 + 1];
  size_t n = strnlen(t->socket, sizeof(t->socket));
  if (n > AGENT_TERMINAL_LISTEN_MAX)
    return false;
  for (size_t i = 0; i < n; i++)
    snprintf(encoded + i * 2, 3, "%02x", (unsigned char)t->socket[i]);
  if (!n)
    strcpy(encoded, "-");
  int wrote = snprintf(out, capacity, "term %016jx %s %ju %s", (uintmax_t)key,
                       TERMINAL_NAMES[t->kind], (uintmax_t)t->pane, encoded);
  return wrote > 0 && (size_t)wrote < capacity;
}
bool agent_terminal_request(const char *request, uint64_t *key,
                            agent_terminal_t *terminal) {
  char key_text[17], kind[9], pane[21], encoded[255];
  int end = 0;
  if (!request || !key || !terminal ||
      sscanf(request, "term %16[0-9a-f] %8[a-z] %20[0-9] %254[0-9a-f-]%n",
             key_text, kind, pane, encoded, &end) != 4 ||
      request[end] || strlen(key_text) != 16)
    return false;
  agent_terminal_t t = {.kind = TERMINAL_NONE};
  for (int i = TERMINAL_TMUX; i <= TERMINAL_GHOSTTY; i++)
    if (!strcmp(kind, TERMINAL_NAMES[i]))
      t.kind = (agent_terminal_kind_t)i;
  if (t.kind == TERMINAL_NONE || !parse_window(pane, &t.pane))
    return false;
  if (t.kind == TERMINAL_GHOSTTY) {
    if (strcmp(encoded, "-") || t.pane)
      return false;
  } else {
    size_t n = strlen(encoded);
    if (!n || n % 2)
      return false;
    for (size_t i = 0; i < n / 2; i++) {
      char pair[3] = {encoded[i * 2], encoded[i * 2 + 1], 0};
      char *tail;
      unsigned long byte = strtoul(pair, &tail, 16);
      if (*tail || !byte || byte > 255 || pair[0] == '-' || pair[1] == '-')
        return false;
      t.socket[i] = (char)byte;
    }
    if (!agent_terminal_socket_ok(t.socket))
      return false;
  }
  uint64_t parsed = strtoull(key_text, NULL, 16);
  if (!parsed)
    return false;
  *key = parsed;
  *terminal = t;
  return true;
}

bool agent_terminal_pane_message(char *out, size_t capacity, pid_t pid,
                                 uint64_t pane, const agent_terminal_t *t) {
  char metadata[384];
  if (!out || !t || t->kind != TERMINAL_TMUX || pid <= 1 || pid > 4194304 ||
      !agent_terminal_message(metadata, sizeof(metadata), 1, t))
    return false;
  const char *encoded = strrchr(metadata, ' ') + 1;
  int n = snprintf(out, capacity, "pane %jd %ju tmux %s", (intmax_t)pid,
                   (uintmax_t)pane, encoded);
  return n > 0 && (size_t)n < capacity;
}
bool agent_terminal_pane_request(const char *request, agent_terminal_t *t) {
  char pid_text[8], pane[21], encoded[255];
  int end = 0;
  pid_t pid;
  if (!request || !t ||
      sscanf(request, "pane %7[0-9] %20[0-9] tmux %254[0-9a-f]%n", pid_text,
             pane, encoded, &end) != 3 ||
      request[end] || !parse_kitty_pid(pid_text, &pid))
    return false;
  // Reuse the metadata validator, including path ownership and traversal.
  char metadata[384];
  int n = snprintf(metadata, sizeof(metadata),
                   "term 0000000000000001 tmux %s %s", pane, encoded);
  uint64_t key;
  if (n < 0 || (size_t)n >= sizeof(metadata) ||
      !agent_terminal_request(metadata, &key, t))
    return false;
  t->client_pid = pid;
  return true;
}

bool agent_terminal_tmux_message(char *out, size_t capacity) {
  const char *value = getenv("TMUX");
  if (!value || strlen(value) > 256)
    return false;
  char data[320], metadata[384];
  int n = snprintf(data, sizeof(data), "TMUX=%s%cTMUX_PANE=%%0%c", value, 0, 0);
  agent_terminal_t terminal;
  if (n < 0 || (size_t)n >= sizeof(data) ||
      !agent_terminal_parse_all(data, (size_t)n, &terminal) ||
      terminal.kind != TERMINAL_TMUX ||
      !agent_terminal_message(metadata, sizeof(metadata), 1, &terminal))
    return false;
  n = snprintf(out, capacity, "tmux %s", strrchr(metadata, ' ') + 1);
  return n > 0 && (size_t)n < capacity;
}
bool agent_terminal_tmux_request(const char *request, agent_terminal_t *t) {
  char encoded[255], metadata[384];
  int end = 0;
  if (!request || !t ||
      sscanf(request, "tmux %254[0-9a-f]%n", encoded, &end) != 1 ||
      request[end])
    return false;
  int n = snprintf(metadata, sizeof(metadata),
                   "term 0000000000000001 tmux 0 %s", encoded);
  uint64_t key;
  return n > 0 && (size_t)n < sizeof(metadata) &&
         agent_terminal_request(metadata, &key, t);
}

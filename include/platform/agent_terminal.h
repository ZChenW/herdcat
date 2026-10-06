#ifndef HERDCAT_AGENT_TERMINAL_H
#define HERDCAT_AGENT_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AGENT_TERMINAL_LISTEN_MAX 127
#define AGENT_TERMINAL_TITLE_MAX  96

typedef enum {
  TERMINAL_KITTY = 0,
  TERMINAL_TMUX,
  TERMINAL_WEZTERM,
  TERMINAL_GHOSTTY,
  TERMINAL_NONE
} agent_terminal_kind_t;

typedef struct {
  agent_terminal_kind_t kind;
  uint64_t pane;
  char socket[AGENT_TERMINAL_LISTEN_MAX + 1];
  // Resolved client and niri window; never persisted.
  pid_t client_pid;
  pid_t outer_kitty_pid;
  uint64_t outer_kitty_pane;
  uint64_t window;
  uint64_t native_window;
  bool native_window_known;
  bool current_known;
  uint64_t current_pane;
  char title[AGENT_TERMINAL_TITLE_MAX + 1];
} agent_terminal_t;

bool agent_terminal_number(const char *text, bool tmux, uint64_t *pane);
bool agent_terminal_socket_ok(const char *path);
bool agent_terminal_environment(agent_terminal_t *terminal);
bool agent_terminal_parse_all(const char *data, size_t length,
                              agent_terminal_t *terminal);
bool agent_terminal_lookup_all(const char *root, pid_t pid,
                               agent_terminal_t *terminal);
// Socket bytes are hex encoded so spaces remain a single control token.
bool agent_terminal_message(char *out, size_t capacity, uint64_t key,
                            const agent_terminal_t *terminal);
bool agent_terminal_request(const char *request, uint64_t *key,
                            agent_terminal_t *terminal);

bool agent_terminal_pane_message(char *out, size_t capacity, pid_t pid,
                                 uint64_t pane,
                                 const agent_terminal_t *terminal);
bool agent_terminal_pane_request(const char *request,
                                 agent_terminal_t *terminal);

// A unix socket value with no control characters, at most 127 bytes.
bool agent_terminal_listen_ok(const char *listen);
// False rejects the pair. Overlong values are rejected, not truncated.
bool agent_terminal_accept(const char *window_text, const char *listen,
                           uint64_t *window, char *socket, size_t capacity);
// NUL-separated environment bytes. An entry with no terminator is ignored.
// On success, a non-null kitty_pid is the kitty process, or 0 when
// KITTY_PID is missing or not a decimal pid from 2 to 4194304. Failure
// leaves it unchanged. The last duplicate key wins.
bool agent_terminal_parse(const char *data, size_t length, uint64_t *window,
                          char *listen, size_t capacity, pid_t *kitty_pid);
// proc_root is a directory fd for a /proc-like tree. Other variables are
// wiped before the buffer is freed. kitty_pid follows parse().
bool agent_terminal_read(int proc_root, pid_t pid, uint64_t *window,
                         char *listen, size_t capacity, pid_t *kitty_pid);
bool agent_terminal_lookup(const char *proc_root, pid_t pid, uint64_t *window,
                           char *listen, size_t capacity, pid_t *kitty_pid);

#endif

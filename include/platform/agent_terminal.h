#ifndef HERDCAT_AGENT_TERMINAL_H
#define HERDCAT_AGENT_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AGENT_TERMINAL_LISTEN_MAX 127

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

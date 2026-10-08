#ifndef HERDCAT_FOCUS_H
#define HERDCAT_FOCUS_H
#include "platform/agent_terminal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
  uint64_t id;
  pid_t pid;
  // Nonzero while the title starts with the mark an agent shows at rest.
  // The parser stores 1; focus_watch replaces it with when that began.
  int64_t resting_since_ms;
  char title[AGENT_TERMINAL_TITLE_MAX + 1];
} focus_window_t;
typedef enum {
  FOCUS_PENDING,
  FOCUS_SUCCESS,
  FOCUS_NOT_FOUND,
  FOCUS_UNAVAILABLE
} focus_result_t;
// Strict bounded JSON extraction; -1 on malformed or oversized lists.
int focus_parse_windows(const char *json, size_t length, focus_window_t *out,
                        size_t capacity);
bool focus_find_window(pid_t pid, const focus_window_t *windows, size_t count,
                       uint64_t *id);
// Same nearest ancestor, with title ranking among windows of that process.
bool focus_find_window_title(pid_t pid, const focus_window_t *windows,
                             size_t count, const char *title, bool contains,
                             uint64_t *id);
// Selects only among candidates with the supplied pid. Ghostty requires one
// unique substring match; ordinary titles rank exact before prefix/suffix.
bool focus_pick_window(pid_t pid, const focus_window_t *windows, size_t count,
                       const char *title, bool contains, uint64_t *id);
bool focus_terminal_window(pid_t pid, const agent_terminal_t *terminal,
                           const char *name, const focus_window_t *windows,
                           size_t count, uint64_t *id);
typedef struct {
  uint64_t pane;
  uint64_t window;
  bool has_window;
  char title[AGENT_TERMINAL_TITLE_MAX + 1];
} focus_wezterm_pane_t;
int focus_parse_wezterm(const char *json, size_t length, bool clients,
                        focus_wezterm_pane_t *out, size_t capacity);
typedef bool (*focus_terminal_fn_t)(pid_t pid, agent_terminal_t *terminal,
                                    char *name, size_t capacity);
typedef bool (*focus_title_fn_t)(pid_t pid, char *title, size_t capacity);
void focus_set_title(focus_title_fn_t lookup);
bool focus_terminal_window_title(pid_t pid, const agent_terminal_t *terminal,
                                 const char *name, const char *title,
                                 const focus_window_t *windows, size_t count,
                                 uint64_t *id);
typedef void (*focus_terminal_note_fn_t)(pid_t pid,
                                         const agent_terminal_t *terminal);
void focus_set_terminal(focus_terminal_fn_t lookup,
                        focus_terminal_note_fn_t note);
typedef void (*focus_current_fn_t)(pid_t pid, uint64_t window,
                                   const char *socket,
                                   const focus_wezterm_pane_t *panes,
                                   size_t count);
void focus_set_current(focus_current_fn_t note);
bool focus_tmux_client(const char *text, const char *session, pid_t *pid,
                       char *tty, size_t capacity);
// Background discovery shares the one command job and adds no periodic wake.
void focus_terminal_resolve(pid_t pid);
void focus_wezterm_current(pid_t pid, uint64_t window);
#ifdef TEST_BUILD
void focus_test_available(bool available);
void focus_reset_stat_reads(void);
unsigned focus_stat_reads(void);
#endif
bool focus_available(void);
int focus_session_window(pid_t agent_pid);
// After niri focuses the window. True provides a split id and socket.
typedef bool (*focus_kitty_fn_t)(pid_t pid, uint64_t *window, char *listen,
                                 size_t capacity);
void focus_set_kitty(focus_kitty_fn_t fn);
// False means kitten must not be started. match receives id:<decimal>.
bool focus_kitty_target(const char *window_text, const char *listen,
                        char *match, size_t capacity);
void focus_poll(void);
int focus_poll_fd(void);
int focus_timeout(void);
focus_result_t focus_take_result(void);
void focus_cleanup(void);
#endif

#ifndef HERDCAT_FOCUS_H
#define HERDCAT_FOCUS_H
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
bool focus_available(void);
int focus_session_window(pid_t agent_pid);
// After niri focuses the window. True provides a split id and socket.
typedef bool (*focus_kitty_fn)(pid_t pid, uint64_t *window, char *listen,
                               size_t capacity);
void focus_set_kitty(focus_kitty_fn fn);
// False means kitten must not be started. match receives id:<decimal>.
bool focus_kitty_target(const char *window_text, const char *listen,
                        char *match, size_t capacity);
void focus_poll(void);
int focus_poll_fd(void);
int focus_timeout(void);
focus_result_t focus_take_result(void);
void focus_cleanup(void);
#endif

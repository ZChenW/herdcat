#ifndef FOCUS_WATCH_H
#define FOCUS_WATCH_H

#include "core/agent_sessions.h"
#include "platform/focus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
  FOCUS_WATCH_IGNORED = 0,
  FOCUS_WATCH_FOCUS,
  FOCUS_WATCH_UPSERT,
  FOCUS_WATCH_CLOSE,
  FOCUS_WATCH_WINDOWS
} focus_watch_kind_t;

typedef struct {
  focus_watch_kind_t kind;
  bool id_null;
  uint64_t id;
  pid_t pid;
  int count;
  // Set from a window's is_focused in window lists and updates.
  bool has_focused;
  uint64_t focused;
} focus_watch_event_t;

// 0 ignored, 1 parsed, -1 malformed or longer than 65536 bytes.
int focus_watch_parse(const char *line, size_t length,
                      focus_watch_event_t *event, focus_window_t *windows,
                      size_t capacity);

// Subscribes on the agent_watch epoll. Missing niri is silent.
int focus_watch_init(void);
void focus_watch_cleanup(void);
void focus_watch_poll(void);
// -1 when connected or permanently off. Otherwise milliseconds until retry.
int focus_watch_timeout(void);
bool focus_watch_available(void);
uint64_t focus_watch_focused_id(void);
// Key of the focused session, or 0. The newest session in that window wins.
uint64_t focus_watch_focused_session(const agent_session_view_t *sessions,
                                     size_t count);
// Newest session whose process owns the focused window, or 0.
uint64_t focus_watch_match(uint64_t focused, const focus_window_t *windows,
                           size_t windows_count,
                           const agent_session_view_t *sessions, size_t count);
// Every session whose process owns that window. Returns how many were written.
int focus_watch_matching(uint64_t focused, const focus_window_t *windows,
                         size_t windows_count,
                         const agent_session_view_t *sessions, size_t count,
                         uint64_t *keys, size_t capacity);
// Sessions on the focused window. Empty when the stream is down.
int focus_watch_focused_keys(const agent_session_view_t *sessions, size_t count,
                             uint64_t *keys, size_t capacity);

#endif

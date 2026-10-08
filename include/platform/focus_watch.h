#ifndef FOCUS_WATCH_H
#define FOCUS_WATCH_H

#include "core/agent_sessions.h"
#include "platform/focus.h"
#include "platform/focus_current.h"

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
  // The title of an upserted window starts with the at-rest mark.
  bool resting;
  char title[AGENT_TERMINAL_TITLE_MAX + 1];
} focus_watch_event_t;

// 0 ignored, 1 parsed, -1 malformed or longer than 65536 bytes.
int focus_watch_parse(const char *line, size_t length,
                      focus_watch_event_t *event, focus_window_t *out,
                      size_t capacity);

// Subscribes on the agent_watch epoll. Missing niri is silent.
int focus_watch_init(void);
void focus_watch_cleanup(void);
void focus_watch_poll(void);
void focus_watch_ready(uint32_t token);
// -1 when connected or permanently off. Otherwise milliseconds until retry.
int focus_watch_timeout(void);
bool focus_watch_available(void);
#define FOCUS_WATCH_WINDOW_MAX 128

// Copies the current niri window list. Returns how many were written.
size_t focus_watch_windows(focus_window_t *out, size_t capacity);
uint64_t focus_watch_focused_id(void);
// Current session, or 0. Reported split, then the last click in this
// window, then the newest session. 0 while the stream is down.
uint64_t focus_watch_focused_session(const agent_session_view_t *sessions,
                                     size_t count);
// Newest session whose process owns the focused window, or 0.
uint64_t focus_watch_match(uint64_t focused, const focus_window_t *wins,
                           size_t windows_count,
                           const agent_session_view_t *sessions, size_t count);
// Every session whose process owns that window. Returns how many were written.
int focus_watch_matching(uint64_t focused, const focus_window_t *wins,
                         size_t windows_count,
                         const agent_session_view_t *sessions, size_t count,
                         uint64_t *keys, size_t capacity);
// How long a title must stay at rest, with no hook event, before the turn
// counts as cancelled.
#define FOCUS_REST_GRACE_MS 2000
// Working sessions whose terminal title has gone back to the at-rest mark:
// the agent stopped without telling anyone, as Claude Code does when Esc is
// pressed before it starts to answer. Only agents whose adapter sets
// rest_title, and only when the title can belong to no other session.
// *next_ms is the delay until a pending case matures, or -1.
int focus_watch_rested(const focus_window_t *wins, size_t windows_count,
                       const agent_session_view_t *sessions, size_t count,
                       const focus_pane_t *panes, size_t pane_count,
                       int64_t now_ms, uint64_t *keys, size_t capacity,
                       int *next_ms);
// The same over the live window list. Nothing while the stream is down.
int focus_watch_rested_now(const agent_session_view_t *sessions, size_t count,
                           int64_t now_ms, uint64_t *keys, size_t capacity,
                           int *next_ms);
// Sessions treated as seen. A reported split narrows this to that split.
// With no report, every session in the focused window is included.
// Empty when the stream is down.
int focus_watch_focused_keys(const agent_session_view_t *sessions, size_t count,
                             uint64_t *keys, size_t capacity);

// Seen sessions and the selected board from a single ancestry pass over
// at most AGENT_SESSIONS_MAX sessions. *chosen is 0 while the stream is down.
int focus_watch_query(const agent_session_view_t *sessions, size_t count,
                      uint64_t *keys, size_t capacity, uint64_t *chosen);

#endif

#ifndef AGENT_SESSIONS_H
#define AGENT_SESSIONS_H

#include "core/agent_state.h"
#include "core/agent_transcript.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define AGENT_SESSIONS_MAX 32
#define AGENT_NAME_MAX     8

typedef enum {
  AGENT_EVENT_IDLE = 0,
  AGENT_EVENT_WORKING,
  AGENT_EVENT_WAITING,
  AGENT_EVENT_DONE,
  AGENT_EVENT_START,
  AGENT_EVENT_REST,
  AGENT_EVENT_END,
  AGENT_EVENT_INTERRUPT,
  // working/waiting -> error. Never creates a session.
  AGENT_EVENT_FAIL,
  AGENT_EVENT_COUNT
} agent_event_t;

typedef struct {
  uint64_t key, order;
  char agent[AGENT_NAME_MAX + 1];
  char name[48];
  agent_state_t state;
  pid_t pid;
  int64_t created_ms, state_since_ms, updated_ms;
  // Set while a done or error sign is waiting for someone to look at it.
  bool unread;
  // Kitty process and split. Both stay 0 until a socket is stored.
  pid_t kitty_pid;
  uint64_t kitty_window;
} agent_session_view_t;

int agent_sessions_set_name(uint64_t key, const char *name);
int agent_sessions_snapshot(agent_session_view_t *out, size_t capacity);
// Input is a creation-ordered snapshot. Choose active then recent, retain
// order.
int agent_sessions_select(const agent_session_view_t *input, size_t count,
                          agent_session_view_t *out, size_t capacity);
int agent_event_parse(const char *name, agent_event_t *out);
void agent_sessions_reset(void);
void agent_sessions_interrupt(uint64_t key, int64_t now_ms);
void agent_sessions_fail(uint64_t key, int64_t now_ms);
int agent_sessions_apply(uint64_t key, const char *agent, agent_event_t event,
                         pid_t pid, int64_t now_ms, int done_timeout_s,
                         bool *is_new);
// Default is sticky: a done sign the focused window did not see stays up.
// False arms done_timeout_s on every completion.
void agent_sessions_set_done_sticky(bool sticky);
// Reload policy, acknowledging existing unread completions (including manual)
// when switching to timeout. Already-running timers are not restarted.
void agent_sessions_configure_done(bool sticky, int64_t now_ms,
                                   int done_timeout_s);
// available is false when no focus stream exists. keys own the focused window.
void agent_sessions_observe_focus(bool available, const uint64_t *keys,
                                  size_t count);
// Start the done timeout once this unread session has been seen.
void agent_sessions_note_focused(uint64_t key, int64_t now_ms,
                                 int done_timeout_s);
// A click uses the timeout from the latest successful apply.
void agent_sessions_note_click(uint64_t key, int64_t now_ms);
bool agent_sessions_expire(int64_t now_ms, int stale_timeout_s);
void agent_sessions_remove_pid(pid_t pid);
void agent_sessions_set_watched(uint64_t key, bool watched);
// Kept until the agent pid changes. Not saved, and generation stays the
// same. kitty_pid is 0 when KITTY_PID was missing or invalid.
void agent_sessions_set_kitty(uint64_t key, pid_t kitty_pid, uint64_t window,
                              const char *listen);
bool agent_sessions_kitty(pid_t pid, uint64_t *window, char *listen,
                          size_t capacity);
agent_state_t agent_sessions_resolve(void);
int64_t agent_sessions_next_deadline(int stale_timeout_s);
int agent_sessions_count(void);
// Return bytes written, excluding NUL. Truncation always terminates the buffer.
int agent_sessions_format(char *buffer, size_t capacity, int64_t now_ms);
// Read-only process snapshots let the caller reconcile shared process watches.
pid_t agent_sessions_pid(uint64_t key);
int agent_sessions_pids(pid_t *pids, size_t capacity);

typedef struct {
  uint64_t key;
  char agent[AGENT_NAME_MAX + 1];
  char name[48];
  char transcript[AGENT_TRANSCRIPT_PATH_MAX + 1];
  agent_state_t state;
  pid_t pid;
  int64_t updated_ms;
  bool unread;
} agent_session_record_t;

// Bumps when a persisted field changes. Watch flags do not count.
uint64_t agent_sessions_generation(void);
int agent_sessions_set_transcript(uint64_t key, const char *path);
// Creation order. Returns how many were written.
int agent_sessions_export(agent_session_record_t *out, size_t capacity);
// working/waiting are stored as idle. A dead pid is the caller's decision.
int agent_sessions_restore(const agent_session_record_t *record, int64_t now_ms,
                           int done_timeout_s);

#endif  // AGENT_SESSIONS_H

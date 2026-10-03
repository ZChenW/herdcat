#ifndef AGENT_SESSIONS_H
#define AGENT_SESSIONS_H

#include "core/agent_state.h"

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
  AGENT_EVENT_COUNT
} agent_event_t;

int agent_event_parse(const char *name, agent_event_t *out);
void agent_sessions_reset(void);
int agent_sessions_apply(uint64_t key, const char *agent, agent_event_t event,
                         pid_t pid, int64_t now_ms, int done_timeout_s,
                         bool *is_new);
bool agent_sessions_expire(int64_t now_ms, int stale_timeout_s);
void agent_sessions_remove_pid(pid_t pid);
void agent_sessions_set_watched(uint64_t key, bool watched);
agent_state_t agent_sessions_resolve(void);
int64_t agent_sessions_next_deadline(int stale_timeout_s);
int agent_sessions_count(void);
// Return bytes written, excluding NUL. Truncation always terminates the buffer.
int agent_sessions_format(char *buffer, size_t capacity, int64_t now_ms);
// Read-only process snapshots let the caller reconcile shared process watches.
pid_t agent_sessions_pid(uint64_t key);
int agent_sessions_pids(pid_t *pids, size_t capacity);

#endif  // AGENT_SESSIONS_H

#ifndef HERDCAT_AGENT_SIGN_STATE_H
#define HERDCAT_AGENT_SIGN_STATE_H

#include "core/agent_sessions.h"

// Display-only: callers must retain the real state for focus and alerts.
static inline bool agent_sign_waits_on_children(const agent_session_view_t *s) {
  return s->child_count && (s->state == AGENT_STATE_IDLE ||
                            (s->state == AGENT_STATE_DONE && !s->unread));
}

static inline agent_state_t agent_sign_state(const agent_session_view_t *s) {
  return agent_sign_waits_on_children(s) ? AGENT_STATE_WORKING : s->state;
}

static inline int64_t agent_sign_since(const agent_session_view_t *s) {
  return agent_sign_waits_on_children(s) ? s->child_started_ms
                                         : s->state_since_ms;
}

#endif

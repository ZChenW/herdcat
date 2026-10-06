#ifndef HERDCAT_AGENT_SESSIONS_INTERNAL_H
#define HERDCAT_AGENT_SESSIONS_INTERNAL_H

#include "core/agent_sessions.h"
#include "core/agent_title.h"
#include "utils/utf8.h"

#include <inttypes.h>
#include <string.h>

typedef struct {
  bool used;
  bool watched;
  uint64_t key, order;
  uint64_t parent_order, parent_key;
  pid_t candidate_pid;
  bool ancestry_checked;
  char name[48];
  char title[AGENT_TITLE_MAX + 1];
  bool title_temporary;
  char session_id[AGENT_SESSION_ID_MAX + 1];
  int64_t created_ms, state_since_ms;
  char agent[AGENT_NAME_MAX + 1];
  agent_state_t state;
  pid_t pid;
  agent_terminal_t terminals[2];
  int64_t updated_ms;
  int64_t done_until_ms;
  bool unread;
  char transcript[AGENT_TRANSCRIPT_PATH_MAX + 1];
  // The latest other session id the same process reported with. See adopt().
  uint64_t alias;
  // Found by scanning processes; the first real id takes the row over.
  bool provisional;
  // Shown as working on the strength of a key press. Goes back to waiting
  // at this time unless a hook event confirms the answer first.
  int64_t answered_until_ms;
} agent_session_t;

extern uint64_t next_order;
extern agent_session_t sessions[AGENT_SESSIONS_MAX];
extern int applied_done_timeout;
void touch_sessions(void);
agent_session_t *find_session(uint64_t key);
agent_session_t *find_alias(uint64_t key);
agent_session_t *available_slot(void);
void remove_session(agent_session_t *s);
void drop_same_process(const agent_session_t *keep);

static inline bool finished(agent_state_t state) {
  return state == AGENT_STATE_DONE || state == AGENT_STATE_ERROR;
}

static inline int64_t deadline(int64_t now_ms, int timeout_s) {
  int64_t duration = (int64_t)timeout_s * 1000;
  return now_ms > INT64_MAX - duration ? INT64_MAX : now_ms + duration;
}

static inline bool valid_agent(const char *agent) {
  if (!agent || !*agent) {
    return false;
  }
  size_t i = 0;
  while (agent[i]) {
    if (i >= AGENT_NAME_MAX || agent[i] < 'a' || agent[i] > 'z') {
      return false;
    }
    i++;
  }
  return true;
}

#endif

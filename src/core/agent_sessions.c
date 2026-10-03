#include "core/agent_sessions.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  bool used;
  bool watched;
  uint64_t key;
  char agent[AGENT_NAME_MAX + 1];
  agent_state_t state;
  pid_t pid;
  int64_t updated_ms;
  int64_t done_until_ms;
} agent_session_t;

static agent_session_t sessions[AGENT_SESSIONS_MAX];
static const char *const EVENT_NAMES[AGENT_EVENT_COUNT] = {
    "idle", "working", "waiting", "done", "start", "rest", "end"};

int agent_event_parse(const char *name, agent_event_t *out) {
  if (!name || !out) {
    return -1;
  }
  for (int i = 0; i < AGENT_EVENT_COUNT; i++) {
    if (strcmp(name, EVENT_NAMES[i]) == 0) {
      *out = (agent_event_t)i;
      return 0;
    }
  }
  return -1;
}

void agent_sessions_reset(void) {
  memset(sessions, 0, sizeof(sessions));
}

static agent_session_t *find_session(uint64_t key) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].key == key) {
      return &sessions[i];
    }
  }
  return NULL;
}

static agent_session_t *available_slot(void) {
  agent_session_t *oldest = NULL;
  agent_session_t *idle = NULL;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *s = &sessions[i];
    if (!s->used) {
      return s;
    }
    if (!oldest || s->updated_ms < oldest->updated_ms) {
      oldest = s;
    }
    if (s->state == AGENT_STATE_IDLE &&
        (!idle || s->updated_ms < idle->updated_ms)) {
      idle = s;
    }
  }
  return idle ? idle : oldest;
}

static int64_t deadline(int64_t now_ms, int timeout_s) {
  int64_t duration = (int64_t)timeout_s * 1000;
  return now_ms > INT64_MAX - duration ? INT64_MAX : now_ms + duration;
}

static bool valid_agent(const char *agent) {
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

int agent_sessions_apply(uint64_t key, const char *agent, agent_event_t event,
                         pid_t pid, int64_t now_ms, int done_timeout_s,
                         bool *is_new) {
  if (is_new) {
    *is_new = false;
  }
  if (!valid_agent(agent) || event < AGENT_EVENT_IDLE ||
      event >= AGENT_EVENT_COUNT || now_ms < 0 || done_timeout_s < 0) {
    return -1;
  }
  agent_session_t *s = find_session(key);
  if (event == AGENT_EVENT_END) {
    if (s) {
      memset(s, 0, sizeof(*s));
    }
    return 0;
  }
  if (!s) {
    if (event == AGENT_EVENT_IDLE || event == AGENT_EVENT_REST) {
      return 0;
    }
    s = available_slot();
    *s = (agent_session_t){.used = true, .key = key};
    memcpy(s->agent, agent, strlen(agent) + 1);
    if (is_new) {
      *is_new = true;
    }
  }
  s->updated_ms = now_ms;
  if (pid > 0 && pid != s->pid) {
    s->pid = pid;
    s->watched = false;
  }
  switch (event) {
  case AGENT_EVENT_IDLE:
    s->state = AGENT_STATE_IDLE;
    break;
  case AGENT_EVENT_WORKING:
    s->state = AGENT_STATE_WORKING;
    break;
  case AGENT_EVENT_WAITING:
    s->state = AGENT_STATE_WAITING;
    break;
  case AGENT_EVENT_DONE:
    s->state = AGENT_STATE_DONE;
    s->done_until_ms =
        done_timeout_s > 0 ? deadline(now_ms, done_timeout_s) : 0;
    break;
  case AGENT_EVENT_REST:
    if (s->state == AGENT_STATE_WORKING) {
      s->state = AGENT_STATE_IDLE;
    }
    break;
  default:
    break;
  }
  if (s->state != AGENT_STATE_DONE) {
    s->done_until_ms = 0;
  }
  return 0;
}

static int64_t session_deadline(const agent_session_t *s, int stale_timeout_s) {
  if (!s->used) {
    return 0;
  }
  if (s->state == AGENT_STATE_DONE) {
    return s->done_until_ms;
  }
  if (stale_timeout_s > 0 &&
      (s->state == AGENT_STATE_WORKING ||
       (s->state == AGENT_STATE_WAITING && !s->watched))) {
    return deadline(s->updated_ms, stale_timeout_s);
  }
  return 0;
}

bool agent_sessions_expire(int64_t now_ms, int stale_timeout_s) {
  bool changed = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    int64_t until = session_deadline(&sessions[i], stale_timeout_s);
    if (until > 0 && now_ms >= until) {
      sessions[i].state = AGENT_STATE_IDLE;
      sessions[i].done_until_ms = 0;
      changed = true;
    }
  }
  return changed;
}

void agent_sessions_remove_pid(pid_t pid) {
  if (pid <= 0) {
    return;
  }
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].pid == pid) {
      memset(&sessions[i], 0, sizeof(sessions[i]));
    }
  }
}

void agent_sessions_set_watched(uint64_t key, bool watched) {
  agent_session_t *s = find_session(key);
  if (s) {
    s->watched = watched && s->pid > 0;
  }
}

agent_state_t agent_sessions_resolve(void) {
  static const int PRIORITY[AGENT_STATE_COUNT] = {0, 1, 3, 2};
  agent_state_t result = AGENT_STATE_IDLE;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && PRIORITY[sessions[i].state] > PRIORITY[result]) {
      result = sessions[i].state;
    }
  }
  return result;
}

int64_t agent_sessions_next_deadline(int stale_timeout_s) {
  int64_t next = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    int64_t until = session_deadline(&sessions[i], stale_timeout_s);
    if (until > 0 && (!next || until < next)) {
      next = until;
    }
  }
  return next;
}

int agent_sessions_count(void) {
  int count = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    count += sessions[i].used;
  }
  return count;
}

int agent_sessions_format(char *buffer, size_t capacity, int64_t now_ms) {
  if (!capacity) {
    return 0;
  }
  if (!buffer) {
    return -1;
  }
  size_t used = 0;
  buffer[0] = '\0';
  for (int i = 0; i < AGENT_SESSIONS_MAX && used < capacity - 1; i++) {
    const agent_session_t *s = &sessions[i];
    if (!s->used) {
      continue;
    }
    int64_t age = now_ms > s->updated_ms ? (now_ms - s->updated_ms) / 1000 : 0;
    char pid[24] = "-";
    if (s->pid > 0) {
      snprintf(pid, sizeof(pid), "%jd", (intmax_t)s->pid);
    }
    int n = snprintf(buffer + used, capacity - used,
                     "%s %08" PRIx32 " %s %" PRId64 "s pid=%s\n", s->agent,
                     (uint32_t)(s->key >> 32), agent_state_name(s->state), age,
                     pid);
    if (n < 0) {
      break;
    }
    size_t written = (size_t)n;
    if (written >= capacity - used) {
      used = capacity - 1;
      break;
    }
    used += written;
  }
  return (int)used;
}

pid_t agent_sessions_pid(uint64_t key) {
  const agent_session_t *s = find_session(key);
  return s ? s->pid : 0;
}

int agent_sessions_pids(pid_t *pids, size_t capacity) {
  size_t count = 0;
  if (!pids) {
    return 0;
  }
  for (int i = 0; i < AGENT_SESSIONS_MAX && count < capacity; i++) {
    pid_t pid = sessions[i].pid;
    if (!sessions[i].used || pid <= 0) {
      continue;
    }
    bool duplicate = false;
    for (size_t j = 0; j < count; j++) {
      duplicate |= pids[j] == pid;
    }
    if (!duplicate) {
      pids[count++] = pid;
    }
  }
  return (int)count;
}

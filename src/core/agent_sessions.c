#include "agent_sessions_internal.h"
#include "utils/utf8.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

uint64_t next_order;
static uint64_t generation;

void touch_sessions(void) {
  generation++;
}

uint64_t agent_sessions_generation(void) {
  return generation;
}
agent_session_t sessions[AGENT_SESSIONS_MAX];
static bool done_sticky = true;
static bool focus_on;
static uint64_t focused_keys[AGENT_SESSIONS_MAX];
static size_t focused_count;
int applied_done_timeout;
static const char *const EVENT_NAMES[AGENT_EVENT_COUNT] = {
    "idle", "working", "waiting",   "done", "start",
    "rest", "end",     "interrupt", "fail"};

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
  next_order = 0;
  done_sticky = true;
  focus_on = false;
  focused_count = 0;
  applied_done_timeout = 0;
  touch_sessions();
}

void agent_sessions_set_done_sticky(bool sticky) {
  done_sticky = sticky;
}

void agent_sessions_observe_focus(bool available, const uint64_t *keys,
                                  size_t count) {
  focus_on = available;
  focused_count = 0;
  if (!available || !keys || !count) {
    return;
  }
  if (count > AGENT_SESSIONS_MAX) {
    count = AGENT_SESSIONS_MAX;
  }
  memcpy(focused_keys, keys, count * sizeof(*keys));
  focused_count = count;
}

static bool key_is_focused(uint64_t key) {
  if (!focus_on) {
    return false;
  }
  for (size_t i = 0; i < focused_count; i++) {
    if (focused_keys[i] == key) {
      return true;
    }
  }
  return false;
}

agent_session_t *find_session(uint64_t key) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].key == key) {
      return &sessions[i];
    }
  }
  return NULL;
}

// One agent process is one sign. Codex worker threads and Grok's start-up
// report under ids of their own; those join the row their process already
// has instead of replacing it. Such an id only moves working and waiting.
agent_session_t *find_alias(uint64_t key) {
  for (int i = 0; key && i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].alias == key) {
      return &sessions[i];
    }
  }
  return NULL;
}

static agent_session_t *find_process(const char *agent, pid_t pid) {
  for (int i = 0; pid > 0 && i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].pid == pid &&
        !strcmp(sessions[i].agent, agent)) {
      return &sessions[i];
    }
  }
  return NULL;
}

// The id that names itself is the one the user is talking to.
static void promote(agent_session_t *s, uint64_t key) {
  s->alias = s->provisional ? 0 : s->key;
  s->provisional = false;
  s->key = key;
  touch_sessions();
}

void agent_sessions_set_provisional(uint64_t key) {
  agent_session_t *s = find_session(key);
  if (s)
    s->provisional = true;
}

void agent_sessions_adopt(uint64_t key) {
  agent_session_t *s = find_session(key) ? NULL : find_alias(key);
  if (s)
    promote(s, key);
}

agent_session_t *available_slot(void) {
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

// Both are shown until someone has looked at them.

void agent_sessions_note_focused(uint64_t key, int64_t now_ms,
                                 int done_timeout_s) {
  if (!key || now_ms < 0 || done_timeout_s < 0) {
    return;
  }
  agent_session_t *s = find_session(key);
  if (!s || !s->unread || !finished(s->state)) {
    return;
  }
  s->unread = false;
  s->done_until_ms = done_timeout_s > 0 ? deadline(now_ms, done_timeout_s) : 0;
  touch_sessions();
}

void agent_sessions_note_click(uint64_t key, int64_t now_ms) {
  agent_sessions_note_focused(key, now_ms, applied_done_timeout);
}

void agent_sessions_configure_done(bool sticky, int64_t now_ms,
                                   int done_timeout_s) {
  if (now_ms < 0 || done_timeout_s < 0)
    return;
  done_sticky = sticky;
  applied_done_timeout = done_timeout_s;
  if (sticky)
    return;
  bool changed = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *s = &sessions[i];
    if (s->used && s->unread && finished(s->state)) {
      s->unread = false;
      s->done_until_ms =
          done_timeout_s > 0 ? deadline(now_ms, done_timeout_s) : 0;
      changed = true;
    }
  }
  if (changed)
    touch_sessions();
}

// One agent process keeps the session that just registered it. pid 0 is
// excluded: those sessions are not tied to a process. Same-key subagents
// never reach here as a second row.
void drop_same_process(const agent_session_t *keep) {
  if (!keep || keep->pid <= 0) {
    return;
  }
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *other = &sessions[i];
    if (!other->used || other == keep || other->pid != keep->pid) {
      continue;
    }
    if (strcmp(other->agent, keep->agent) != 0) {
      continue;
    }
    memset(other, 0, sizeof(*other));
  }
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
  applied_done_timeout = done_timeout_s;
  agent_session_t *s = find_session(key);
  if (!s && key) {
    agent_session_t *host = find_alias(key);
    if (!host)
      host = find_process(agent, pid);
    if (host && host->provisional) {
      promote(host, key);
      s = host;
    } else if (host && event == AGENT_EVENT_START) {
      // A new conversation in the same process starts from idle.
      promote(host, key);
      host->state = AGENT_STATE_IDLE;
      host->state_since_ms = now_ms;
      s = host;
    } else if (host) {
      if (event != AGENT_EVENT_WORKING && event != AGENT_EVENT_WAITING)
        return 0;
      host->alias = key;
      s = host;
    }
  }
  if (event == AGENT_EVENT_END) {
    if (s) {
      memset(s, 0, sizeof(*s));
      touch_sessions();
    }
    return 0;
  }
  if ((event == AGENT_EVENT_INTERRUPT || event == AGENT_EVENT_FAIL) &&
      (!s ||
       (s->state != AGENT_STATE_WORKING && s->state != AGENT_STATE_WAITING)))
    return 0;
  if (!s) {
    if (event == AGENT_EVENT_IDLE || event == AGENT_EVENT_REST) {
      return 0;
    }
    s = available_slot();
    *s = (agent_session_t){.used = true,
                           .key = key,
                           .order = ++next_order,
                           .created_ms = now_ms,
                           .state_since_ms = now_ms};
    snprintf(s->name, sizeof(s->name), "%s %04" PRIx16, agent,
             (uint16_t)(key >> 48));
    memcpy(s->agent, agent, strlen(agent) + 1);
    if (is_new) {
      *is_new = true;
    }
  }
  bool created = is_new && *is_new;
  pid_t previous_pid = s->pid;
  s->updated_ms = now_ms;
  s->answered_until_ms = 0;
  if (pid > 0 && pid != s->pid) {
    s->pid = pid;
    s->watched = false;
    memset(s->terminals, 0, sizeof(s->terminals));
  }
  agent_state_t previous = s->state;
  switch (event) {
  case AGENT_EVENT_INTERRUPT:
  case AGENT_EVENT_IDLE:
    s->state = AGENT_STATE_IDLE;
    break;
  case AGENT_EVENT_WORKING:
    s->state = AGENT_STATE_WORKING;
    break;
  case AGENT_EVENT_WAITING:
    s->state = AGENT_STATE_WAITING;
    break;
  case AGENT_EVENT_FAIL:
  case AGENT_EVENT_DONE: {
    bool seen = !done_sticky || key_is_focused(key);
    s->state = event == AGENT_EVENT_FAIL ? AGENT_STATE_ERROR : AGENT_STATE_DONE;
    s->unread = !seen;
    s->done_until_ms =
        seen && done_timeout_s > 0 ? deadline(now_ms, done_timeout_s) : 0;
    break;
  }
  case AGENT_EVENT_REST:
    if (s->state == AGENT_STATE_WORKING) {
      s->state = AGENT_STATE_IDLE;
    }
    break;
  default:
    break;
  }
  if (s->state != previous)
    s->state_since_ms = now_ms;
  if (!finished(s->state)) {
    s->done_until_ms = 0;
    s->unread = false;
  }
  if (created || s->pid != previous_pid) {
    drop_same_process(s);
  }
  touch_sessions();
  return 0;
}

static int64_t session_deadline(const agent_session_t *s, int stale_timeout_s) {
  if (!s->used) {
    return 0;
  }
  if (finished(s->state)) {
    if (s->unread && s->pid <= 0 && stale_timeout_s > 0)
      return deadline(s->updated_ms, stale_timeout_s);
    return s->done_until_ms;
  }
  // A session with no pid cannot be tied to a window or a process exit.
  // Drop it after it has been idle with no events for the stale timeout.
  // Finished signs above first wait for acknowledgement and then fold.
  if (stale_timeout_s > 0 && s->pid <= 0 && s->state == AGENT_STATE_IDLE) {
    return deadline(s->updated_ms, stale_timeout_s);
  }
  if (stale_timeout_s > 0 &&
      (s->state == AGENT_STATE_WORKING ||
       (s->state == AGENT_STATE_WAITING && !s->watched))) {
    return deadline(s->updated_ms, stale_timeout_s);
  }
  return 0;
}

bool agent_sessions_answer(uint64_t key, int64_t now_ms, int revert_s) {
  agent_session_t *s = find_session(key);
  if (!s || s->state != AGENT_STATE_WAITING || now_ms < 0 || revert_s <= 0)
    return false;
  s->state = AGENT_STATE_WORKING;
  s->state_since_ms = now_ms;
  s->answered_until_ms = deadline(now_ms, revert_s);
  touch_sessions();
  return true;
}

bool agent_sessions_expire(int64_t now_ms, int stale_timeout_s) {
  bool changed = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *answered = &sessions[i];
    if (answered->used && answered->answered_until_ms &&
        now_ms >= answered->answered_until_ms) {
      // Nothing followed the key press: the question is still open.
      answered->answered_until_ms = 0;
      if (answered->state == AGENT_STATE_WORKING) {
        answered->state = AGENT_STATE_WAITING;
        answered->state_since_ms = now_ms;
        changed = true;
      }
    }
    int64_t until = session_deadline(&sessions[i], stale_timeout_s);
    if (until > 0 && now_ms >= until) {
      if (finished(sessions[i].state) && sessions[i].unread) {
        sessions[i].unread = false;
        sessions[i].done_until_ms = applied_done_timeout > 0
                                        ? deadline(now_ms, applied_done_timeout)
                                        : 0;
      } else if (sessions[i].pid <= 0 &&
                 sessions[i].state == AGENT_STATE_IDLE) {
        memset(&sessions[i], 0, sizeof(sessions[i]));
      } else {
        bool was_finished = finished(sessions[i].state);
        sessions[i].state = AGENT_STATE_IDLE;
        sessions[i].state_since_ms = now_ms;
        sessions[i].done_until_ms = 0;
        sessions[i].unread = false;
        // The stale working/waiting transition lands on idle at the same
        // instant the no-pid idle lifetime is already over.
        if (!was_finished && sessions[i].pid <= 0 && stale_timeout_s > 0 &&
            now_ms >= deadline(sessions[i].updated_ms, stale_timeout_s)) {
          memset(&sessions[i], 0, sizeof(sessions[i]));
        }
      }
      changed = true;
    }
  }
  if (changed)
    touch_sessions();
  return changed;
}

void agent_sessions_remove_pid(pid_t pid) {
  if (pid <= 0) {
    return;
  }
  bool removed = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (sessions[i].used && sessions[i].pid == pid) {
      memset(&sessions[i], 0, sizeof(sessions[i]));
      removed = true;
    }
  }
  if (removed)
    touch_sessions();
}

void agent_sessions_set_watched(uint64_t key, bool watched) {
  agent_session_t *s = find_session(key);
  if (s) {
    s->watched = watched && s->pid > 0;
  }
}

static bool kitty_listen_ok(const char *listen, size_t capacity) {
  if (!listen)
    return false;
  size_t n = 0;
  while (listen[n]) {
    unsigned char c = (unsigned char)listen[n];
    if (c < 0x20 || c == 0x7f)
      return false;
    if (++n >= capacity)
      return false;
  }
  return n >= 5 && !strncmp(listen, "unix:", 5);
}

void agent_sessions_set_kitty(uint64_t key, pid_t kitty_pid, uint64_t window,
                              const char *listen) {
  agent_session_t *s = find_session(key);
  if (!s || s->pid <= 0 ||
      !kitty_listen_ok(listen, sizeof(s->terminals[0].socket)))
    return;
  snprintf(s->terminals[0].socket, sizeof(s->terminals[0].socket), "%s",
           listen);
  s->terminals[0].pane = window;
  s->terminals[0].client_pid =
      kitty_pid > 1 && kitty_pid <= 4194304 ? kitty_pid : 0;
  s->terminals[0].kind = TERMINAL_KITTY;
}

bool agent_sessions_kitty(pid_t pid, uint64_t *window, char *listen,
                          size_t capacity) {
  if (pid <= 0 || !window || !listen || !capacity)
    return false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const agent_session_t *s = &sessions[i];
    if (!s->used || s->pid != pid || !s->terminals[0].socket[0])
      continue;
    size_t n = strlen(s->terminals[0].socket);
    if (n >= capacity)
      return false;
    *window = s->terminals[0].pane;
    memcpy(listen, s->terminals[0].socket, n + 1);
    return true;
  }
  return false;
}

agent_state_t agent_sessions_resolve(void) {
  // waiting > error > done > working > idle
  static const int PRIORITY[AGENT_STATE_COUNT] = {0, 1, 4, 2, 3};
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
    int64_t answered = sessions[i].used ? sessions[i].answered_until_ms : 0;
    if (answered > 0 && (!until || answered < until))
      until = answered;
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

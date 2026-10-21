#include "core/agent_cursor.h"

#include "agent_sessions_internal.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "platform/agent_terminal.h"
#include "platform/terminal_screen.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#define HOOK_GRACE_MS 1500
#define PROBE_GAP_MS  1000

typedef struct {
  uint64_t order;
  int64_t started_ms;
  bool sampled;
} sample_t;

static sample_t samples[AGENT_SESSIONS_MAX];
static struct {
  uint64_t order, serial;
  pid_t pid;
  agent_terminal_t target;
} probe;
static unsigned next_slot;
static int64_t due_at;

static int64_t after_ms(int64_t now, int interval) {
  return now > INT64_MAX - interval ? INT64_MAX : now + interval;
}

static bool eligible(const agent_session_t *s) {
  return s->used && !strcmp(s->agent, "cursor") && s->pid > 1 &&
         s->cursor_tool_pending &&
         (s->state == AGENT_STATE_WORKING ||
          (s->state == AGENT_STATE_WAITING && s->cursor_waiting));
}

static void waiting(agent_session_t *s, bool value, int64_t now) {
  agent_state_t previous = s->state;
  if (value)
    s->state = AGENT_STATE_WAITING;
  else if (s->cursor_waiting && s->state == AGENT_STATE_WAITING)
    s->state = AGENT_STATE_WORKING;
  if (s->cursor_waiting || value)
    s->answered_until_ms = 0;
  s->cursor_waiting = value;
  if (s->state != previous) {
    s->state_since_ms = now;
    touch_sessions();
  }
}

static agent_session_t *pending_session(void) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *s = &sessions[i];
    agent_terminal_t target;
    if (eligible(s) && s->order == probe.order &&
        s->hook_serial == probe.serial && s->pid == probe.pid &&
        terminal_screen_target(s->pid, &target) &&
        target.kind == probe.target.kind && target.pane == probe.target.pane &&
        !strcmp(target.socket, probe.target.socket))
      return s;
  }
  return NULL;
}

void agent_cursor_reset(void) {
  terminal_screen_cleanup();
  memset(samples, 0, sizeof(samples));
  memset(&probe, 0, sizeof(probe));
  due_at = 0;
  next_slot = 0;
}

void agent_cursor_sync(bool enabled, int64_t now_ms) {
  due_at = 0;
  if (!enabled) {
    if (probe.order) {
      terminal_screen_cleanup();
      probe.order = 0;
    }
    for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
      if (sessions[i].used && sessions[i].cursor_waiting)
        waiting(&sessions[i], false, now_ms);
    return;
  }
  if (probe.order) {
    agent_session_t *s = pending_session();
    if (!s) {
      // Any newer hook (even in the same millisecond), removal or terminal
      // replacement wins over this capture. Kill/reap without applying it.
      terminal_screen_cleanup();
    } else {
      bool value = false;
      int result = terminal_screen_poll(&value);
      if (!result)
        return;
      waiting(s, result > 0 && value, now_ms);
    }
    probe.order = 0;
  }
  int chosen = -1;
  agent_terminal_t target = {0};
  for (unsigned n = 0; n < AGENT_SESSIONS_MAX; n++) {
    unsigned i = (next_slot + n) % AGENT_SESSIONS_MAX;
    agent_session_t *s = &sessions[i];
    if (!eligible(s))
      continue;
    agent_terminal_t candidate;
    if (!terminal_screen_target(s->pid, &candidate)) {
      waiting(s, false, now_ms);
      continue;
    }
    sample_t *sample = &samples[i];
    if (sample->order != s->order)
      *sample = (sample_t){.order = s->order};
    int64_t due = after_ms(s->updated_ms, HOOK_GRACE_MS);
    int64_t retry = after_ms(sample->started_ms, PROBE_GAP_MS);
    if (sample->sampled && retry > due)
      due = retry;
    if (!due_at || due < due_at) {
      due_at = due;
      chosen = (int)i;
      target = candidate;
    }
  }
  if (chosen < 0 || due_at > now_ms)
    return;
  agent_session_t *s = &sessions[chosen];
  samples[chosen].sampled = true;
  samples[chosen].started_ms = now_ms;
  next_slot = ((unsigned)chosen + 1) % AGENT_SESSIONS_MAX;
  if (terminal_screen_start(&target)) {
    probe.order = s->order;
    probe.serial = s->hook_serial;
    probe.pid = s->pid;
    probe.target = target;
    due_at = 0;
  } else {
    waiting(s, false, now_ms);
    due_at = after_ms(now_ms, PROBE_GAP_MS);
  }
}

int agent_cursor_timeout(int64_t now_ms) {
  if (probe.order)
    return terminal_screen_timeout(now_ms);
  if (!due_at)
    return -1;
  int64_t left = due_at - now_ms;
  return left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}

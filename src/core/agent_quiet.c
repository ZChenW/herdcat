#include "core/agent_quiet.h"

#include "core/agent_adapters.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "platform/agent_output.h"

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#define QUIET_BYTES 256
#define WINDOW_MS   1000
#define RECENT_MS   10000
#define PAIR_MS     5000
// Scheduler delay must not turn an arbitrary gap into a quiet window.
#define WINDOW_MAX_MS 1250

typedef struct {
  uint64_t key, order, wchar;
  pid_t pid;
  int64_t updated_ms, event_ms, sampled_ms, due_ms;
  unsigned quiet;
  bool baseline, stopped;
} quiet_sample_t;

static quiet_sample_t samples[AGENT_SESSIONS_MAX];
static int64_t next_deadline;

void agent_quiet_reset(void) {
  memset(samples, 0, sizeof(samples));
  next_deadline = 0;
}

int64_t agent_quiet_deadline(void) {
  return next_deadline;
}

bool agent_quiet_stopped(uint64_t key, uint64_t order, int64_t updated_ms) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (samples[i].key == key && samples[i].order == order &&
        samples[i].updated_ms == updated_ms && samples[i].stopped)
      return true;
  return false;
}

void agent_quiet_record(uint64_t key, int64_t now_ms) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++) {
    const agent_session_view_t *view = &views[i];
    if (view->key != key)
      continue;
    if (view->state == AGENT_STATE_IDLE &&
        agent_quiet_stopped(key, view->order, view->updated_ms)) {
      agent_sessions_working(key, now_ms);
      return;
    }
    if (view->state != AGENT_STATE_WORKING)
      return;
    for (int j = 0; j < AGENT_SESSIONS_MAX; j++)
      if (samples[j].order == view->order && samples[j].due_ms) {
        samples[j].event_ms = now_ms;
        samples[j].due_ms = now_ms;
        samples[j].baseline = true;
        samples[j].quiet = 0;
        return;
      }
  }
}

void agent_quiet_sync(bool enabled, int64_t now_ms) {
  if (!enabled) {
    agent_quiet_reset();
    return;
  }
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  bool retained[AGENT_SESSIONS_MAX] = {false};
  next_deadline = 0;
  for (int i = 0; i < count; i++) {
    const agent_session_view_t *view = &views[i];
    if (view->state == AGENT_STATE_IDLE &&
        agent_quiet_stopped(view->key, view->order, view->updated_ms)) {
      for (int j = 0; j < AGENT_SESSIONS_MAX; j++)
        retained[j] |= samples[j].order == view->order;
      continue;
    }
    const agent_adapter_t *adapter = agent_adapter_find(view->agent);
    if (view->state != AGENT_STATE_WORKING || view->pid <= 0 ||
        strcmp(adapter->name, view->agent) || !adapter->continuous_output)
      continue;
    int slot = -1;
    for (int j = 0; j < AGENT_SESSIONS_MAX; j++)
      if (samples[j].order == view->order) {
        slot = j;
        break;
      }
    if (slot < 0)
      for (int j = 0; j < AGENT_SESSIONS_MAX; j++) {
        bool present = false;
        for (int k = 0; k < count; k++)
          present |= samples[j].order == views[k].order;
        if (!samples[j].order || (!retained[j] && !present)) {
          slot = j;
          samples[j] = (quiet_sample_t){0};
          break;
        }
      }
    if (slot < 0)
      continue;
    retained[slot] = true;
    quiet_sample_t *sample = &samples[slot];
    bool fresh = !sample->order || sample->pid != view->pid ||
                 sample->updated_ms != view->updated_ms;
    if (fresh) {
      *sample = (quiet_sample_t){.key = view->key,
                                 .order = view->order,
                                 .pid = view->pid,
                                 .updated_ms = view->updated_ms,
                                 .event_ms = view->updated_ms,
                                 .sampled_ms = now_ms};
      if (agent_output_wchar(view->pid, &sample->wchar))
        sample->due_ms = now_ms + WINDOW_MS;
    } else if (sample->due_ms && now_ms >= sample->due_ms) {
      uint64_t wchar;
      if (!agent_output_wchar(view->pid, &wchar)) {
        sample->due_ms = 0;  // Retry only on a new event, never periodically.
      } else {
        int64_t elapsed = now_ms - sample->sampled_ms;
        bool window = !sample->baseline && elapsed >= WINDOW_MS &&
                      elapsed <= WINDOW_MAX_MS && wchar >= sample->wchar;
        bool quiet = window && wchar - sample->wchar < QUIET_BYTES;
        sample->quiet = quiet ? sample->quiet + 1 : 0;
        if (sample->quiet == 2 && now_ms - sample->event_ms >= WINDOW_MS) {
          agent_sessions_interrupt(view->key, now_ms);
          sample->stopped = true;
          sample->due_ms = 0;
          sample->updated_ms = now_ms;
          continue;
        }
        bool recent = sample->sampled_ms - sample->event_ms < RECENT_MS;
        // Sparse pairs: baseline, then a one-second window. A quiet first
        // window adds one adjacent confirmation instead of counting the gap.
        if (recent || sample->baseline || quiet || !window) {
          sample->due_ms = now_ms + WINDOW_MS;
          sample->baseline = false;
        } else {
          sample->due_ms = now_ms + PAIR_MS - WINDOW_MS;
          sample->baseline = true;
        }
        sample->sampled_ms = now_ms;
        sample->wchar = wchar;
      }
    }
    if (sample->due_ms && (!next_deadline || sample->due_ms < next_deadline))
      next_deadline = sample->due_ms;
  }
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (!retained[i])
      samples[i] = (quiet_sample_t){0};
}

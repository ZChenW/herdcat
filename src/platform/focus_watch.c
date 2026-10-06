#define _GNU_SOURCE
#include "platform/focus_watch.h"

#include "compositor_internal.h"
#include "core/agent_adapters.h"
#include "platform/agent_watch.h"
#include "platform/compositor.h"
#include "platform/focus_current.h"
#include "utils/json_string.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#define WINDOW_MAX FOCUS_WATCH_WINDOW_MAX
static bool have_focus;
static uint64_t focused_id;
static focus_window_t windows[WINDOW_MAX];
static size_t window_count;
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void focus_watch_apply(const focus_watch_event_t *event,
                       const focus_window_t *parsed) {
  if (event->kind == FOCUS_WATCH_FOCUS) {
    have_focus = !event->id_null && event->id;
    focused_id = have_focus ? event->id : 0;
  } else if (event->kind == FOCUS_WATCH_CLOSE) {
    size_t kept = 0;
    for (size_t i = 0; i < window_count; i++) {
      if (windows[i].id != event->id) {
        windows[kept++] = windows[i];
      }
    }
    window_count = kept;
    if (have_focus && focused_id == event->id) {
      have_focus = false;
      focused_id = 0;
    }
  } else if (event->kind == FOCUS_WATCH_UPSERT && event->pid > 0) {
    bool replaced = false;
    for (size_t i = 0; i < window_count; i++) {
      if (windows[i].id == event->id) {
        windows[i].pid = event->pid;
        memcpy(windows[i].title, event->title, sizeof(event->title));
        if (!event->resting)
          windows[i].resting_since_ms = 0;
        else if (!windows[i].resting_since_ms)
          windows[i].resting_since_ms = now_ms();
        replaced = true;
        break;
      }
    }
    if (!replaced && window_count < WINDOW_MAX) {
      windows[window_count] =
          (focus_window_t){.id = event->id,
                           .pid = event->pid,
                           .resting_since_ms = event->resting ? now_ms() : 0};
      memcpy(windows[window_count++].title, event->title, sizeof(event->title));
    }
    if (event->has_focused) {
      have_focus = true;
      focused_id = event->focused;
    }
  } else if (event->kind == FOCUS_WATCH_WINDOWS) {
    // A reconnect resends the list. A title that was already at rest keeps
    // its start; a new one starts now.
    focus_window_t old[WINDOW_MAX];
    size_t old_count = window_count;
    memcpy(old, windows, sizeof(old[0]) * old_count);
    int64_t now = now_ms();
    window_count = 0;
    for (int i = 0; i < event->count && window_count < WINDOW_MAX; i++) {
      focus_window_t window = parsed[i];
      if (window.resting_since_ms) {
        window.resting_since_ms = now;
        for (size_t k = 0; k < old_count; k++)
          if (old[k].id == window.id && old[k].resting_since_ms)
            window.resting_since_ms = old[k].resting_since_ms;
      }
      windows[window_count++] = window;
    }
    // The first line of a new stream is the full window list. No separate
    // focus event follows it, so this is the only source of the initial focus.
    have_focus = event->has_focused;
    focused_id = have_focus ? event->focused : 0;
  }
}
size_t focus_watch_windows(focus_window_t *out, size_t capacity) {
  if (!out || !capacity || !window_count)
    return 0;
  size_t count = window_count < capacity ? window_count : capacity;
  memcpy(out, windows, count * sizeof(*out));
  return count;
}
uint64_t focus_watch_focused_id(void) {
  return have_focus ? focused_id : 0;
}
void focus_watch_lost(void) {
  have_focus = false;
  focused_id = 0;
  window_count = 0;
}
int focus_watch_init(void) {
  agent_watch_on_ready(focus_watch_ready);
  const compositor_ops_t *ops = compositor_selected();
  return ops ? ops->connect() : 0;
}
void focus_watch_cleanup(void) {
  COMPOSITOR_NIRI.cleanup();
  COMPOSITOR_HYPRLAND.cleanup();
  COMPOSITOR_SWAY.cleanup();
  focus_watch_lost();
}
void focus_watch_poll(void) {
  const compositor_ops_t *ops = compositor_selected();
  if (ops)
    ops->events();
}
void focus_watch_ready(uint32_t token) {
  const compositor_ops_t *ops = compositor_selected();
  if (ops)
    ops->ready(token);
}
int focus_watch_timeout(void) {
  const compositor_ops_t *ops = compositor_selected();
  return ops ? ops->timeout() : -1;
}
bool focus_watch_available(void) {
  const compositor_ops_t *ops = compositor_selected();
  return ops && ops->available();
}
uint64_t focus_watch_match(uint64_t focused, const focus_window_t *wins,
                           size_t windows_count,
                           const agent_session_view_t *sessions, size_t count) {
  if (!focused || !wins || !sessions) {
    return 0;
  }
  uint64_t best = 0;
  int64_t updated = -1;
  for (size_t i = 0; i < count; i++) {
    uint64_t id = 0;
    if (sessions[i].pid <= 1 ||
        !focus_terminal_window(sessions[i].pid, &sessions[i].terminal,
                               sessions[i].name, wins, windows_count, &id) ||
        id != focused) {
      continue;
    }
    if (sessions[i].updated_ms >= updated) {
      best = sessions[i].key;
      updated = sessions[i].updated_ms;
    }
  }
  return best;
}
int focus_watch_matching(uint64_t focused, const focus_window_t *wins,
                         size_t windows_count,
                         const agent_session_view_t *sessions, size_t count,
                         uint64_t *keys, size_t capacity) {
  if (!focused || !wins || !sessions || !keys || !capacity) {
    return 0;
  }
  size_t written = 0;
  for (size_t i = 0; i < count && written < capacity; i++) {
    uint64_t id = 0;
    if (sessions[i].pid <= 1 ||
        !focus_terminal_window(sessions[i].pid, &sessions[i].terminal,
                               sessions[i].name, wins, windows_count, &id) ||
        id != focused) {
      continue;
    }
    keys[written++] = sessions[i].key;
  }
  return (int)written;
}
int focus_watch_focused_keys(const agent_session_view_t *sessions, size_t count,
                             uint64_t *keys, size_t capacity) {
  if (!focus_watch_available() || !have_focus) {
    return 0;
  }
  focus_pane_t panes[FOCUS_PANE_MAX];
  size_t panes_count = focus_pane_copy(panes, FOCUS_PANE_MAX);
  return focus_current_seen(focused_id, windows, window_count, sessions, count,
                            panes, panes_count, keys, capacity);
}
uint64_t focus_watch_focused_session(const agent_session_view_t *sessions,
                                     size_t count) {
  if (!focus_watch_available()) {
    return 0;
  }
  focus_current_observe(have_focus ? focused_id : 0);
  if (!have_focus) {
    return 0;
  }
  focus_pane_t panes[FOCUS_PANE_MAX];
  size_t panes_count = focus_pane_copy(panes, FOCUS_PANE_MAX);
  uint64_t clicked_window = 0;
  uint64_t clicked_key = 0;
  focus_current_clicked(&clicked_window, &clicked_key);
  return focus_current_choose(focused_id, windows, window_count, sessions,
                              count, panes, panes_count, clicked_window,
                              clicked_key);
}
int focus_watch_query(const agent_session_view_t *sessions, size_t count,
                      uint64_t *keys, size_t capacity, uint64_t *chosen) {
  if (!chosen)
    return 0;
  *chosen = 0;
  if (!focus_watch_available())
    return 0;
  focus_current_observe(have_focus ? focused_id : 0);
  if (!have_focus)
    return 0;
  focus_pane_t panes[FOCUS_PANE_MAX];
  size_t pane_count = focus_pane_copy(panes, FOCUS_PANE_MAX);
  uint64_t clicked_window = 0, clicked_key = 0;
  focus_current_clicked(&clicked_window, &clicked_key);
  return focus_current_query(focused_id, windows, window_count, sessions, count,
                             panes, pane_count, clicked_window, clicked_key,
                             keys, capacity, chosen);
}
int focus_watch_rested(const focus_window_t *wins, size_t windows_count,
                       const agent_session_view_t *sessions, size_t count,
                       const focus_pane_t *panes, size_t pane_count,
                       int64_t now, uint64_t *keys, size_t capacity,
                       int *next_ms) {
  if (next_ms)
    *next_ms = -1;
  if (!wins || !sessions || !keys)
    return 0;
  size_t written = 0;
  for (size_t w = 0; w < windows_count; w++) {
    if (wins[w].resting_since_ms <= 0)
      continue;
    // A split report narrows this to the split whose title is showing.
    // Without one the title is only trusted for a window with one session.
    uint64_t owners[2];
    if (focus_current_seen(wins[w].id, wins, windows_count, sessions, count,
                           panes, pane_count, owners, 2) != 1)
      continue;
    const agent_session_view_t *session = NULL;
    for (size_t i = 0; i < count; i++)
      if (sessions[i].key == owners[0])
        session = &sessions[i];
    if (!session || session->state != AGENT_STATE_WORKING ||
        session->terminal.kind == TERMINAL_TMUX)
      continue;
    const agent_adapter_t *adapter = agent_adapter_find(session->agent);
    if (!adapter->rest_title || strcmp(adapter->name, session->agent))
      continue;
    // The grace period covers the title lagging behind a new submission
    // and the Stop hook that follows a normal finish.
    int64_t since = wins[w].resting_since_ms > session->updated_ms
                        ? wins[w].resting_since_ms
                        : session->updated_ms;
    int64_t left = since + FOCUS_REST_GRACE_MS - now;
    if (left > 0) {
      if (next_ms && (*next_ms < 0 || left < *next_ms))
        *next_ms = (int)left;
    } else if (written < capacity) {
      keys[written++] = session->key;
    }
  }
  return (int)written;
}
int focus_watch_rested_now(const agent_session_view_t *sessions, size_t count,
                           int64_t now, uint64_t *keys, size_t capacity,
                           int *next_ms) {
  if (next_ms)
    *next_ms = -1;
  if (!focus_watch_available())
    return 0;
  focus_pane_t panes[FOCUS_PANE_MAX];
  size_t panes_count = focus_pane_copy(panes, FOCUS_PANE_MAX);
  return focus_watch_rested(windows, window_count, sessions, count, panes,
                            panes_count, now, keys, capacity, next_ms);
}

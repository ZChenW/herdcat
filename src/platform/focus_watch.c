#define _GNU_SOURCE
#include "platform/focus_watch.h"

#include "compositor_internal.h"
#include "core/agent_sessions.h"
#include "platform/agent_watch.h"
#include "platform/compositor.h"
#include "platform/focus.h"
#include "platform/focus_current.h"

#include <stdint.h>
#include <string.h>
#define WINDOW_MAX FOCUS_WATCH_WINDOW_MAX
static bool have_focus;
static uint64_t focused_id;
static focus_window_t windows[WINDOW_MAX];
static size_t window_count;
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
        replaced = true;
        break;
      }
    }
    if (!replaced && window_count < WINDOW_MAX) {
      windows[window_count] =
          (focus_window_t){.id = event->id, .pid = event->pid};
      memcpy(windows[window_count++].title, event->title, sizeof(event->title));
    }
    if (event->has_focused) {
      have_focus = true;
      focused_id = event->focused;
    }
  } else if (event->kind == FOCUS_WATCH_WINDOWS) {
    window_count = 0;
    for (int i = 0; i < event->count && window_count < WINDOW_MAX; i++)
      windows[window_count++] = parsed[i];
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
    if (sessions[i].parent || sessions[i].pid <= 1 ||
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
    if (sessions[i].parent || sessions[i].pid <= 1 ||
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

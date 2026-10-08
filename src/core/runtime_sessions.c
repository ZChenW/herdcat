#define _GNU_SOURCE
#include "config/config.h"
#include "config/sign_options.h"
#include "core/agent_quiet.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "graphics/animation.h"
#include "platform/agent_discover.h"
#include "platform/agent_terminal.h"
#include "platform/agent_watch.h"
#include "platform/compositor.h"
#include "platform/focus.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "platform/input.h"
#include "platform/overlay_signs.h"
#include "platform/transcript_watch.h"
#include "platform/wayland.h"
#include "runtime_internal.h"

#include <fcntl.h>

static bool hidden, paused;

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
void agent_refresh(void) {
  pid_t pids[AGENT_SESSIONS_MAX];
  int count = agent_sessions_pids(pids, AGENT_SESSIONS_MAX);
  agent_watch_retain(pids, (size_t)count);
  transcript_watch_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_quiet_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_state_t state = agent_sessions_resolve();
  if (state != animation_get_agent_state()) {
    animation_set_agent_state(state);
  }
  animation_use_agent_frames(config.sign_style == SIGN_STYLE_OFF);
}

static bool has_pid(const pid_t *pids, int count, pid_t pid) {
  for (int i = 0; i < count; i++) {
    if (pids[i] == pid) {
      return true;
    }
  }
  return false;
}

static void capture_kitty(uint64_t key, pid_t pid);

void discover_expanded(void) {
  // focus_watch keeps at most 128 windows.
  focus_window_t windows[128];
  size_t count = focus_watch_windows(windows, 128);
  int created = agent_discover_scan(
      "/proc", windows, count, AGENT_DISCOVER_PROCESS_MAX,
      AGENT_DISCOVER_CREATE_MAX, monotonic_ms(), config.agent_done_timeout);
  if (created <= 0)
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int sessions = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < sessions; i++) {
    pid_t pid = views[i].pid;
    if (pid <= 0)
      continue;
    if (views[i].terminal.kind == TERMINAL_NONE)
      capture_kitty(views[i].key, pid);
    if (agent_watch_add(pid) == 0) {
      agent_sessions_set_watched(views[i].key, true);
      continue;
    }
    if (errno == ESRCH)
      agent_sessions_remove_pid(pid);
  }
}

bool kitty_for_session(pid_t pid, uint64_t *window, char *listen,
                       size_t capacity) {
  return agent_sessions_kitty(pid, window, listen, capacity);
}

static uint64_t pending_wezterm_focus;
static void try_deferred_wezterm_focus(void);

void terminal_resolved(pid_t pid, const agent_terminal_t *terminal) {
  if (terminal->kind != TERMINAL_WEZTERM) {
    agent_sessions_terminal_resolved(pid, terminal);
    return;
  }
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
  size_t n = focus_watch_windows(windows, FOCUS_WATCH_WINDOW_MAX);
  focus_current_wezterm_resolve(pid, terminal, windows, n, views,
                                (size_t)count);
  for (int i = 0; i < count; i++)
    agent_sessions_terminal_resolved(views[i].pid, &views[i].terminal);
  try_deferred_wezterm_focus();
}
void terminal_current(pid_t pid, uint64_t window, const char *socket,
                      const focus_wezterm_pane_t *panes, size_t count) {
  if (!focus_watch_available() || focus_watch_focused_id() != window)
    return;
  agent_terminal_t terminal;
  if (!agent_sessions_terminal(pid, &terminal, NULL, 0) ||
      terminal.kind != TERMINAL_WEZTERM || strcmp(terminal.socket, socket) != 0)
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int n = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t pane = 0;
  bool known = focus_current_wezterm_pane(window, terminal.socket, views,
                                          (size_t)n, panes, count, &pane);
  if (known) {
    terminal.current_known = true;
    terminal.current_pane = pane;
    terminal.window = window;
    terminal_resolved(pid, &terminal);
    return;
  }
  for (int i = 0; i < n; i++) {
    agent_terminal_t t = views[i].terminal;
    if (t.kind == TERMINAL_WEZTERM && !strcmp(t.socket, terminal.socket) &&
        (!t.window || t.window == window)) {
      t.current_known = false;
      agent_sessions_terminal_resolved(views[i].pid, &t);
    }
  }
}
static bool request_wezterm_current(bool typing) {
  uint64_t focused = focus_watch_focused_id();
  if (!focus_watch_available() || !focused)
    return false;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
  size_t n = focus_watch_windows(windows, FOCUS_WATCH_WINDOW_MAX);
  pid_t pid = 0;
  int matches = 0;
  for (int i = 0; i < count; i++) {
    if (views[i].parent || views[i].terminal.kind != TERMINAL_WEZTERM)
      continue;
    uint64_t window = 0;
    for (size_t w = 0; w < n; w++) {
      if (windows[w].id != focused ||
          !focus_find_window(views[i].pid, &windows[w], 1, &window))
        continue;
      if (!pid || views[i].terminal.window == focused)
        pid = views[i].pid;
      if (!views[i].terminal.window || views[i].terminal.window == focused)
        matches++;
      break;
    }
  }
  if (pid > 1 && (!typing || matches > 1)) {
    focus_wezterm_current(pid, focused);
    return true;
  }
  return false;
}
static void try_deferred_wezterm_focus(void) {
  // A focus event can arrive before the first agent identifies its terminal.
  // Finish that event's request once its socket metadata becomes available.
  if (pending_wezterm_focus &&
      pending_wezterm_focus == focus_watch_focused_id() &&
      request_wezterm_current(false))
    pending_wezterm_focus = 0;
}

static void capture_kitty(uint64_t key, pid_t pid) {
  uint64_t window = 0;
  char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
  pid_t kitty_pid = 0;
  if (agent_terminal_lookup("/proc", pid, &window, listen, sizeof(listen),
                            &kitty_pid))
    agent_sessions_set_kitty(key, kitty_pid, window, listen);
  agent_terminal_t terminal;
  if (agent_terminal_lookup_all("/proc", pid, &terminal)) {
    agent_sessions_set_terminal(key, &terminal);
    focus_terminal_resolve(pid);
    if (terminal.kind == TERMINAL_WEZTERM)
      try_deferred_wezterm_focus();
  }
}

static int agent_apply(uint64_t key, const char *agent, agent_event_t event,
                       pid_t pid, pid_t candidate, bool metadata, pid_t owner) {
  pid_t before[AGENT_SESSIONS_MAX], after[AGENT_SESSIONS_MAX];
  int before_count = agent_sessions_pids(before, AGENT_SESSIONS_MAX);
  pid_t previous = agent_sessions_pid(key);
  if (agent_sessions_apply_owned(key, agent, event, pid, candidate, metadata,
                                 owner, "/proc", monotonic_ms(),
                                 config.agent_done_timeout) < 0) {
    return 1;
  }
  if (event == AGENT_EVENT_DONE || event == AGENT_EVENT_REST ||
      event == AGENT_EVENT_START)
    agent_sessions_refresh_title(key);
  if (event == AGENT_EVENT_WORKING)
    overlay_signs_note_working(key);
  int after_count = agent_sessions_pids(after, AGENT_SESSIONS_MAX);
  for (int i = 0; i < before_count; i++) {
    if (!has_pid(after, after_count, before[i])) {
      agent_watch_remove(before[i]);
    }
  }
  pid_t current = agent_sessions_pid(key);
  if (current > 0 && current != previous)
    capture_kitty(key, current);
  if (current > 0) {
    int watched = agent_watch_add(current);
    agent_sessions_set_watched(key, watched == 0);
    if (watched < 0 && errno == ESRCH)
      agent_sessions_remove_pid(current);
  }
  agent_refresh();
  return 0;
}

static int agent_command(const char *request) {
  char agent[AGENT_NAME_MAX + 1];
  agent_event_t event;
  uint64_t key;
  pid_t pid, candidate, owner;
  bool metadata;
  if (!agent_event_owner_request(request, &key, agent, &event, &pid, &candidate,
                                 &metadata, &owner))
    return 1;
  return agent_apply(key, agent, event, pid, candidate, metadata, owner);
}

// Keep compositor IDs out of the persisted session model. These diagnostics
// reflect the experimental compositor's live map, including removal and opt-in
// gating.
static void window_session_diagnostics(char *response, size_t capacity) {
  const compositor_ops_t *ops = compositor_selected();
  if ((ops != &COMPOSITOR_SWAY && ops != &COMPOSITOR_HYPRLAND) || !capacity) {
    return;
  }
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
  size_t n = focus_watch_windows(windows, FOCUS_WATCH_WINDOW_MAX);
  uint64_t keys[AGENT_SESSIONS_MAX];
  int seen =
      focus_watch_focused_keys(views, (size_t)count, keys, AGENT_SESSIONS_MAX);
  size_t used = strlen(response);
  for (int i = 0; i < count && used < capacity - 1; i++) {
    uint64_t window = 0;
    if (!views[i].parent && focus_watch_available()) {
      focus_terminal_window(views[i].pid, &views[i].terminal, views[i].name,
                            windows, n, &window);
    }
    bool watching = false;
    for (int j = 0; j < seen; j++) {
      watching = watching || keys[j] == views[i].key;
    }
    int written = snprintf(
        response + used, capacity - used,
        "window-session %08" PRIx32 " window=%" PRIu64 " seen=%s\n",
        (uint32_t)(views[i].key >> 32U), window, watching ? "yes" : "no");
    if (written < 0 || (size_t)written >= capacity - used) {
      break;
    }
    used += (size_t)written;
  }
}

static int focus_command(const char *key) {
  size_t length = strlen(key);
  if ((length != 8 && length != 16) ||
      strspn(key, "0123456789abcdefABCDEF") != length)
    return 1;
  uint64_t requested = strtoull(key, NULL, 16);
  agent_session_view_t sessions[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(sessions, AGENT_SESSIONS_MAX);
  pid_t pid = 0;
  bool found = false;
  for (int i = 0; i < count; i++) {
    uint64_t candidate = length == 8 ? sessions[i].key >> 32 : sessions[i].key;
    if (candidate != requested)
      continue;
    if (found)
      return 1;
    found = true;
    pid = sessions[i].pid;
  }
  return !found || focus_session_window(pid) < 0;
}

static int status_response(char *response, size_t capacity) {
  const compositor_ops_t *backend = compositor_selected();
  const char *input = input_status_name(
      input_child_is_alive(), input_device_count(), input_denied_count());
  int length = snprintf(
      response, capacity,
      "running pid=%ld hidden=%s paused=%s input=%s input-helper=%s "
      "devices=%u denied=%u "
      "config=%s agent=%s sessions=%d compositor=%s focus-watch=%s\n%s",
      (long)getpid(), (int)hidden ? "yes" : "no", (int)paused ? "yes" : "no",
      input, input_mode_name(), input_device_count(), input_denied_count(),
      config_path, agent_state_name(animation_get_agent_state()),
      agent_sessions_count(), backend ? backend->name : "unavailable",
      focus_watch_available() ? "ready" : "unavailable", input_mode_hint());
  if (strcmp(input, "denied") == 0 && length > 0 && (size_t)length < capacity) {
    snprintf(response + length, capacity - (size_t)length,
             "\nNo keyboard readable: %s", input_access_hint());
  }
  return 0;
}

static void reconcile_metadata(const char *request) {
  const char *space = strchr(request, ' ');
  uint64_t key = strtoull(space + 1, NULL, 16);
  pid_t before[AGENT_SESSIONS_MAX], after[AGENT_SESSIONS_MAX];
  int n = agent_sessions_pids(before, AGENT_SESSIONS_MAX);
  agent_sessions_process(key, 0, true, "/proc");
  int m = agent_sessions_pids(after, AGENT_SESSIONS_MAX);
  for (int i = 0; i < n; i++)
    if (!has_pid(after, m, before[i]))
      agent_watch_remove(before[i]);
  pid_t pid = agent_sessions_pid(key);
  if (pid > 0) {
    int watched = agent_watch_add(pid);
    agent_sessions_set_watched(key, watched == 0);
    if (watched < 0 && errno == ESRCH)
      agent_sessions_remove_pid(pid);
  }
  agent_refresh();
}

static int terminal_command(const char *request) {
  int result = 0;
  uint64_t key;
  agent_terminal_t terminal, previous;
  if (!agent_terminal_request(request, &key, &terminal) ||
      agent_sessions_terminal_pid(key) <= 1) {
    result = 1;
  } else {
    pid_t pid = agent_sessions_terminal_pid(key);
    bool changed = !agent_sessions_terminal(pid, &previous, NULL, 0) ||
                   previous.kind != terminal.kind ||
                   previous.pane != terminal.pane ||
                   strcmp(previous.socket, terminal.socket);
    agent_sessions_set_terminal(key, &terminal);
    if (changed)
      focus_terminal_resolve(pid);
    if (terminal.kind == TERMINAL_WEZTERM)
      try_deferred_wezterm_focus();
  }
  return result;
}

static int state_command(const char *request) {
  int result = 0;
  agent_state_t state;
  if (agent_state_parse(request + 6, &state) == 0) {
    agent_event_t event = AGENT_EVENT_FAIL;
    agent_event_parse(request + 6, &event);
    // An error only replaces a turn in progress, so start one first.
    if (state == AGENT_STATE_ERROR)
      agent_apply(0, "manual", AGENT_EVENT_WORKING, 0, 0, false, 0);
    result = agent_apply(0, "manual",
                         state == AGENT_STATE_IDLE ? AGENT_EVENT_END : event, 0,
                         0, false, 0);
  } else {
    result = 1;
  }
  return result;
}

static int pane_command(const char *request) {
  int result = 0;
  agent_terminal_t report;
  if (agent_terminal_pane_request(request, &report)) {
    result = !focus_tmux_pane_set_socket(report.client_pid, report.pane,
                                         report.socket);
    if (!result)
      agent_sessions_tmux_attached(report.socket);
  } else {
    pid_t pane_pid = 0;
    uint64_t pane_split = 0;
    // Keep kitty's original protocol and validation unchanged.
    result = !focus_pane_parse(request, &pane_pid, &pane_split) ||
             !focus_pane_set(pane_pid, pane_split);
  }
  if (!result)
    note_window_focus();
  return result;
}

int command(const char *request, char *response, size_t capacity) {
  int result = 0;
  if (strcmp(request, "stop") == 0) {
    {
      running = 0;
    }
  } else if (strcmp(request, "hide") == 0) {
    hidden = true;
    wayland_set_hidden(true);
  } else if (strcmp(request, "show") == 0) {
    hidden = false;
    wayland_set_hidden(false);
  } else if (strcmp(request, "pause") == 0 || strcmp(request, "resume") == 0) {
    paused = strcmp(request, "pause") == 0;
    input_process_events();
    if (pending_paws) {
      atomic_store(pending_paws, 0);
    }
    animation_set_paused(paused);
    wayland_request_redraw();
  } else if (strcmp(request, "reset-position") == 0) {
    result = wayland_reset_position();
  } else if (strcmp(request, "reload") == 0) {
    { result = reload(); }
  } else if (strncmp(request, "state ", 6) == 0) {
    result = state_command(request);
  } else if (strncmp(request, "ev ", 3) == 0) {
    result = agent_command(request);
  } else if (strncmp(request, "term ", 5) == 0) {
    result = terminal_command(request);
  } else if (strncmp(request, "tmux ", 5) == 0) {
    agent_terminal_t terminal;
    if (!agent_terminal_tmux_request(request, &terminal)) {
      result = 1;
    } else {
      pid_t pids[AGENT_SESSIONS_MAX];
      int count =
          agent_sessions_tmux_pids(terminal.socket, pids, AGENT_SESSIONS_MAX);
      for (int i = 0; i < count; i++)
        focus_terminal_resolve(pids[i]);
    }
  } else if (!strncmp(request, "ask ", 4) || !strncmp(request, "ttl ", 4)) {
    result = agent_sessions_title_command(request);
  } else if (strncmp(request, "sid ", 4) == 0) {
    result = agent_sessions_id_command(request);
  } else if (strncmp(request, "path ", 5) == 0) {
    result = transcript_watch_command(request, monotonic_ms());
  } else if (strncmp(request, "cwd ", 4) == 0) {
    result = agent_sessions_cwd_command(request);
  } else if (strncmp(request, "name ", 5) == 0) {
    char key[17];
    int end = 0;
    if (strlen(request) > 63 ||
        sscanf(request + 5, "%16[0-9a-fA-F]%n", key, &end) != 1 || end != 16 ||
        request[21] != ' ' || !strtoull(key, NULL, 16))
      result = 1;
    else
      result =
          agent_sessions_set_name(strtoull(key, NULL, 16), request + 22) < 0;
  } else if (strncmp(request, "focus ", 6) == 0) {
    result = focus_command(request + 6);
  } else if (strncmp(request, "pane ", 5) == 0) {
    result = pane_command(request);
  } else if (strcmp(request, "sessions") == 0) {
    if (agent_sessions_format(response, capacity, monotonic_ms()) == 0) {
      snprintf(response, capacity, "No agent sessions");
    }
    window_session_diagnostics(response, capacity);
    return 0;
  } else if (strcmp(request, "status") == 0) {
    return status_response(response, capacity);
  } else {
    { result = 1; }
  }
  // Metadata handoffs retry an unmerged candidate after a late parent arrives.
  if (!result &&
      (!strncmp(request, "sid ", 4) || !strncmp(request, "cwd ", 4) ||
       !strncmp(request, "name ", 5) || !strncmp(request, "path ", 5) ||
       !strncmp(request, "ttl ", 4) || !strncmp(request, "ask ", 4))) {
    reconcile_metadata(request);
  }
  snprintf(response, capacity, "%s", result ? "request failed" : "ok");
  return result;
}
// How long a typed answer is believed without a hook event to confirm it.
#define ANSWER_REVERT_S 60
// Keys pressed right after focus arrives are the ones that moved it there.
#define ANSWER_DWELL_MS 600
static uint64_t focus_window_seen;
static int64_t focus_window_since;
static uint64_t pending_key_window;
static int64_t pending_key_ms;
static void note_key_feedback(void) {
  int64_t now = monotonic_ms();
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t keys[2];
  // Only when the keys can be meant for no other session.
  if (focus_watch_focused_id() == focus_window_seen &&
      now - focus_window_since >= ANSWER_DWELL_MS &&
      focus_watch_focused_keys(views, (size_t)count, keys, 2) == 1 &&
      agent_sessions_answer(keys[0], now, ANSWER_REVERT_S)) {
    overlay_signs_note_working(keys[0]);
    wayland_request_redraw();
    return;
  }
  overlay_signs_note_key();
}
void note_key(void) {
  request_wezterm_current(true);
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t focused = focus_watch_focused_id();
  focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
  size_t n = focus_watch_windows(windows, FOCUS_WATCH_WINDOW_MAX);
  for (int i = 0; i < count; i++) {
    uint64_t window = 0;
    if (!views[i].parent && views[i].terminal.kind == TERMINAL_WEZTERM &&
        !views[i].terminal.current_known &&
        focus_terminal_window(views[i].pid, &views[i].terminal, views[i].name,
                              windows, n, &window) &&
        window == focused) {
      pending_key_window = focused;
      pending_key_ms = monotonic_ms();
      return;
    }
  }
  note_key_feedback();
}
void note_window_focus(void) {
  if (focus_watch_focused_id() != focus_window_seen) {
    focus_window_seen = focus_watch_focused_id();
    focus_window_since = monotonic_ms();
    pending_wezterm_focus = focus_window_seen;
    try_deferred_wezterm_focus();
  }
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t keys[AGENT_SESSIONS_MAX];
  uint64_t newest = 0;
  int focused = focus_watch_query(views, (size_t)count, keys,
                                  AGENT_SESSIONS_MAX, &newest);
  bool watching = focus_watch_available();
  agent_sessions_observe_focus(watching, keys, (size_t)focused);
  overlay_signs_sync_focus(newest);
  int64_t now = monotonic_ms();
  if (pending_key_window && (pending_key_window != focus_watch_focused_id() ||
                             now - pending_key_ms > 2500))
    pending_key_window = 0;
  if (newest && pending_key_window == focus_watch_focused_id()) {
    pending_key_window = 0;
    note_key_feedback();
  }
  for (int i = 0; i < focused; i++) {
    agent_sessions_note_focused(keys[i], now, config.agent_done_timeout);
  }
}
void resolve_restored_terminals(void) {
  agent_session_view_t restored[AGENT_SESSIONS_MAX];
  int restored_count = agent_sessions_snapshot(restored, AGENT_SESSIONS_MAX);
  for (int i = 0; i < restored_count; i++)
    if (restored[i].terminal.kind == TERMINAL_TMUX ||
        restored[i].terminal.kind == TERMINAL_WEZTERM)
      focus_terminal_resolve(restored[i].pid);
}

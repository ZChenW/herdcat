#include "platform/focus_current.h"

#include "platform/focus_watch.h"

#include <string.h>

enum {
  PID_MAX = 4194304
};

typedef struct {
  bool used;
  pid_t pid;
  uint64_t split;
  uint64_t seq;
} pane_slot_t;

static pane_slot_t pane_slots[FOCUS_PANE_MAX];
static uint64_t pane_seq;
typedef struct {
  pane_slot_t report;
  char socket[AGENT_TERMINAL_LISTEN_MAX + 1];
  pid_t kitty_pid;
  uint64_t kitty_pane;
} tmux_slot_t;
static tmux_slot_t tmux_slots[FOCUS_PANE_MAX];
static uint64_t clicked_window;
static uint64_t clicked_key;

static bool parse_token(const char *text, const char **end, uint64_t *out) {
  if (!text || text[0] < '1' || text[0] > '9')
    return false;
  uint64_t value = 0;
  size_t n = 0;
  while (text[n] >= '0' && text[n] <= '9') {
    if (n >= 20)
      return false;
    uint64_t digit = (uint64_t)(text[n] - '0');
    if (value > (UINT64_MAX - digit) / 10)
      return false;
    value = value * 10 + digit;
    n++;
  }
  if (!n)
    return false;
  *out = value;
  if (end)
    *end = text + n;
  return true;
}

static bool accept_ids(uint64_t pid_value, uint64_t split, pid_t *pid,
                       uint64_t *out_split) {
  if (!pid || !out_split || pid_value <= 1 || pid_value > PID_MAX || !split)
    return false;
  *pid = (pid_t)pid_value;
  *out_split = split;
  return true;
}

bool focus_pane_fields(const char *pid_text, const char *split_text, pid_t *pid,
                       uint64_t *split) {
  const char *pid_end = NULL;
  const char *split_end = NULL;
  uint64_t pid_value = 0;
  uint64_t split_value = 0;
  if (!parse_token(pid_text, &pid_end, &pid_value) || !pid_end || *pid_end ||
      !parse_token(split_text, &split_end, &split_value) || !split_end ||
      *split_end)
    return false;
  return accept_ids(pid_value, split_value, pid, split);
}

bool focus_pane_parse(const char *request, pid_t *pid, uint64_t *split) {
  if (!request || strncmp(request, "pane ", 5))
    return false;
  const char *pid_end = NULL;
  uint64_t pid_value = 0;
  if (!parse_token(request + 5, &pid_end, &pid_value) || !pid_end ||
      *pid_end != ' ')
    return false;
  const char *split_end = NULL;
  uint64_t split_value = 0;
  if (!parse_token(pid_end + 1, &split_end, &split_value) || !split_end ||
      *split_end)
    return false;
  return accept_ids(pid_value, split_value, pid, split);
}

bool focus_tmux_pane_fields(const char *pid_text, const char *split_text,
                            pid_t *pid, uint64_t *split) {
  uint64_t process;
  const char *end;
  if (!pid || !split || !parse_token(pid_text, &end, &process) || *end ||
      process <= 1 || process > PID_MAX || !split_text ||
      (split_text[0] == '0' && split_text[1]) ||
      !agent_terminal_number(split_text, false, split))
    return false;
  *pid = (pid_t)process;
  return true;
}
bool focus_tmux_pane_set_socket(pid_t pid, uint64_t split, const char *socket) {
  if (pid <= 1 || pid > PID_MAX ||
      (socket && strlen(socket) > AGENT_TERMINAL_LISTEN_MAX))
    return false;
  size_t slot = 0;
  for (size_t i = 0; i < FOCUS_PANE_MAX; i++) {
    if (!tmux_slots[i].report.used || tmux_slots[i].report.pid == pid) {
      slot = i;
      break;
    }
    if (tmux_slots[i].report.seq < tmux_slots[slot].report.seq)
      slot = i;
  }
  tmux_slot_t *t = &tmux_slots[slot];
  *t = (tmux_slot_t){
      .report = {true, pid, split, ++pane_seq}
  };
  if (socket) {
    memcpy(t->socket, socket, strlen(socket) + 1);
    char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
    agent_terminal_lookup("/proc", pid, &t->kitty_pane, listen, sizeof(listen),
                          &t->kitty_pid);
  }
  return true;
}
bool focus_tmux_pane_set(pid_t pid, uint64_t split) {
  return focus_tmux_pane_set_socket(pid, split, NULL);
}

void focus_pane_reset(void) {
  memset(pane_slots, 0, sizeof(pane_slots));
  memset(tmux_slots, 0, sizeof(tmux_slots));
  pane_seq = 0;
}

bool focus_pane_set(pid_t pid, uint64_t split) {
  if (pid <= 1 || pid > PID_MAX || !split)
    return false;
  int empty = -1;
  int oldest = 0;
  for (int i = 0; i < FOCUS_PANE_MAX; i++) {
    if (pane_slots[i].used && pane_slots[i].pid == pid) {
      pane_slots[i].split = split;
      pane_slots[i].seq = ++pane_seq;
      return true;
    }
    if (!pane_slots[i].used && empty < 0)
      empty = i;
    if (pane_slots[i].used && pane_slots[i].seq < pane_slots[oldest].seq)
      oldest = i;
  }
  int slot = empty >= 0 ? empty : oldest;
  pane_slots[slot] = (pane_slot_t){
      .used = true, .pid = pid, .split = split, .seq = ++pane_seq};
  return true;
}

bool focus_pane_get(pid_t pid, uint64_t *split) {
  if (pid <= 1 || !split)
    return false;
  for (int i = 0; i < FOCUS_PANE_MAX; i++) {
    if (!pane_slots[i].used || pane_slots[i].pid != pid)
      continue;
    *split = pane_slots[i].split;
    return true;
  }
  return false;
}

size_t focus_pane_copy(focus_pane_t *out, size_t capacity) {
  if (!out || !capacity)
    return 0;
  size_t written = 0;
  for (int i = 0; i < FOCUS_PANE_MAX && written < capacity; i++) {
    if (!pane_slots[i].used)
      continue;
    out[written++] =
        (focus_pane_t){.pid = pane_slots[i].pid, .split = pane_slots[i].split};
  }
  return written;
}

void focus_current_reset(void) {
  clicked_window = 0;
  clicked_key = 0;
}

void focus_current_click(uint64_t window, uint64_t key) {
  if (!window || !key)
    return;
  clicked_window = window;
  clicked_key = key;
}

void focus_current_observe(uint64_t window) {
  if (clicked_window && window != clicked_window) {
    clicked_window = 0;
    clicked_key = 0;
  }
}

void focus_current_clicked(uint64_t *window, uint64_t *key) {
  if (window)
    *window = clicked_window;
  if (key)
    *key = clicked_key;
}

static bool reported_split(const focus_pane_t *panes, size_t pane_count,
                           pid_t pid, uint64_t *split) {
  if (!panes || pid <= 1)
    return false;
  for (size_t i = 0; i < pane_count; i++) {
    if (panes[i].pid == pid && panes[i].split) {
      *split = panes[i].split;
      return true;
    }
  }
  return false;
}

uint64_t focus_current_choose(uint64_t focused, const focus_window_t *windows,
                              size_t windows_count,
                              const agent_session_view_t *sessions,
                              size_t count, const focus_pane_t *panes,
                              size_t pane_count, uint64_t clicked_id,
                              uint64_t clicked) {
  uint64_t chosen = 0;
  focus_current_query(focused, windows, windows_count, sessions, count, panes,
                      pane_count, clicked_id, clicked, NULL, 0, &chosen);
  return chosen;
}
int focus_current_seen(uint64_t focused, const focus_window_t *windows,
                       size_t windows_count,
                       const agent_session_view_t *sessions, size_t count,
                       const focus_pane_t *panes, size_t pane_count,
                       uint64_t *keys, size_t capacity) {
  if (!keys || !capacity)
    return 0;
  uint64_t chosen = 0;
  return focus_current_query(focused, windows, windows_count, sessions, count,
                             panes, pane_count, 0, 0, keys, capacity, &chosen);
}

int focus_current_query(uint64_t focused, const focus_window_t *windows,
                        size_t windows_count,
                        const agent_session_view_t *sessions, size_t count,
                        const focus_pane_t *panes, size_t pane_count,
                        uint64_t clicked_id, uint64_t clicked, uint64_t *keys,
                        size_t capacity, uint64_t *chosen) {
  if (!chosen)
    return 0;
  *chosen = 0;
  if (!focused || !windows || !sessions || count > AGENT_SESSIONS_MAX)
    return 0;
  // Client ancestry is shared within this query only. Returning to an older
  // attached client must not require another CLI command or hook report.
  uint64_t client_windows[FOCUS_PANE_MAX] = {0};
  bool has_tmux = false;
  for (size_t i = 0; i < count; i++)
    if (sessions[i].terminal.kind == TERMINAL_TMUX)
      has_tmux = true;
  if (has_tmux) {
    for (size_t i = 0; i < FOCUS_PANE_MAX; i++)
      if (tmux_slots[i].report.used)
        focus_find_window(tmux_slots[i].report.pid, windows, windows_count,
                          &client_windows[i]);
  }
  bool belongs[AGENT_SESSIONS_MAX] = {0};
  bool reported[AGENT_SESSIONS_MAX] = {0};
  bool matches[AGENT_SESSIONS_MAX] = {0};
  bool split_found = false;
  for (size_t i = 0; i < count; i++) {
    const agent_session_view_t *s = &sessions[i];
    if (s->parent)
      continue;
    const agent_terminal_t *t = &s->terminal;
    if (t->kind == TERMINAL_TMUX) {
      const tmux_slot_t *best = NULL;
      int best_rank = -1;
      for (size_t k = 0; k < FOCUS_PANE_MAX; k++) {
        const tmux_slot_t *slot = &tmux_slots[k];
        if (!slot->report.used || client_windows[k] != focused ||
            (slot->socket[0] ? strcmp(slot->socket, t->socket) != 0
                             : slot->report.pid != t->client_pid))
          continue;
        uint64_t outer = 0;
        pid_t kitty_pid =
            slot->socket[0] ? slot->kitty_pid : t->outer_kitty_pid;
        uint64_t kitty_pane =
            slot->socket[0] ? slot->kitty_pane : t->outer_kitty_pane;
        bool known = reported_split(panes, pane_count, kitty_pid, &outer);
        int rank = known ? (outer == kitty_pane ? 2 : 0) : 1;
        if (!best || rank > best_rank ||
            (rank == best_rank && slot->report.seq > best->report.seq)) {
          best = slot;
          best_rank = rank;
        }
      }
      if (best) {
        belongs[i] = true;
        reported[i] = true;
        matches[i] = best_rank > 0 && best->report.split == t->pane;
      } else {
        uint64_t window = 0, outer = 0;
        belongs[i] =
            focus_terminal_window_title(s->pid, t, s->name, s->title, windows,
                                        windows_count, &window) &&
            window == focused;
        reported[i] =
            reported_split(panes, pane_count, t->outer_kitty_pid, &outer);
      }
    } else {
      uint64_t window = 0, split = 0;
      belongs[i] =
          s->pid > 1 &&
          focus_terminal_window_title(s->pid, t, s->name, s->title, windows,
                                      windows_count, &window) &&
          window == focused;
      if (t->kind == TERMINAL_WEZTERM) {
        reported[i] = true;
        matches[i] = t->current_known && t->current_pane == t->pane;
      } else {
        reported[i] = reported_split(panes, pane_count, s->kitty_pid, &split);
        matches[i] = reported[i] && s->kitty_window == split;
      }
    }
    if (belongs[i] && reported[i])
      split_found = true;
  }
  size_t written = 0;
  int64_t updated = -1;
  bool clicked_found = false;
  for (size_t i = 0; i < count; i++) {
    if (!belongs[i] || (split_found && (!reported[i] || !matches[i])))
      continue;
    if (keys && written < capacity)
      keys[written++] = sessions[i].key;
    if (sessions[i].updated_ms >= updated) {
      *chosen = sessions[i].key;
      updated = sessions[i].updated_ms;
    }
    if (sessions[i].key == clicked)
      clicked_found = true;
  }
  if (!split_found && clicked && clicked_found && clicked_id == focused)
    *chosen = clicked;
  return (int)written;
}

bool focus_current_wezterm_pane(uint64_t window, const char *socket,
                                const agent_session_view_t *sessions,
                                size_t count, const focus_wezterm_pane_t *panes,
                                size_t pane_count, uint64_t *pane) {
  if (!socket || !pane || !panes || !pane_count || (!sessions && count))
    return false;
  uint64_t selected = panes[0].pane;
  bool conflicting = false;
  for (size_t i = 1; i < pane_count; i++)
    if (panes[i].pane != selected)
      conflicting = true;
  if (!conflicting) {
    *pane = selected;
    return true;
  }
  bool known = false;
  for (size_t i = 0; i < pane_count; i++) {
    for (size_t k = 0; k < count; k++) {
      const agent_terminal_t *t = &sessions[k].terminal;
      if (t->kind != TERMINAL_WEZTERM || t->window != window ||
          strcmp(t->socket, socket) != 0 || t->pane != panes[i].pane)
        continue;
      if (known && selected != panes[i].pane)
        return false;
      known = true;
      selected = panes[i].pane;
    }
  }
  if (known)
    *pane = selected;
  return known;
}

void focus_current_wezterm_resolve(pid_t pid, const agent_terminal_t *terminal,
                                   const focus_window_t *windows,
                                   size_t windows_count,
                                   agent_session_view_t *sessions,
                                   size_t count) {
  if (!terminal || terminal->kind != TERMINAL_WEZTERM || !sessions)
    return;
  bool group_known = false;
  uint64_t group = 0;
  if (terminal->current_known) {
    for (size_t i = 0; i < count; i++) {
      const agent_terminal_t *t = &sessions[i].terminal;
      if (t->kind == TERMINAL_WEZTERM && !strcmp(t->socket, terminal->socket) &&
          t->pane == terminal->current_pane && t->native_window_known) {
        group = t->native_window;
        group_known = true;
        break;
      }
    }
  }
  for (size_t i = 0; i < count; i++) {
    agent_terminal_t t = sessions[i].terminal;
    if (t.kind != TERMINAL_WEZTERM || strcmp(t.socket, terminal->socket) != 0)
      continue;
    bool target = sessions[i].pid == pid && t.pane == terminal->pane;
    if (target)
      t = *terminal;
    if (!terminal->window) {
      if (target && t.native_window_known) {
        // A newly registered pane can reuse a prior observation of its mux
        // group without issuing another current-pane query.
        for (size_t k = 0; k < count; k++) {
          const agent_terminal_t *peer = &sessions[k].terminal;
          if (k != i && peer->kind == TERMINAL_WEZTERM && peer->window &&
              peer->native_window_known &&
              peer->native_window == t.native_window &&
              !strcmp(peer->socket, t.socket)) {
            t.window = peer->window;
            t.current_known = peer->current_known;
            t.current_pane = peer->current_pane;
            break;
          }
        }
      }
      if (target)
        sessions[i].terminal = t;
      continue;
    }
    uint64_t window = 0;
    bool belongs = group_known
                       ? t.native_window_known && t.native_window == group
                       : target || (focus_terminal_window_title(
                                        sessions[i].pid, &t, sessions[i].name,
                                        sessions[i].title, windows,
                                        windows_count, &window) &&
                                    window == terminal->window);
    if (belongs) {
      t.window = terminal->window;
      t.current_known = terminal->current_known;
      t.current_pane = terminal->current_pane;
      sessions[i].terminal = t;
    }
  }
}

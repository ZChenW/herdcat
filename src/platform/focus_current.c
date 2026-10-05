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

void focus_pane_reset(void) {
  memset(pane_slots, 0, sizeof(pane_slots));
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

static bool in_window(const agent_session_view_t *session, uint64_t focused,
                      const focus_window_t *windows, size_t windows_count) {
  uint64_t id = 0;
  return session->pid > 1 &&
         focus_find_window(session->pid, windows, windows_count, &id) &&
         id == focused;
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

static bool split_known(uint64_t focused, const focus_window_t *windows,
                        size_t windows_count,
                        const agent_session_view_t *sessions, size_t count,
                        const focus_pane_t *panes, size_t pane_count) {
  for (size_t i = 0; i < count; i++) {
    uint64_t split = 0;
    if (in_window(&sessions[i], focused, windows, windows_count) &&
        reported_split(panes, pane_count, sessions[i].kitty_pid, &split))
      return true;
  }
  return false;
}

uint64_t focus_current_choose(uint64_t focused, const focus_window_t *windows,
                              size_t windows_count,
                              const agent_session_view_t *sessions,
                              size_t count, const focus_pane_t *panes,
                              size_t pane_count, uint64_t clicked_id,
                              uint64_t clicked) {
  if (!focused || !windows || !sessions)
    return 0;
  if (split_known(focused, windows, windows_count, sessions, count, panes,
                  pane_count)) {
    uint64_t best = 0;
    int64_t updated = -1;
    for (size_t i = 0; i < count; i++) {
      uint64_t split = 0;
      if (!in_window(&sessions[i], focused, windows, windows_count) ||
          !reported_split(panes, pane_count, sessions[i].kitty_pid, &split) ||
          sessions[i].kitty_window != split)
        continue;
      if (sessions[i].updated_ms >= updated) {
        best = sessions[i].key;
        updated = sessions[i].updated_ms;
      }
    }
    return best;
  }
  if (clicked && clicked_id == focused) {
    for (size_t i = 0; i < count; i++) {
      if (sessions[i].key == clicked &&
          in_window(&sessions[i], focused, windows, windows_count))
        return clicked;
    }
  }
  return focus_watch_match(focused, windows, windows_count, sessions, count);
}

int focus_current_seen(uint64_t focused, const focus_window_t *windows,
                       size_t windows_count,
                       const agent_session_view_t *sessions, size_t count,
                       const focus_pane_t *panes, size_t pane_count,
                       uint64_t *keys, size_t capacity) {
  if (!focused || !windows || !sessions || !keys || !capacity)
    return 0;
  if (!split_known(focused, windows, windows_count, sessions, count, panes,
                   pane_count))
    return focus_watch_matching(focused, windows, windows_count, sessions,
                                count, keys, capacity);
  size_t written = 0;
  for (size_t i = 0; i < count && written < capacity; i++) {
    uint64_t split = 0;
    if (!in_window(&sessions[i], focused, windows, windows_count) ||
        !reported_split(panes, pane_count, sessions[i].kitty_pid, &split) ||
        sessions[i].kitty_window != split)
      continue;
    keys[written++] = sessions[i].key;
  }
  return (int)written;
}

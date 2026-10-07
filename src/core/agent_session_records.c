#include "agent_sessions_internal.h"
#include "core/agent_sign_state.h"
#include "utils/path_wire.h"

#include <stdio.h>
#include <stdlib.h>

int agent_sessions_format(char *buffer, size_t capacity, int64_t now_ms) {
  if (!capacity) {
    return 0;
  }
  if (!buffer) {
    return -1;
  }
  size_t used = 0;
  buffer[0] = '\0';
  agent_session_view_t view[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(view, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count && used < capacity - 1; i++) {
    const agent_session_view_t *s = &view[i];
    int64_t age = now_ms > s->updated_ms ? (now_ms - s->updated_ms) / 1000 : 0;
    char pid[24] = "-", parent[24] = "";
    if (s->parent)
      snprintf(parent, sizeof(parent), " parent=%08" PRIx32,
               (uint32_t)(s->parent >> 32));
    if (s->pid > 0) {
      snprintf(pid, sizeof(pid), "%jd", (intmax_t)s->pid);
    }
    int n = snprintf(
        buffer + used, capacity - used,
        "%s %08" PRIx32 " %s %" PRId64 "s pid=%s %s%s%s%s%s\n", s->agent,
        (uint32_t)(s->key >> 32), agent_state_name(s->state), age, pid, s->name,
        s->unread ? " unread" : "",
        s->title[0] ? (s->title_temporary ? " title~=" : " title=") : "",
        s->title, parent);
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

int agent_sessions_set_name(uint64_t key, const char *name) {
  if (!utf8_label_valid(name, 40))
    return -1;
  agent_sessions_adopt(key);
  agent_session_t *s = find_session(key);
  if (s && strcmp(s->name, name)) {
    snprintf(s->name, sizeof(s->name), "%s", name);
    touch_sessions();
  }
  return 0;
}

int agent_sessions_set_cwd_name(uint64_t key, const char *cwd,
                                const char *name) {
  char normalized[AGENT_CWD_MAX + 1];
  if (!path_normalize(cwd, normalized) || !utf8_label_valid(name, 40))
    return -1;
  agent_sessions_adopt(key);
  agent_session_t *s = find_session(key);
  if (!s)
    return -1;
  if (!s->start_cwd[0]) {
    memcpy(s->start_cwd, normalized, strlen(normalized) + 1);
    touch_sessions();
    return agent_sessions_set_name(key, name);
  }
  size_t n = strlen(s->start_cwd);
  bool below = !strncmp(normalized, s->start_cwd, n) &&
               (n == 1 ? normalized[1] != 0
                       : normalized[n] == '/' && normalized[n + 1] != 0);
  return below ? agent_sessions_set_name(key, name) : 0;
}
int agent_sessions_cwd_command(const char *request) {
  char key[17], encoded[AGENT_CWD_MAX * 2 + 1], cwd[AGENT_CWD_MAX + 1];
  int end = 0;
  if (!request || strlen(request) > 575 ||
      sscanf(request, "cwd %16[0-9a-fA-F] %510[0-9a-fA-F]%n", key, encoded,
             &end) != 2 ||
      strlen(key) != 16 || !strtoull(key, NULL, 16) || request[20] != ' ' ||
      request[end] != ' ' || !path_unhex(encoded, cwd))
    return 1;
  return agent_sessions_set_cwd_name(strtoull(key, NULL, 16), cwd,
                                     request + end + 1) < 0;
}

static bool transcript_ok(const char *path) {
  return path && path[0] == '/' && !strchr(path, '\n') && !strchr(path, '\t') &&
         strlen(path) <= AGENT_TRANSCRIPT_PATH_MAX;
}

int agent_sessions_set_transcript(uint64_t key, const char *path) {
  agent_session_t *s = find_session(key);
  if (!s || !transcript_ok(path))
    return -1;
  if (!strcmp(s->transcript, path))
    return 0;
  memcpy(s->transcript, path, strlen(path) + 1);
  touch_sessions();
  return 0;
}

int agent_sessions_export(agent_session_record_t *out, size_t capacity) {
  if (!out || !capacity)
    return 0;
  const agent_session_t *sorted[AGENT_SESSIONS_MAX];
  size_t count = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (!sessions[i].used)
      continue;
    size_t j = count++;
    while (j && sorted[j - 1]->order > sessions[i].order) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = &sessions[i];
  }
  if (count > capacity)
    count = capacity;
  for (size_t i = 0; i < count; i++) {
    const agent_session_t *s = sorted[i];
    agent_session_record_t row = {.key = s->key,
                                  .order = s->order,
                                  .state = s->state,
                                  .pid = s->pid,
                                  .updated_ms = s->updated_ms,
                                  .unread = s->unread};
    memcpy(row.agent, s->agent, sizeof(row.agent));
    memcpy(row.name, s->name, sizeof(row.name));
    memcpy(row.title, s->title, sizeof(row.title));
    row.title_temporary = s->title_temporary;
    memcpy(row.session_id, s->session_id, sizeof(row.session_id));
    memcpy(row.transcript, s->transcript, sizeof(row.transcript));
    memcpy(row.start_cwd, s->start_cwd, sizeof(row.start_cwd));
    out[i] = row;
  }
  return (int)count;
}

int agent_sessions_restore(const agent_session_record_t *record, int64_t now_ms,
                           int done_timeout_s) {
  if (!record || !record->key || !valid_agent(record->agent) || now_ms < 0 ||
      done_timeout_s < 0 || record->updated_ms < 0 || record->pid < 0 ||
      record->state < AGENT_STATE_IDLE || record->state >= AGENT_STATE_COUNT ||
      find_session(record->key) || agent_sessions_count() >= AGENT_SESSIONS_MAX)
    return -1;
  agent_session_t *s = available_slot();
  agent_state_t state = record->state;
  bool unread = record->unread;
  if (state == AGENT_STATE_WORKING || state == AGENT_STATE_WAITING) {
    state = AGENT_STATE_IDLE;
    unread = false;
  }
  int64_t updated = record->updated_ms > now_ms ? now_ms : record->updated_ms;
  *s = (agent_session_t){.used = true,
                         .key = record->key,
                         .order = ++next_order,
                         .created_ms = updated,
                         .state_since_ms = updated,
                         .state = state,
                         .pid = record->pid,
                         .updated_ms = updated,
                         .unread = finished(state) && unread};
  memcpy(s->agent, record->agent, strlen(record->agent) + 1);
  if (utf8_label_valid(record->name, 40))
    snprintf(s->name, sizeof(s->name), "%s", record->name);
  else
    snprintf(s->name, sizeof(s->name), "%s %04" PRIx16, record->agent,
             (uint16_t)(record->key >> 48));
  if (utf8_label_valid(record->title, AGENT_TITLE_MAX)) {
    snprintf(s->title, sizeof(s->title), "%s", record->title);
    s->title_temporary = record->title_temporary;
  }
  if (agent_session_id_valid(record->session_id))
    snprintf(s->session_id, sizeof(s->session_id), "%s", record->session_id);
  if (transcript_ok(record->transcript))
    memcpy(s->transcript, record->transcript, strlen(record->transcript) + 1);
  if (record->start_cwd[0])
    path_normalize(record->start_cwd, s->start_cwd);
  if (finished(s->state) && !s->unread && done_timeout_s > 0)
    s->done_until_ms = deadline(now_ms, done_timeout_s);
  applied_done_timeout = done_timeout_s;
  if (s->pid > 0)
    drop_same_process(s);
  touch_sessions();
  return 0;
}
int agent_sessions_snapshot(agent_session_view_t *out, size_t capacity) {
  if (!out || !capacity)
    return 0;
  agent_session_view_t sorted[AGENT_SESSIONS_MAX];
  size_t count = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const agent_session_t *s = &sessions[i];
    if (!s->used)
      continue;
    agent_session_view_t v = {
        .key = s->key,
        .order = s->order,
        .parent = s->parent_key,
        .state = s->state,
        .pid = s->pid,
        .created_ms = s->created_ms,
        .state_since_ms = s->state_since_ms,
        .updated_ms = s->updated_ms,
        .unread = s->unread,
        .kitty_pid = s->terminals[0].socket[0] ? s->terminals[0].client_pid : 0,
        .kitty_window = s->terminals[0].socket[0] ? s->terminals[0].pane : 0};
    for (int j = 0; j < AGENT_SESSIONS_MAX; j++)
      if (s->parent_order && sessions[j].used &&
          sessions[j].order == s->parent_order)
        v.parent = sessions[j].key;
    v.terminal = s->terminals[1];
    if (!v.terminal.socket[0] && v.terminal.kind != TERMINAL_GHOSTTY)
      v.terminal.kind = TERMINAL_NONE;
    memcpy(v.agent, s->agent, sizeof(v.agent));
    memcpy(v.name, s->name, sizeof(v.name));
    memcpy(v.title, s->title, sizeof(v.title));
    v.title_temporary = s->title_temporary;
    memcpy(v.session_id, s->session_id, sizeof(v.session_id));
    size_t j = count++;
    while (j && sorted[j - 1].order > v.order) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = v;
  }
  for (size_t i = 0; i < count; i++) {
    agent_session_view_t *v = &sorted[i];
    if (v->parent)
      continue;
    char types[AGENT_SESSIONS_MAX][AGENT_NAME_MAX + 1] = {0};
    unsigned totals[AGENT_SESSIONS_MAX] = {0};
    size_t kinds = 0;
    for (size_t j = 0; j < count; j++) {
      const agent_session_view_t *child = &sorted[j];
      if (child->parent != v->key || !child->parent)
        continue;
      size_t type = 0;
      while (type < kinds && strcmp(types[type], child->agent))
        type++;
      if (type == kinds) {
        memcpy(types[kinds++], child->agent, sizeof(child->agent));
      }
      if (child->state == AGENT_STATE_WORKING ||
          child->state == AGENT_STATE_WAITING) {
        totals[type]++;
        if (!v->child_count || child->created_ms < v->child_started_ms)
          v->child_started_ms = child->created_ms;
        v->child_count++;
      }
    }
    unsigned active = 0;
    for (size_t j = 0; j < kinds; j++) {
      if (!totals[j])
        continue;
      if (active < 2) {
        memcpy(v->child_agents[active], types[j], sizeof(types[j]));
        v->child_counts[active] = totals[j];
      }
      active++;
    }
    if (active > 2)
      v->child_other = v->child_count - v->child_counts[0];
  }
  if (count > capacity)
    count = capacity;
  memcpy(out, sorted, count * sizeof(*out));
  return (int)count;
}
int agent_sessions_select(const agent_session_view_t *input, size_t count,
                          agent_session_view_t *out, size_t capacity) {
  if (!input || !out || count > AGENT_SESSIONS_MAX)
    return 0;
  bool selected[AGENT_SESSIONS_MAX] = {0};
  size_t n = count < capacity ? count : capacity;
  for (size_t i = 0; i < n; i++) {
    size_t best = count;
    for (size_t j = 0; j < count; j++) {
      if (selected[j] || input[j].parent)
        continue;
      bool active = agent_sign_state(&input[j]) != AGENT_STATE_IDLE;
      bool best_active =
          best < count && agent_sign_state(&input[best]) != AGENT_STATE_IDLE;
      if (best == count || (active && !best_active) ||
          (active == best_active &&
           input[j].updated_ms > input[best].updated_ms))
        best = j;
    }
    if (best == count)
      break;
    selected[best] = true;
  }
  size_t written = 0;
  for (size_t i = 0; i < count; i++)
    if (selected[i])
      out[written++] = input[i];
  return (int)written;
}

void agent_sessions_interrupt(uint64_t key, int64_t now_ms) {
  agent_session_t *s = find_session(key);
  if (s)
    agent_sessions_apply(key, s->agent, AGENT_EVENT_INTERRUPT, 0, now_ms,
                         applied_done_timeout, NULL);
}

void agent_sessions_working(uint64_t key, int64_t now_ms) {
  agent_session_t *s = find_session(key);
  if (s)
    agent_sessions_apply(key, s->agent, AGENT_EVENT_WORKING, 0, now_ms,
                         applied_done_timeout, NULL);
}

void agent_sessions_fail(uint64_t key, int64_t now_ms) {
  agent_session_t *s = find_session(key);
  if (s)
    agent_sessions_apply(key, s->agent, AGENT_EVENT_FAIL, 0, now_ms,
                         applied_done_timeout, NULL);
}

pid_t agent_sessions_terminal_pid(uint64_t key) {
  const agent_session_t *s = find_session(key);
  if (!s)
    s = find_alias(key);
  return s ? s->pid : 0;
}
void agent_sessions_set_terminal(uint64_t key, const agent_terminal_t *t) {
  agent_session_t *s = find_session(key);
  if (!s)
    s = find_alias(key);
  if (!s || s->pid <= 1 || !t || t->kind < TERMINAL_TMUX ||
      t->kind > TERMINAL_GHOSTTY)
    return;
  if (s->terminals[1].kind == t->kind && s->terminals[1].pane == t->pane &&
      !strcmp(s->terminals[1].socket, t->socket))
    return;
  s->terminals[1] = *t;
  s->terminals[1].detached = t->kind == TERMINAL_TMUX && t->detached;
}
bool agent_sessions_terminal(pid_t pid, agent_terminal_t *t, char *name,
                             size_t capacity) {
  if (!t || pid <= 1)
    return false;
  for (size_t i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const agent_session_t *s = &sessions[i];
    if (!s->used || s->pid != pid)
      continue;
    if (!s->terminals[1].socket[0] && s->terminals[1].kind != TERMINAL_GHOSTTY)
      continue;
    *t = s->terminals[1];
    if (name && capacity)
      snprintf(name, capacity, "%s", s->name);
    return true;
  }
  return false;
}
void agent_sessions_terminal_resolved(pid_t pid, const agent_terminal_t *t) {
  for (size_t i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_session_t *s = &sessions[i];
    if (s->used && s->pid == pid && s->terminals[1].kind == t->kind &&
        s->terminals[1].pane == t->pane &&
        !strcmp(s->terminals[1].socket, t->socket))
      s->terminals[1] = *t;
    if (s->used && s->pid == pid && s->terminals[1].kind != TERMINAL_TMUX)
      s->terminals[1].detached = false;
  }
}

void agent_sessions_tmux_attached(const char *socket) {
  if (!socket)
    return;
  for (size_t i = 0; i < AGENT_SESSIONS_MAX; i++) {
    agent_terminal_t *t = &sessions[i].terminals[1];
    if (sessions[i].used && t->kind == TERMINAL_TMUX &&
        !strcmp(t->socket, socket))
      t->detached = false;
  }
}
int agent_sessions_tmux_pids(const char *socket, pid_t *pids, size_t capacity) {
  if (!socket || !pids)
    return 0;
  size_t count = 0;
  for (size_t i = 0; i < AGENT_SESSIONS_MAX && count < capacity; i++) {
    const agent_session_t *s = &sessions[i];
    const agent_terminal_t *t = &s->terminals[1];
    if (!s->used || s->pid <= 1 || t->kind != TERMINAL_TMUX ||
        strcmp(t->socket, socket))
      continue;
    bool duplicate = false;
    for (size_t j = 0; j < count; j++)
      duplicate |= pids[j] == s->pid;
    if (!duplicate)
      pids[count++] = s->pid;
  }
  return (int)count;
}

bool agent_event_owner_request(const char *request, uint64_t *key,
                               char agent[9], agent_event_t *event, pid_t *pid,
                               pid_t *candidate, bool *metadata, pid_t *owner) {
  char event_name[10], key_text[17], pid_text[8], candidate_text[8];
  int agent_end = 0, event_end = 0, key_end = 0, end = 0;
  if (!request || !key || !agent || !event || !pid || !candidate || !metadata ||
      !owner ||
      sscanf(request, "ev %8[a-z]%n %9[a-z]%n %16[0-9a-fA-F]%n %7[0-9]%n",
             agent, &agent_end, event_name, &event_end, key_text, &key_end,
             pid_text, &end) != 4 ||
      request[agent_end] != ' ' || request[event_end] != ' ' ||
      request[key_end] != ' ' || strlen(key_text) != 16 ||
      agent_event_parse(event_name, event) < 0)
    return false;
  *owner = 0;
  unsigned long actual = 0;
  int flag = 0, extra_end = 0;
  if (request[end]) {
    if (request[end] != ' ' ||
        sscanf(request + end, " %7[0-9] %1[01]%n", candidate_text, event_name,
               &extra_end) != 2)
      return false;
    const char *rest = request + end + extra_end;
    if (*rest) {
      char owner_text[8];
      int used = 0;
      if (*rest != ' ' || sscanf(rest, " %7[0-9]%n", owner_text, &used) != 1 ||
          rest[used])
        return false;
      unsigned long value = strtoul(owner_text, NULL, 10);
      if (value <= 1 || value > 4194304)
        return false;
      *owner = (pid_t)value;
    }
    flag = event_name[0] == '1';
    actual = strtoul(candidate_text, NULL, 10);
    if (actual <= 1 || actual > 4194304UL)
      return false;
  }
  uint64_t parsed = strtoull(key_text, NULL, 16);
  unsigned long process = strtoul(pid_text, NULL, 10);
  if (!parsed || process > 4194304UL)
    return false;
  *key = parsed;
  *pid = (pid_t)process;
  *candidate = (pid_t)actual;
  *metadata = flag != 0;
  return true;
}

bool agent_event_request(const char *request, uint64_t *key, char agent[9],
                         agent_event_t *event, pid_t *pid, pid_t *candidate,
                         bool *metadata) {
  pid_t owner;
  return agent_event_owner_request(request, key, agent, event, pid, candidate,
                                   metadata, &owner);
}

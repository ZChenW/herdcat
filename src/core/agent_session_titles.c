#include "agent_sessions_internal.h"

#include <stdio.h>
#include <stdlib.h>

int agent_sessions_set_title(uint64_t key, const char *title) {
  agent_session_t *s = find_session(key);
  if (!s || !title || (*title && !utf8_label_valid(title, AGENT_TITLE_MAX)))
    return -1;
  if (!*title ||
      (!strcmp(s->agent, "opencode") && !strncmp(title, "New session - ", 14)))
    return 0;
  if (strcmp(s->title, title) || s->title_temporary) {
    s->title_temporary = false;
    snprintf(s->title, sizeof(s->title), "%s", title);
    touch_sessions();
  }
  return 0;
}
int agent_sessions_set_prompt(uint64_t key, const char *prompt) {
  agent_session_t *s = find_session(key);
  if (!s || !utf8_label_valid(prompt, AGENT_TITLE_MAX))
    return -1;
  const char *first = prompt;
  while (*first == ' ')
    first++;
  if (!*first || *first == '/')
    return -1;
  if (!s->title[0]) {
    snprintf(s->title, sizeof(s->title), "%s", prompt);
    s->title_temporary = true;
    touch_sessions();
  }
  return 0;
}

int agent_sessions_title_command(const char *request) {
  char key[17];
  int end = 0;
  bool prompt = request && !strncmp(request, "ask ", 4);
  if (!request || (!prompt && strncmp(request, "ttl ", 4)) ||
      strlen(request) > 21 + AGENT_TITLE_MAX ||
      sscanf(request + 4, "%16[0-9a-fA-F]%n", key, &end) != 1 || end != 16 ||
      request[20] != ' ' || !strtoull(key, NULL, 16))
    return 1;
  uint64_t id = strtoull(key, NULL, 16);
  return (prompt ? agent_sessions_set_prompt(id, request + 21)
                 : agent_sessions_set_title(id, request + 21)) < 0;
}

int agent_sessions_set_id(uint64_t key, const char *id) {
  if (!agent_session_id_valid(id))
    return -1;
  agent_sessions_adopt(key);
  agent_session_t *s = find_session(key);
  if (!s)
    return -1;
  bool changed = strcmp(s->session_id, id) != 0;
  if (changed) {
    snprintf(s->session_id, sizeof(s->session_id), "%s", id);
    s->title[0] = 0;
    s->title_temporary = false;
    touch_sessions();
  }
  if (changed && (!strcmp(s->agent, "codex") || !strcmp(s->agent, "kimi") ||
                  !strcmp(s->agent, "copilot") || s->transcript[0]))
    agent_sessions_refresh_title(key);
  return 0;
}
int agent_sessions_id_command(const char *request) {
  char key[17];
  int end = 0;
  if (!request || strlen(request) > 21 + AGENT_SESSION_ID_MAX ||
      sscanf(request, "sid %16[0-9a-fA-F]%n", key, &end) != 1 || end != 20 ||
      request[20] != ' ' || !strtoull(key, NULL, 16))
    return 1;
  return agent_sessions_set_id(strtoull(key, NULL, 16), request + 21) < 0;
}
void agent_sessions_refresh_title(uint64_t key) {
  agent_session_t *s = find_session(key);
  if (!s)
    return;
  char title[AGENT_TITLE_MAX + 1];
  if (!strcmp(s->agent, "claude") || !strcmp(s->agent, "codex") ||
      !strcmp(s->agent, "kimi") || !strcmp(s->agent, "pi") ||
      !strcmp(s->agent, "grok") || !strcmp(s->agent, "copilot")) {
    if (agent_title_read(s->agent, s->session_id, s->transcript, title) &&
        *title)
      agent_sessions_set_title(key, title);
  }
}
bool agent_sessions_title(pid_t pid, char *out, size_t capacity) {
  if (!out || !capacity)
    return false;
  out[0] = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    const agent_session_t *s = &sessions[i];
    if (s->used && s->pid == pid && s->title[0]) {
      snprintf(out, capacity, "%s", s->title);
      return true;
    }
  }
  return false;
}

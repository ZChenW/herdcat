#define _GNU_SOURCE
#include "core/agent_title.h"

#include "core/agent_transcript.h"
#include "platform/transcript_watch.h"
#include "utils/json.h"
#include "utils/utf8.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// json_text is intentionally permissive for window titles. Session titles
// additionally reject NUL and unpaired surrogates instead of repairing them.
bool agent_title_scalar(json_span_t value, char *out, size_t capacity) {
  if (value.end - value.p < 2 || *value.p != '"')
    return false;
  for (const char *p = value.p + 1; p < value.end - 1; p++) {
    if (*p != '\\')
      continue;
    p++;
    if (*p != 'u')
      continue;
    char hex[5];
    memcpy(hex, p + 1, 4);
    hex[4] = 0;
    unsigned long cp = strtoul(hex, NULL, 16);
    p += 4;
    if (!cp || (cp >= 0xdc00 && cp <= 0xdfff))
      return false;
    if (cp >= 0xd800 && cp <= 0xdbff) {
      if (value.end - p < 8 || p[1] != '\\' || p[2] != 'u')
        return false;
      memcpy(hex, p + 3, 4);
      cp = strtoul(hex, NULL, 16);
      if (cp < 0xdc00 || cp > 0xdfff)
        return false;
      p += 6;
    }
  }
  return json_text(value, out, capacity);
}
bool agent_title_line(const char *agent, const char *id, const char *line,
                      size_t length, char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  if (!agent || !line || length > 4096)
    return false;
  json_span_t doc, kind, title, sid;
  if (!json_document(line, length, &doc))
    return false;
  if (!strcmp(agent, "claude")) {
    if (!json_field(doc, "type", &kind) || !json_equal(kind, "ai-title") ||
        !json_field(doc, "aiTitle", &title))
      return false;
    if (id && *id &&
        (!json_field(doc, "sessionId", &sid) || !json_equal(sid, id)))
      return false;
  } else if (!strcmp(agent, "codex")) {
    if (!agent_session_id_valid(id) || !json_field(doc, "id", &sid) ||
        !json_equal(sid, id) || !json_field(doc, "thread_name", &title))
      return false;
  } else if (!strcmp(agent, "pi")) {
    if (!json_field(doc, "type", &kind) || !json_equal(kind, "session_info") ||
        !json_field(doc, "name", &title))
      return false;
  } else if (!strcmp(agent, "grok")) {
    json_span_t info;
    if (!agent_session_id_valid(id) || !json_field(doc, "info", &info) ||
        !json_field(info, "id", &sid) || !json_equal(sid, id) ||
        !json_field(doc, "generated_title", &title))
      return false;
  } else if (!strcmp(agent, "kimi")) {
    if (!json_field(doc, "title", &title))
      return false;
  } else {
    return false;
  }
  char decoded[4097];
  if (!agent_title_scalar(title, decoded, sizeof(decoded)) ||
      (*decoded && !utf8_label_valid(decoded, AGENT_TITLE_MAX)))
    return false;
  memcpy(out, decoded, strlen(decoded) + 1);
  return true;
}
bool agent_title_read(const char *agent, const char *id, const char *path,
                      char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  if (!strcmp(agent, "kimi"))
    return agent_title_kimi(id, out);
  if (!strcmp(agent, "copilot"))
    return agent_title_copilot(id, out);
  char index[AGENT_TRANSCRIPT_PATH_MAX + 1];
  if (!strcmp(agent, "codex")) {
    const char *base = getenv("CODEX_HOME");
    const char *home = getenv("HOME");
    if (!home || !agent_session_id_valid(id))
      return false;
    int n = base && *base
                ? snprintf(index, sizeof(index), "%s/session_index.jsonl", base)
                : snprintf(index, sizeof(index),
                           "%s/.codex/session_index.jsonl", home);
    if (n < 0 || (size_t)n >= sizeof(index))
      return false;
    path = index;
  } else if (strcmp(agent, "claude") && strcmp(agent, "pi") &&
             strcmp(agent, "grok")) {
    return false;
  }
  int fd = agent_title_open(path);
  if (fd < 0)
    return false;
  struct stat st;
  if (fstat(fd, &st) || st.st_size <= 0) {
    close(fd);
    return false;
  }
  size_t n = st.st_size > AGENT_TITLE_TAIL_MAX ? AGENT_TITLE_TAIL_MAX
                                               : (size_t)st.st_size;
  off_t offset = st.st_size - (off_t)n;
  char *tail = malloc(n + 1);
  if (!tail) {
    close(fd);
    return false;
  }
  // One bounded pread, even if the file changes during registration.
  ssize_t got = pread(fd, tail, n, offset);
  close(fd);
  bool found = false;
  if (got == (ssize_t)n) {
    tail[n] = 0;
    if (!strcmp(agent, "grok")) {
      found = !offset && agent_title_line(agent, id, tail, n, out);
      free(tail);
      return found;
    }
    size_t end = n;
    // An incomplete final record is never interpreted.
    if (end && tail[end - 1] != '\n') {
      while (end && tail[end - 1] != '\n')
        end--;
    }
    while (end && !found) {
      size_t stop = end - 1, start = stop;
      while (start && tail[start - 1] != '\n')
        start--;
      if (start || !offset)
        found = agent_title_line(agent, id, tail + start, stop - start, out);
      end = start;
    }
  }
  free(tail);
  return found;
}

static bool prompt_space(uint32_t cp) {
  return cp == ' ' || (cp >= '\t' && cp <= '\r') || cp == 0x85 || cp == 0xa0 ||
         cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}
static bool prompt_decoded(const char *decoded, char out[AGENT_TITLE_MAX + 1]) {
  size_t used = 0;
  bool space = false;
  for (const char *p = decoded; *p;) {
    uint32_t cp;
    size_t n = utf8_decode(p, &cp);
    if (!n)
      return false;
    if (prompt_space(cp)) {
      if (used && (cp == '\n' || cp == '\r' || cp == 0x85 || cp == 0x2028 ||
                   cp == 0x2029))
        break;
      space = used > 0;
    } else {
      if (utf8_control(cp) || (!used && cp == '/'))
        return false;
      if (used + space + n > AGENT_TITLE_MAX)
        break;
      if (space)
        out[used++] = ' ';
      memcpy(out + used, p, n);
      used += n;
      space = false;
    }
    p += n;
  }
  out[used] = 0;
  return used && utf8_label_valid(out, AGENT_TITLE_MAX);
}
static bool prompt_text(json_span_t value, char out[AGENT_TITLE_MAX + 1]) {
  char decoded[AGENT_TRANSCRIPT_LINE_MAX + 1];
  return agent_title_scalar(value, decoded, sizeof(decoded)) &&
         prompt_decoded(decoded, out);
}
static bool agy_prompt(json_span_t content, char out[AGENT_TITLE_MAX + 1]) {
  char decoded[AGENT_TRANSCRIPT_LINE_MAX + 1];
  if (!agent_title_scalar(content, decoded, sizeof(decoded)))
    return false;
  char *start = strstr(decoded, "<USER_REQUEST>");
  if (!start)
    return false;
  start += strlen("<USER_REQUEST>");
  char *end = strstr(start, "</USER_REQUEST>");
  if (!end)
    return false;
  *end = 0;
  return prompt_decoded(start, out);
}
static bool agy_user_input(json_span_t doc) {
  json_span_t type, source;
  return json_field(doc, "type", &type) && json_equal(type, "USER_INPUT") &&
         json_field(doc, "source", &source) &&
         json_equal(source, "USER_EXPLICIT");
}
bool agent_prompt_line(const char *agent, const char *line, size_t length,
                       char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  json_span_t doc, type, message, role, content, meta;
  if (!agent || length > AGENT_TRANSCRIPT_LINE_MAX ||
      !json_document(line, length, &doc) || !json_field(doc, "type", &type))
    return false;
  if (!strcmp(agent, "claude")) {
    if (!json_equal(type, "user") ||
        (json_field(doc, "isMeta", &meta) &&
         (meta.end - meta.p != 5 || memcmp(meta.p, "false", 5))) ||
        !json_field(doc, "message", &message) ||
        !json_field(message, "role", &role) || !json_equal(role, "user") ||
        !json_field(message, "content", &content))
      return false;
  } else if (!strcmp(agent, "agy")) {
    return agy_user_input(doc) && json_field(doc, "content", &content) &&
           agy_prompt(content, out);
  } else if (!strcmp(agent, "codex")) {
    if (!json_field(doc, "payload", &message))
      return false;
    if (json_equal(type, "event_msg")) {
      return json_field(message, "type", &role) &&
             json_equal(role, "user_message") &&
             json_field(message, "message", &content) &&
             prompt_text(content, out);
    }
    if (!json_equal(type, "response_item") ||
        !json_field(message, "role", &role) || !json_equal(role, "user") ||
        !json_field(message, "content", &content))
      return false;
  } else
    return false;
  if (*content.p == '"')
    return prompt_text(content, out);
  if (*content.p != '[')
    return false;
  json_span_t block, text;
  for (size_t i = 0; json_item(content, i, &block); i++)
    if (json_field(block, "type", &type) &&
        (json_equal(type, "text") ||
         (!strcmp(agent, "codex") && json_equal(type, "input_text"))) &&
        json_field(block, "text", &text))
      return prompt_text(text, out);
  return false;
}
bool agent_prompt_read(const char *agent, const char *path,
                       char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  int fd = transcript_watch_open(path);
  if (fd < 0)
    return false;
  const size_t limit = 256UL * 1024UL;
  char *head = malloc(limit);
  if (!head) {
    close(fd);
    return false;
  }
  ssize_t got = pread(fd, head, limit, 0);
  close(fd);
  bool found = false;
  for (size_t at = 0; got > 0 && at < (size_t)got && !found;) {
    const char *end = memchr(head + at, '\n', (size_t)got - at);
    if (!end)
      break;
    size_t n = (end - head - at);
    if (!strcmp(agent, "agy") && n > AGENT_TRANSCRIPT_LINE_MAX)
      break;  // Cannot safely determine if this was the first explicit input.
    found = agent_prompt_line(agent, head + at, n, out);
    json_span_t doc;
    if (!strcmp(agent, "agy") && json_document(head + at, n, &doc) &&
        agy_user_input(doc))
      break;  // The first explicit input owns the fallback, even if unusable.
    at += n + 1;
  }
  free(head);
  return found;
}
int agent_prompt_main(int argc, char **argv) {
  if (argc != 4)
    return 1;
  alarm(1);
  char prompt[AGENT_TITLE_MAX + 1];
  if (agent_prompt_read(argv[2], argv[3], prompt)) {
    size_t length = strlen(prompt);
    if (write(STDOUT_FILENO, prompt, length) != (ssize_t)length)
      return 1;
  }
  return 0;
}

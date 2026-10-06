#define _GNU_SOURCE
#include "core/agent_title.h"

#include "core/agent_transcript.h"
#include "platform/transcript_watch.h"
#include "utils/json.h"
#include "utils/utf8.h"

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

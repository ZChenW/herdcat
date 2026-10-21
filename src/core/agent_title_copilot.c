#define _GNU_SOURCE
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "utils/json.h"
#include "utils/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool horizontal(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

// Only top-level, single-line scalars are supported. Collections, tags,
// aliases, block scalars and indented keys never supply a title.
static bool yaml_scalar(const char *p, const char *end,
                        char out[AGENT_TITLE_MAX + 1]) {
  while (p < end && horizontal(*p))
    p++;
  while (end > p && horizontal(end[-1]))
    end--;
  if (p == end || strchr("#|>[{!&*", *p))
    return false;
  char decoded[4097];
  size_t used = 0;
  bool comment_gap = false;
  if (*p == '"') {
    const char *stop = ++p;
    while (stop < end) {
      if (*stop == '\\' && stop + 1 < end)
        stop += 2;
      else if (*stop == '"')
        break;
      else
        stop++;
    }
    if (stop == end)
      return false;
    json_span_t value;
    if (!json_document(p - 1, (size_t)(stop - p + 2), &value) ||
        !agent_title_scalar(value, decoded, sizeof(decoded)))
      return false;
    p = stop + 1;
  } else if (*p == '\'') {
    p++;
    bool closed = false;
    while (p < end && used < sizeof(decoded) - 1) {
      char c = *p++;
      if (c == '\'') {
        if (p < end && *p == '\'')
          p++;
        else {
          closed = true;
          break;
        }
      }
      decoded[used++] = c;
    }
    if (!closed)
      return false;
    decoded[used] = 0;
  } else {
    const char *stop = p;
    while (stop < end) {
      if (*stop == '#' && (stop == p || horizontal(stop[-1])))
        break;
      if (*stop == ':' && (stop + 1 == end || horizontal(stop[1])))
        return false;
      stop++;
    }
    const char *trim = stop;
    while (trim > p && horizontal(trim[-1]))
      trim--;
    used = (size_t)(trim - p);
    if (used >= sizeof(decoded))
      return false;
    memcpy(decoded, p, used);
    decoded[used] = 0;
    if (!strcmp(decoded, "null") || !strcmp(decoded, "~"))
      return false;
    comment_gap = stop < end && stop > p && horizontal(stop[-1]);
    p = stop;
  }
  // Closing quotes can only be followed by whitespace or a YAML comment.
  bool gap = comment_gap || (p < end && horizontal(*p));
  while (p < end && horizontal(*p))
    p++;
  if (p < end && (*p != '#' || !gap))
    return false;
  if (!utf8_label_valid(decoded, AGENT_TITLE_MAX))
    return false;
  memcpy(out, decoded, strlen(decoded) + 1);
  return true;
}

bool agent_title_copilot_yaml(const char *data, size_t length,
                              char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  if (!data || length > AGENT_TITLE_TAIL_MAX || memchr(data, 0, length))
    return false;
  char name[AGENT_TITLE_MAX + 1] = "", summary[AGENT_TITLE_MAX + 1] = "";
  const char *end = data + length;
  for (const char *line = data; line < end;) {
    const char *stop = memchr(line, '\n', (size_t)(end - line));
    if (!stop)
      stop = end;
    size_t n = (size_t)(stop - line);
    if (n <= 4096) {
      if (n >= 5 && !memcmp(line, "name:", 5) &&
          (n == 5 || horizontal(line[5])))
        yaml_scalar(line + 5, stop, name);
      else if (n >= 8 && !memcmp(line, "summary:", 8) &&
               (n == 8 || horizontal(line[8])))
        yaml_scalar(line + 8, stop, summary);
    }
    line = stop < end ? stop + 1 : end;
  }
  const char *title = name[0] ? name : summary;
  memcpy(out, title, strlen(title) + 1);
  return *out != 0;
}

bool agent_title_copilot(const char *id, char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  const char *home = getenv("HOME"), *base = getenv("COPILOT_HOME");
  if (!home || !agent_session_id_valid(id))
    return false;
  char root[AGENT_TRANSCRIPT_PATH_MAX + 1], path[AGENT_TRANSCRIPT_PATH_MAX + 1];
  int n = base && *base ? snprintf(root, sizeof(root), "%s", base)
                        : snprintf(root, sizeof(root), "%s/.copilot", home);
  if (n < 0 || (size_t)n >= sizeof(root))
    return false;
  n = snprintf(path, sizeof(path), "%s/session-state/%s/workspace.yaml", root,
               id);
  if (n < 0 || (size_t)n >= sizeof(path))
    return false;
  int fd = agent_title_open(path);
  if (fd < 0)
    return false;
  struct stat st;
  if (fstat(fd, &st) || st.st_size <= 0 || st.st_size > AGENT_TITLE_TAIL_MAX) {
    close(fd);
    return false;
  }
  size_t length = (size_t)st.st_size;
  char *data = malloc(length);
  if (!data) {
    close(fd);
    return false;
  }
  ssize_t got = pread(fd, data, length, 0);
  close(fd);
  bool found =
      got == (ssize_t)length && agent_title_copilot_yaml(data, length, out);
  free(data);
  return found;
}

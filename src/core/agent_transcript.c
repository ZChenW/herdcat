#include "core/agent_transcript.h"

#include "core/agent_hook.h"

#include <string.h>

typedef struct {
  const char *p, *end;
} value_t;

static void spaces(value_t *v) {
  while (v->p < v->end && strchr(" \r\n\t", *v->p))
    v->p++;
}
// Called only after full JSON validation. Keep slices rather than allocate a
// DOM.
static value_t next(value_t *v) {
  spaces(v);
  value_t result = {v->p, v->p};
  unsigned depth = 0;
  bool string = false, escape = false;
  while (v->p < v->end) {
    char c = *v->p;
    if (string) {
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"') {
        string = false;
        if (!depth) {
          v->p++;
          break;
        }
      }
    } else if (c == '"')
      string = true;
    else if (c == '{' || c == '[')
      depth++;
    else if (c == '}' || c == ']') {
      if (!depth)
        break;
      if (!--depth) {
        v->p++;
        break;
      }
    } else if (!depth && (c == ',' || c == ':' || strchr(" \r\n\t", c)))
      break;
    v->p++;
  }
  result.end = v->p;
  return result;
}
static bool string_is(value_t v, const char *text, bool prefix) {
  size_t n = strlen(text);
  if (!v.p || v.end - v.p < 2 || *v.p != '"' || v.end[-1] != '"')
    return false;
  size_t length = (size_t)(v.end - v.p - 2);
  // Marker and schema keys are ASCII literals. Escaped spellings fail closed.
  return (prefix ? length >= n : length == n) && !memcmp(v.p + 1, text, n);
}
static value_t field(value_t object, const char *key) {
  value_t found = {0};
  if (!object.p || *object.p != '{')
    return found;
  object.p++;
  while (object.p < object.end) {
    spaces(&object);
    if (*object.p == '}')
      break;
    value_t name = next(&object);
    spaces(&object);
    object.p++;  // colon, already validated
    value_t value = next(&object);
    if (string_is(name, key, false))
      found = value;  // JSON duplicate members: final value wins.
    spaces(&object);
    if (object.p < object.end && *object.p == ',')
      object.p++;
  }
  return found;
}

bool agent_transcript_interrupted(const char *agent, const char *line,
                                  size_t length) {
  if (!agent || !line || !length || length > AGENT_TRANSCRIPT_LINE_MAX)
    return false;
  agent_hook_scanner_t scanner;
  agent_hook_scan_init(&scanner);
  agent_hook_scan_feed(&scanner, line, length);
  if (!agent_hook_scan_finish(&scanner))
    return false;
  value_t root = {line, line + length};
  spaces(&root);
  value_t type = field(root, "type");
  if (!strcmp(agent, "claude")) {
    if (!string_is(type, "user", false))
      return false;
    value_t message = field(root, "message");
    if (!string_is(field(message, "role"), "user", false))
      return false;
    value_t content = field(message, "content");
    if (!content.p || *content.p != '[')
      return false;
    content.p++;
    value_t first = next(&content);
    spaces(&content);
    // Only the observed single text block qualifies, never a quoted tool
    // result.
    return content.p < content.end && *content.p == ']' &&
           string_is(field(first, "type"), "text", false) &&
           string_is(field(first, "text"), "[Request interrupted by user",
                     true);
  }
  if (!strcmp(agent, "codex") && string_is(type, "event_msg", false)) {
    value_t payload = field(root, "payload");
    value_t kind = field(payload, "type");
    if (string_is(kind, "turn_aborted", false))
      return true;
    value_t error = field(payload, "error");
    return string_is(kind, "task_complete", false) && error.p &&
           *error.p == '{';
  }
  return false;
}

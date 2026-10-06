#include "config/nameplate.h"

#include "utils/utf8.h"

#include <stdio.h>
#include <string.h>

static int field_id(const char *s, size_t n) {
  const char *fields[] = {"name", "project", "title", "agent", "state"};
  for (int i = 0; i < 5; i++)
    if (strlen(fields[i]) == n && !memcmp(fields[i], s, n))
      return i;
  return -1;
}
const char *nameplate_validate(const char *s, size_t *position) {
  *position = 0;
  if (!s)
    return "missing template";
  size_t n = strlen(s);
  if (n > 160) {
    *position = 160;
    return "template exceeds 160 bytes";
  }
  bool bold = false;
  size_t opening = 0;
  int lines = 1;
  for (size_t i = 0; i < n;) {
    *position = i;
    if (s[i] == '*' && s[i + 1] == '*') {
      if (!bold)
        opening = i;
      bold = !bold;
      i += 2;
    } else if (s[i] == '{') {
      const char *end = strchr(s + i + 1, '}');
      if (!end)
        return "unclosed placeholder";
      if (field_id(s + i + 1, (size_t)(end - s - i - 1)) < 0)
        return "unknown placeholder";
      i = (size_t)(end - s) + 1;
    } else if (s[i] == '}') {
      return "unexpected closing brace";
    } else if (s[i] == '\\' && s[i + 1] == 'n') {
      if (++lines > 2)
        return "at most two lines are supported";
      i += 2;
    } else {
      uint32_t cp;
      size_t bytes = utf8_decode(s + i, &cp);
      if (!bytes || utf8_control(cp))
        return "invalid UTF-8 or control character";
      i += bytes;
    }
  }
  if (bold) {
    *position = opening;
    return "unclosed bold marker";
  }
  return NULL;
}
const char *nameplate_builtin(sign_name_extra_t extra, bool title_main) {
  static const char *const templates[2][5] = {
      {"**{name}**  {agent} · {state}",
       "**{name}**  {title} · {agent} · {state}",   "**{name}**  {agent} · {state} · {title}",
       "{title}\\n**{name}**  {agent} · {state}",   "**{name}**  {agent} · {state}\\n{title}"  },
      {"**{name}**  {agent} · {state}",
       "**{name}**  {project} · {agent} · {state}", "**{name}**  {agent} · {state} · {project}",
       "{project}\\n**{name}**  {agent} · {state}", "**{name}**  {agent} · {state}\\n{project}"}
  };
  if (extra < SIGN_EXTRA_OFF || extra > SIGN_EXTRA_BELOW)
    extra = SIGN_EXTRA_OFF;
  return templates[title_main ? 1 : 0][extra];
}
static void append(nameplate_t *out, const char *text, size_t n, int line,
                   bool bold, bool state, bool gap) {
  size_t used = strlen(out->text);
  if (!n || out->count >= NAMEPLATE_RUN_MAX || used >= sizeof(out->text) ||
      n >= sizeof(out->text) - used)
    return;
  nameplate_run_t *r = &out->runs[out->count++];
  *r = (nameplate_run_t){.start = (uint16_t)used,
                         .length = (uint16_t)n,
                         .line = (uint8_t)line,
                         .bold = bold,
                         .state = state,
                         .gap = gap};
  memcpy(out->text + used, text, n);
  out->text[used + n] = 0;
}
static void segment(const char *begin, const char *end,
                    const nameplate_fields_t *fields, nameplate_t *out,
                    int line, bool *bold, bool separator) {
  const char *values[] = {fields->name, fields->project, fields->title,
                          fields->agent, fields->state};
  bool empty = false, b = *bold;
  int first = out->count;
  for (const char *p = begin; p < end; p++) {
    if (*p == '{') {
      const char *close = strchr(p, '}');
      int id = field_id(p + 1, (size_t)(close - p - 1));
      const char *v = values[id];
      if (!v || !*v || (id == 2 && fields->name && !strcmp(v, fields->name)))
        empty = true;
      p = close;
    }
  }
  if (separator && !empty)
    append(out, " · ", 4, line, b, false, false);
  const char *p = begin;
  while (p < end) {
    if (end - p >= 2 && !memcmp(p, "**", 2)) {
      b = !b;
      p += 2;
    } else if (*p == '{') {
      const char *close = strchr(p, '}');
      int id = field_id(p + 1, (size_t)(close - p - 1));
      if (!empty)
        append(out, values[id], strlen(values[id]), line, b, id == 4, false);
      p = close + 1;
    } else {
      const char *start = p++;
      while (p < end && *p != '{' && !(end - p >= 2 && !memcmp(p, "**", 2)))
        p++;
      bool gap = !b && p - start == 2 && !memcmp(start, "  ", 2);
      if (!empty)
        append(out, start, (size_t)(p - start), line, b, false, gap);
    }
  }
  *bold = b;
  for (int i = first; i < out->count; i++)
    out->runs[i].segment = (uint8_t)first;
}
void nameplate_expand(const char *source, const nameplate_fields_t *fields,
                      nameplate_t *out) {
  memset(out, 0, sizeof(*out));
  if (!fields)
    return;
  size_t at;
  if (nameplate_validate(source, &at))
    return;
  if ((!strcmp(source, nameplate_builtin(SIGN_EXTRA_INLINE, false)) &&
       (!fields->title || !fields->title[0] ||
        (fields->name && !strcmp(fields->title, fields->name)))) ||
      (!strcmp(source, nameplate_builtin(SIGN_EXTRA_INLINE, true)) &&
       (!fields->project || !fields->project[0] ||
        (fields->name && !strcmp(fields->project, fields->name)))))
    source = nameplate_builtin(SIGN_EXTRA_OFF, false);
  bool bold = false;
  int line = 0;
  const char *p = source;
  while (*p) {
    const char *end = strstr(p, "\\n");
    if (!end)
      end = p + strlen(p);
    int first = out->count;
    const char *start = p;
    while (start < end) {
      const char *sep = strstr(start, " · ");
      if (!sep || sep > end)
        sep = end;
      segment(start, sep, fields, out, line, &bold, out->count > first);
      start = sep == end ? end : sep + 4;
    }
    bool content = false;
    for (int i = first; i < out->count; i++) {
      const nameplate_run_t *r = &out->runs[i];
      for (int k = 0; k < r->length; k++)
        content |= out->text[r->start + k] != ' ';
    }
    if (content)
      line++;
    else {
      if (first < out->count)
        out->text[out->runs[first].start] = 0;
      out->count = first;
    }
    p = *end ? end + 2 : end;
  }
  out->lines = line;
  out->legacy = !strcmp(source, nameplate_builtin(SIGN_EXTRA_OFF, false));
  if (out->legacy && fields->name && fields->name[0] && fields->agent &&
      fields->agent[0] && fields->state && fields->state[0]) {
    memset(out, 0, sizeof(*out));
    append(out, fields->name, strlen(fields->name), 0, true, false, false);
    append(out, "  ", 2, 0, false, false, true);
    char meta[128];
    snprintf(meta, sizeof(meta), "%s · %s", fields->agent, fields->state);
    append(out, meta, strlen(meta), 0, false, true, false);
    out->runs[1].segment = out->runs[2].segment = 1;
    out->lines = 1;
    out->legacy = true;
  }
}

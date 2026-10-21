#include "utils/json.h"

#include "utils/json_string.h"

#include <stdint.h>
#include <string.h>

static void space(json_span_t *s) {
  while (s->p < s->end && strchr(" \t\r\n", *s->p))
    s->p++;
}
static bool take(json_span_t *s, char expected) {
  if (s->p == s->end)
    return false;
  char value = *s->p++;
  return value == expected;
}
static bool string(json_span_t *s) {
  if (!take(s, '"'))
    return false;
  while (s->p < s->end) {
    unsigned char c = (unsigned char)*s->p++;
    if (c == '"')
      return true;
    if (c < 32)
      return false;
    if (c == '\\') {
      if (s->p == s->end)
        return false;
      c = (unsigned char)*s->p++;
      if (c == 'u') {
        for (int i = 0; i < 4; i++) {
          if (s->p == s->end)
            return false;
          char hex = *s->p++;
          if (!strchr("0123456789abcdefABCDEF", hex))
            return false;
        }
      } else if (!strchr("\"\\/bfnrt", c))
        return false;
    }
  }
  return false;
}
static bool digit(json_span_t *s) {
  return s->p < s->end && *s->p >= '0' && *s->p <= '9';
}
static bool skip(json_span_t *s, unsigned depth) {
  space(s);
  if (s->p == s->end || depth > 32)
    return false;
  char c = *s->p;
  if (c == '"')
    return string(s);
  if (c == '{' || c == '[') {
    char close = c == '{' ? '}' : ']';
    s->p++;
    space(s);
    if (s->p < s->end && *s->p == close) {
      s->p++;
      return true;
    }
    for (;;) {
      if (c == '{') {
        if (!string(s))
          return false;
        space(s);
        if (!take(s, ':'))
          return false;
      }
      if (!skip(s, depth + 1))
        return false;
      space(s);
      if (s->p == s->end)
        return false;
      if (*s->p == close) {
        s->p++;
        return true;
      }
      if (*s->p++ != ',')
        return false;
      space(s);
    }
  }
  const char *literals[] = {"true", "false", "null"};
  for (size_t i = 0; i < 3; i++) {
    size_t n = strlen(literals[i]);
    if ((size_t)(s->end - s->p) >= n && !memcmp(s->p, literals[i], n)) {
      s->p += n;
      return true;
    }
  }
  if (c == '-')
    s->p++;
  if (!digit(s))
    return false;
  if (*s->p++ != '0')
    while (digit(s))
      s->p++;
  if (s->p < s->end && *s->p == '.') {
    s->p++;
    if (!digit(s))
      return false;
    while (digit(s))
      s->p++;
  }
  if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
    s->p++;
    if (s->p < s->end && (*s->p == '-' || *s->p == '+'))
      s->p++;
    if (!digit(s))
      return false;
    while (digit(s))
      s->p++;
  }
  return true;
}
bool json_document(const char *text, size_t length, json_span_t *out) {
  if (!text || !out || length > 65536 || memchr(text, 0, length))
    return false;
  json_span_t s = {text, text + length};
  space(&s);
  *out = s;
  if (!skip(&s, 0))
    return false;
  out->end = s.p;
  space(&s);
  return s.p == s.end;
}
bool json_field(json_span_t s, const char *key, json_span_t *out) {
  if (!take(&s, '{'))
    return false;
  bool found = false;
  space(&s);
  while (s.p < s.end && *s.p != '}') {
    json_span_t name = s;
    if (!string(&s))
      return false;
    name.end = s.p;
    space(&s);
    if (!take(&s, ':'))
      return false;
    space(&s);
    json_span_t v = s;
    if (!skip(&s, 0))
      return false;
    v.end = s.p;
    if (json_equal(name, key)) {
      if (found)
        return false;
      *out = v;
      found = true;
    }
    space(&s);
    if (s.p == s.end || *s.p == '}')
      break;
    if (*s.p++ != ',')
      return false;
    space(&s);
  }
  return found;
}
bool json_item(json_span_t s, size_t index, json_span_t *out) {
  if (!take(&s, '['))
    return false;
  space(&s);
  for (size_t i = 0; s.p < s.end && *s.p != ']'; i++) {
    json_span_t v = s;
    if (!skip(&s, 0))
      return false;
    if (i == index) {
      v.end = s.p;
      *out = v;
      return true;
    }
    space(&s);
    if (!take(&s, ','))
      return false;
    space(&s);
  }
  return false;
}
bool json_equal(json_span_t v, const char *text) {
  size_t n = strlen(text);
  return (size_t)(v.end - v.p) == n + 2 && v.p[0] == '"' && v.end[-1] == '"' &&
         !memcmp(v.p + 1, text, n);
}
bool json_uint(json_span_t v, uint64_t *out) {
  *out = 0;
  if (v.p == v.end || *v.p < '0' || *v.p > '9' ||
      (v.end - v.p > 1 && *v.p == '0'))
    return false;
  while (v.p < v.end) {
    unsigned d = (unsigned)(*v.p++ - '0');
    if (d > 9 || *out > (UINT64_MAX - d) / 10)
      return false;
    *out = *out * 10 + d;
  }
  return true;
}
bool json_text(json_span_t v, char *out, size_t capacity) {
  if (v.end - v.p < 2 || *v.p != '"' || v.end[-1] != '"' || !capacity)
    return false;
  json_string_copy(v.p + 1, (size_t)(v.end - v.p - 2), out, capacity);
  return true;
}

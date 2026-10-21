#ifndef HERDCAT_PATH_WIRE_H
#define HERDCAT_PATH_WIRE_H

#include "core/agent_sessions.h"

#include <string.h>

static inline void path_hex(const char *path, char *out) {
  const char digits[] = "0123456789abcdef";
  while (*path) {
    unsigned char c = (unsigned char)*path++;
    *out++ = digits[c >> 4];
    *out++ = digits[c & 15];
  }
  *out = 0;
}
static inline int path_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static inline bool path_unhex(const char *text, char *out) {
  size_t n = strlen(text);
  if (n % 2 || n > AGENT_CWD_MAX * 2UL)
    return false;
  for (size_t i = 0; i < n; i += 2) {
    int a = path_nibble(text[i]), b = path_nibble(text[i + 1]);
    if (a < 0 || b < 0 || !(a | b))
      return false;
    out[i / 2] = (char)(a * 16 + b);
  }
  out[n / 2] = 0;
  return true;
}
// Lexical normalization prevents sibling prefixes and '..' from qualifying.
// cwd is supplied by the agent; this does not perform filesystem IO.
static inline bool path_normalize(const char *path, char *out) {
  if (!path || *path != '/' || strlen(path) > AGENT_CWD_MAX)
    return false;
  size_t used = 1;
  out[0] = '/';
  for (const char *p = path + 1; *p;) {
    const char *end = strchr(p, '/');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n == 2 && !memcmp(p, "..", 2)) {
      while (used > 1 && out[used - 1] != '/')
        used--;
      if (used > 1)
        used--;
    } else if (n && !(n == 1 && *p == '.')) {
      if (used > 1)
        out[used++] = '/';
      memcpy(out + used, p, n);
      used += n;
    }
    p += n;
    if (*p)
      p++;
  }
  out[used] = 0;
  return true;
}
#endif

#ifndef BONGOCAT_UTF8_H
#define BONGOCAT_UTF8_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Strict Unicode scalar decoder. Zero means invalid; callers choose recovery.
static inline size_t utf8_decode(const char *text, uint32_t *codepoint) {
  const unsigned char *p = (const unsigned char *)text;
  if (!p || !*p)
    return 0;
  unsigned n = *p < 0x80                  ? 1
               : *p >= 0xc2 && *p <= 0xdf ? 2
               : *p >= 0xe0 && *p <= 0xef ? 3
               : *p >= 0xf0 && *p <= 0xf4 ? 4
                                          : 0;
  if (!n)
    return 0;
  uint32_t cp = p[0] & (n == 1 ? 0x7fU : (1U << (7 - n)) - 1);
  for (unsigned i = 1; i < n; i++) {
    if (p[i] < 0x80 || p[i] > 0xbf)
      return 0;
    cp = (cp << 6) | (p[i] & 0x3f);
  }
  if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
      (n == 4 && cp < 0x10000) || cp > 0x10ffff ||
      (cp >= 0xd800 && cp <= 0xdfff))
    return 0;
  *codepoint = cp;
  return n;
}
static inline bool utf8_control(uint32_t cp) {
  return cp < 0x20 || (cp >= 0x7f && cp <= 0x9f);
}
static inline bool utf8_label_valid(const char *s, size_t limit) {
  if (!s || !*s)
    return false;
  size_t used = 0;
  while (*s) {
    uint32_t cp;
    size_t n = utf8_decode(s, &cp);
    if (!n || used + n > limit || utf8_control(cp))
      return false;
    used += n;
    s += n;
  }
  return true;
}
#endif

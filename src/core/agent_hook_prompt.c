#include "agent_hook_internal.h"
#include "utils/utf8.h"

#include <string.h>

static bool prompt_space(uint32_t cp) {
  return cp == ' ' || (cp >= '\t' && cp <= '\r') || cp == 0x85 || cp == 0xa0 ||
         cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

static void prompt_scalar(agent_hook_scanner_t *s, uint32_t cp) {
  if (s->prompt_high) {
    if (cp < 0xdc00 || cp > 0xdfff) {
      s->prompt_invalid = true;
      return;
    }
    cp = 0x10000 + ((s->prompt_high - 0xd800) << 10) + cp - 0xdc00;
    s->prompt_high = 0;
  } else if (cp >= 0xd800 && cp <= 0xdbff) {
    s->prompt_high = cp;
    return;
  } else if (cp >= 0xdc00 && cp <= 0xdfff) {
    s->prompt_invalid = true;
    return;
  }
  if (prompt_space(cp)) {
    if (s->prompt_length && (cp == '\n' || cp == '\r' || cp == 0x85 ||
                             cp == 0x2028 || cp == 0x2029))
      s->prompt_done = true;
    s->prompt_space = s->prompt_length > 0;
    return;
  }
  if (utf8_control(cp) || (!s->prompt_length && cp == '/')) {
    s->prompt_invalid = true;
    return;
  }
  unsigned n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
  if (s->prompt_length + s->prompt_space + n > AGENT_TITLE_MAX) {
    s->prompt_done = true;
    return;
  }
  if (s->prompt_space)
    s->prompt[s->prompt_length++] = ' ';
  s->prompt_space = false;
  size_t used = s->prompt_length;
  if (n == 1)
    s->prompt[used] = (char)cp;
  else {
    s->prompt[used] = (char)((n == 2   ? 0xc0
                              : n == 3 ? 0xe0
                                       : 0xf0) |
                             (cp >> (6 * (n - 1))));
    for (unsigned i = 1; i < n; i++)
      s->prompt[used + i] = (char)(0x80 | ((cp >> (6 * (n - i - 1))) & 63));
  }
  s->prompt_length += n;
  s->prompt[s->prompt_length] = 0;
}

// Reuse the scanner's pre-byte escape/UTF-8 state. Keep only the normalized
// first line even for arbitrarily long strings and leading whitespace.
void agent_hook_prompt_byte(agent_hook_scanner_t *s, unsigned char c) {
  if (s->prompt_done || s->prompt_invalid)
    return;
  if (s->unicode_left) {
    if (!hex(c))
      return;
    s->prompt_cp =
        (s->prompt_cp << 4) | (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    if (s->unicode_left == 1)
      prompt_scalar(s, s->prompt_cp);
  } else if (s->utf8_left) {
    s->prompt_cp = (s->prompt_cp << 6) | (c & 63);
    if (s->utf8_left == 1)
      prompt_scalar(s, s->prompt_cp);
  } else if (s->escape_next) {
    if (c == 'u') {
      s->prompt_cp = 0;
    } else {
      static const char escapes[] = "bfnrt";
      const char *escaped = strchr(escapes, c);
      prompt_scalar(s, escaped ? (unsigned char)"\b\f\n\r\t"[escaped - escapes]
                               : c);
    }
  } else if (c != '\\' && c != '"') {
    if (c >= 0x80)
      s->prompt_cp = c & (c < 0xe0 ? 31 : c < 0xf0 ? 15 : 7);
    else
      prompt_scalar(s, c);
  }
}

bool agent_hook_prompt(const agent_hook_scanner_t *s, const char *event_name,
                       char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  agent_event_t event;
  bool metadata = false;
  if (!(s->valid_fields & (1U << HOOK_FIELD_PROMPT)) ||
      !agent_hook_event_override(s, event_name, &event, &metadata) ||
      !metadata || event != AGENT_EVENT_WORKING)
    return false;
  memcpy(out, s->prompt, s->prompt_length + 1);
  return true;
}

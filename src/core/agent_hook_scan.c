#define _POSIX_C_SOURCE 200809L
#include "agent_hook_internal.h"
#include "core/agent_adapters.h"
#include "core/agent_hook.h"
#include "core/agent_sessions.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

enum {
  OBJECT_FIRST,
  OBJECT_KEY,
  OBJECT_COLON,
  OBJECT_VALUE,
  OBJECT_END,
  ARRAY_FIRST,
  ARRAY_VALUE,
  ARRAY_END
};
enum {
  TOKEN_NONE,
  TOKEN_STRING,
  TOKEN_LITERAL,
  TOKEN_NUMBER
};
enum {
  NUMBER_SIGN,
  NUMBER_ZERO,
  NUMBER_INTEGER,
  NUMBER_DOT,
  NUMBER_FRACTION,
  NUMBER_EXPONENT,
  NUMBER_EXPONENT_SIGN,
  NUMBER_EXPONENT_DIGITS
};

static bool space(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
bool digit(unsigned char c) {
  return c >= '0' && c <= '9';
}
bool hex(unsigned char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

void agent_hook_scan_init(agent_hook_scanner_t *s) {
  agent_hook_scan_adapter(s, agent_adapter_find("claude"));
}

void agent_hook_scan_adapter(agent_hook_scanner_t *s,
                             const agent_adapter_t *adapter) {
  memset(s, 0, sizeof(*s));
  s->adapter = adapter;
  for (unsigned i = 0; i < HOOK_FIELD_COUNT; i++)
    s->ranks[i] = UINT_MAX;
}

static void finish_string(agent_hook_scanner_t *s) {
  s->text[s->text_length] = '\0';
  if (!s->key && s->depth == 1 && s->field == HOOK_FIELD_PARENT)
    s->child_session = s->text_length > 0 || s->overflow;
  bool prompt = !s->key && s->field == HOOK_FIELD_PROMPT && s->depth == 1;
  bool valid =
      (prompt || !s->overflow) &&
      (!s->escaped || (!s->key && (s->field == HOOK_FIELD_CWD ||
                                   s->field == HOOK_FIELD_TRANSCRIPT ||
                                   s->field == HOOK_FIELD_TITLE || prompt)));
  if (s->key) {
    if (s->depth == 1) {
      s->field = HOOK_FIELD_NONE;
      s->array_field = s->array_taken = false;
      const agent_hook_alias_t *alias =
          valid ? agent_adapter_alias(s->adapter, s->text) : NULL;
      if (alias && alias->rank <= s->ranks[alias->field]) {
        s->field = alias->field;
        s->ranks[s->field] = alias->rank;
        s->array_field = alias->array;
        s->valid_fields &= ~(1U << s->field);
        if (s->field == HOOK_FIELD_PARENT)
          s->child_session = false;
        if (s->field == HOOK_FIELD_STOP)
          s->stop_present = true;
      }
    }
    s->stack[s->depth - 1] = OBJECT_COLON;
  } else if (valid && ((!s->array_field && s->depth == 1) ||
                       (s->array_field && !s->array_taken && s->depth == 2 &&
                        s->stack[1] == ARRAY_END))) {
    s->array_taken = true;
    char *target = NULL;
    size_t capacity = 0;
    switch (s->field) {
    case HOOK_FIELD_TITLE:
      target = s->title;
      capacity = sizeof(s->title);
      break;
    case HOOK_FIELD_PROMPT:
      if (!s->prompt_invalid && !s->prompt_high && s->prompt_length)
        s->valid_fields |= 1U << HOOK_FIELD_PROMPT;
      break;
    case HOOK_FIELD_EVENT:
      target = s->event;
      capacity = sizeof(s->event);
      break;
    case HOOK_FIELD_SESSION:
      target = s->session_id;
      capacity = sizeof(s->session_id);
      break;
    case HOOK_FIELD_TRANSCRIPT:
      target = s->transcript;
      capacity = sizeof(s->transcript);
      break;
    case HOOK_FIELD_CWD:
      target = s->cwd;
      capacity = sizeof(s->cwd);
      break;
    case HOOK_FIELD_PARENT:
      s->child_session = s->text_length > 0;
      break;
    case HOOK_FIELD_STATUS:
      target = s->status;
      capacity = sizeof(s->status);
      break;
    case HOOK_FIELD_NOTIFICATION:
      target = s->notification;
      capacity = sizeof(s->notification);
      break;
    default:
      break;
    }
    if (target && s->text_length < capacity) {
      memcpy(target, s->text, s->text_length + 1);
      s->valid_fields |= 1U << s->field;
    }
  }
  if (!s->key && s->array_field && s->depth == 2 && s->stack[1] == ARRAY_END)
    s->array_taken = true;
  s->token = TOKEN_NONE;
}

static void string_byte(agent_hook_scanner_t *s, unsigned char c) {
  if (!s->key && s->depth == 1 && s->field == HOOK_FIELD_PROMPT)
    agent_hook_prompt_byte(s, c);
  if (s->utf8_left) {
    if (c < s->utf8_min || c > s->utf8_max) {
      s->failed = true;
      return;
    }
    s->utf8_left--;
    s->utf8_min = 0x80;
    s->utf8_max = 0xbf;
  } else if (s->unicode_left) {
    if (!hex(c)) {
      s->failed = true;
      return;
    }
    s->unicode_left--;
  } else if (s->escape_next) {
    s->escape_next = false;
    if (c == 'u') {
      s->unicode_left = 4;
    } else if (!c || !strchr("\"\\/bfnrt", c)) {
      s->failed = true;
      return;
    }
  } else if (c == '"') {
    finish_string(s);
    return;
  } else if (c == '\\') {
    s->escaped = true;
    s->escape_next = true;
  } else if (c < 0x20) {
    s->failed = true;
    return;
  } else if (c >= 0x80) {
    s->utf8_min = 0x80;
    s->utf8_max = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) {
      s->utf8_left = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      s->utf8_left = 2;
      if (c == 0xe0)
        s->utf8_min = 0xa0;
      if (c == 0xed)
        s->utf8_max = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      s->utf8_left = 3;
      if (c == 0xf0)
        s->utf8_min = 0x90;
      if (c == 0xf4)
        s->utf8_max = 0x8f;
    } else {
      s->failed = true;
      return;
    }
  }
  if (s->text_length < sizeof(s->text) - 1) {
    s->text[s->text_length++] = (char)c;
  } else {
    s->overflow = true;
  }
}

static bool number_complete(unsigned state) {
  return state == NUMBER_ZERO || state == NUMBER_INTEGER ||
         state == NUMBER_FRACTION || state == NUMBER_EXPONENT_DIGITS;
}

static void finish_number(agent_hook_scanner_t *s) {
  if (s->depth != 1 || s->field != HOOK_FIELD_PID || s->overflow)
    return;
  s->text[s->text_length] = '\0';
  if (!s->text_length || strspn(s->text, "0123456789") != s->text_length)
    return;
  errno = 0;
  long value = strtol(s->text, NULL, 10);
  if (!errno && value > 1 && value <= INT_MAX) {
    s->pid = (pid_t)value;
    s->valid_fields |= 1U << HOOK_FIELD_PID;
  }
}

static bool number_byte(agent_hook_scanner_t *s, unsigned char c) {
  unsigned state = s->number_state;
  if (digit(c)) {
    if (state == NUMBER_SIGN) {
      s->number_state = c == '0' ? NUMBER_ZERO : NUMBER_INTEGER;
    } else if (state == NUMBER_DOT || state == NUMBER_FRACTION) {
      s->number_state = NUMBER_FRACTION;
    } else if (state == NUMBER_EXPONENT || state == NUMBER_EXPONENT_SIGN ||
               state == NUMBER_EXPONENT_DIGITS) {
      s->number_state = NUMBER_EXPONENT_DIGITS;
    } else if (state != NUMBER_INTEGER) {
      s->failed = true;
    }
  } else if (c == '.' && (state == NUMBER_ZERO || state == NUMBER_INTEGER)) {
    s->number_state = NUMBER_DOT;
  } else if ((c == 'e' || c == 'E') &&
             (state == NUMBER_ZERO || state == NUMBER_INTEGER ||
              state == NUMBER_FRACTION)) {
    s->number_state = NUMBER_EXPONENT;
  } else if ((c == '+' || c == '-') && state == NUMBER_EXPONENT) {
    s->number_state = NUMBER_EXPONENT_SIGN;
  } else {
    s->failed |= !number_complete(state);
    if (!s->failed)
      finish_number(s);
    s->token = TOKEN_NONE;
    return false;
  }
  if (s->text_length < sizeof(s->text) - 1)
    s->text[s->text_length++] = (char)c;
  else
    s->overflow = true;
  return true;
}

static void finish_literal(agent_hook_scanner_t *s) {
  if (s->depth == 1 && s->field == HOOK_FIELD_STOP &&
      (s->literal[0] == 't' || s->literal[0] == 'f')) {
    s->stop_hook_active = s->literal[0] == 't';
    s->valid_fields |= 1U << HOOK_FIELD_STOP;
  }
  s->token = TOKEN_NONE;
}

static void begin_string(agent_hook_scanner_t *s, bool key) {
  s->token = TOKEN_STRING;
  s->key = key;
  s->text_length = 0;
  s->escaped = s->escape_next = s->overflow = false;
  s->unicode_left = s->utf8_left = 0;
  if (!key && s->depth == 1 && s->field == HOOK_FIELD_PROMPT) {
    s->prompt[0] = 0;
    s->prompt_length = s->prompt_cp = s->prompt_high = 0;
    s->prompt_space = s->prompt_done = s->prompt_invalid = false;
  }
}

static void begin_value(agent_hook_scanner_t *s, unsigned char c) {
  unsigned state = s->stack[s->depth - 1];
  s->stack[s->depth - 1] = state == OBJECT_VALUE ? OBJECT_END : ARRAY_END;
  if (c == '"') {
    begin_string(s, false);
  } else if (c == '{' || c == '[') {
    if (s->depth >= AGENT_HOOK_MAX_DEPTH) {
      s->failed = true;
      return;
    }
    if (s->depth == 1 && c != '[')
      s->array_field = false;
    s->stack[s->depth++] = c == '{' ? OBJECT_FIRST : ARRAY_FIRST;
  } else if (c == 't' || c == 'f' || c == 'n') {
    s->token = TOKEN_LITERAL;
    s->literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
    s->literal_pos = 1;
  } else if (c == '-' || digit(c)) {
    s->token = TOKEN_NUMBER;
    s->text_length = 1;
    s->text[0] = (char)c;
    s->overflow = false;
    s->number_state = c == '-'   ? NUMBER_SIGN
                      : c == '0' ? NUMBER_ZERO
                                 : NUMBER_INTEGER;
  } else {
    s->failed = true;
  }
}

void agent_hook_scan_feed(agent_hook_scanner_t *s, const char *data,
                          size_t length) {
  if (!data && length) {
    s->failed = true;
  }
  for (size_t i = 0; i < length && !s->failed; i++) {
    unsigned char c = (unsigned char)data[i];
    if (s->token == TOKEN_STRING) {
      string_byte(s, c);
      continue;
    }
    if (s->token == TOKEN_LITERAL) {
      if (s->literal[s->literal_pos]) {
        if (c != (unsigned char)s->literal[s->literal_pos++]) {
          s->failed = true;
        }
        continue;
      }
      finish_literal(s);
    } else if (s->token == TOKEN_NUMBER && number_byte(s, c)) {
      continue;
    }
    if (space(c)) {
      continue;
    }
    if (!s->started) {
      s->started = true;
      if (c != '{') {
        s->failed = true;
      } else {
        s->stack[s->depth++] = OBJECT_FIRST;
      }
      continue;
    }
    if (s->complete || !s->depth) {
      s->failed = true;
      continue;
    }
    unsigned state = s->stack[s->depth - 1];
    if ((c == '}' && (state == OBJECT_FIRST || state == OBJECT_END)) ||
        (c == ']' && (state == ARRAY_FIRST || state == ARRAY_END))) {
      s->depth--;
      s->complete = s->depth == 0;
    } else if (state == OBJECT_FIRST || state == OBJECT_KEY) {
      if (c == '"') {
        begin_string(s, true);
      } else {
        s->failed = true;
      }
    } else if (state == OBJECT_COLON) {
      if (c == ':') {
        s->stack[s->depth - 1] = OBJECT_VALUE;
      } else {
        s->failed = true;
      }
    } else if (state == OBJECT_VALUE || state == ARRAY_FIRST ||
               state == ARRAY_VALUE) {
      begin_value(s, c);
    } else if (c == ',') {
      s->stack[s->depth - 1] = state == OBJECT_END ? OBJECT_KEY : ARRAY_VALUE;
    } else {
      s->failed = true;
    }
  }
}

bool agent_hook_scan_finish(agent_hook_scanner_t *s) {
  return !s->failed && s->complete && s->token == TOKEN_NONE;
}

bool agent_hook_event(const agent_hook_scanner_t *s, agent_event_t *event) {
  return agent_hook_event_override(s, NULL, event, NULL);
}

bool agent_hook_event_override(const agent_hook_scanner_t *s, const char *name,
                               agent_event_t *event, bool *metadata) {
  if (!event || s->child_session || s->failed || !s->complete ||
      (!name && !(s->valid_fields & (1U << HOOK_FIELD_EVENT))))
    return false;
  return agent_adapter_event(
      s->adapter, name ? name : s->event,
      s->valid_fields & (1U << HOOK_FIELD_NOTIFICATION) ? s->notification
                                                        : NULL,
      s->valid_fields & (1U << HOOK_FIELD_STATUS) ? s->status : NULL,
      s->stop_present, s->valid_fields & (1U << HOOK_FIELD_STOP),
      s->stop_hook_active, event, metadata);
}

bool agent_hook_valid_agent(const char *agent) {
  if (!agent || !*agent) {
    return false;
  }
  for (size_t i = 0; agent[i]; i++) {
    if (i >= AGENT_NAME_MAX || agent[i] < 'a' || agent[i] > 'z') {
      return false;
    }
  }
  return true;
}

uint64_t agent_hook_key(const char *agent, const agent_hook_scanner_t *s) {
  uint64_t hash = UINT64_C(14695981039346656037);
  const char *parts[] = {
      agent, ":",
      s->valid_fields & (1U << HOOK_FIELD_SESSION) ? s->session_id : "default"};
  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    // Every part is NUL-terminated; the loop stops at ':'s trailing NUL too.
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
    for (const unsigned char *p = (const unsigned char *)parts[i]; *p; p++) {
      hash ^= *p;
      hash *= UINT64_C(1099511628211);
    }
  }
  return hash ? hash : 1;
}

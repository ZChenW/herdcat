#define _POSIX_C_SOURCE 200809L
#include "core/agent_hook.h"

#include "core/control.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
  FIELD_NONE,
  FIELD_EVENT,
  FIELD_SESSION,
  FIELD_NOTIFICATION,
  FIELD_STOP
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
static bool digit(unsigned char c) {
  return c >= '0' && c <= '9';
}
static bool hex(unsigned char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

void agent_hook_scan_init(agent_hook_scanner_t *s) {
  memset(s, 0, sizeof(*s));
}

static void finish_string(agent_hook_scanner_t *s) {
  s->text[s->text_length] = '\0';
  bool valid = !s->overflow && !s->escaped;
  if (s->key) {
    if (s->depth == 1) {
      static const char *const FIELDS[] = {"", "hook_event_name", "session_id",
                                           "notification_type",
                                           "stop_hook_active"};
      s->field = FIELD_NONE;
      for (unsigned i = FIELD_EVENT; valid && i <= FIELD_STOP; i++) {
        if (strcmp(s->text, FIELDS[i]) == 0) {
          s->field = i;
          s->valid_fields &= ~(1U << i);
          if (i == FIELD_STOP) {
            s->stop_present = true;
          }
          break;
        }
      }
    }
    s->stack[s->depth - 1] = OBJECT_COLON;
  } else if (s->depth == 1 && valid) {
    char *target = NULL;
    size_t capacity = 0;
    switch (s->field) {
    case FIELD_EVENT:
      target = s->event;
      capacity = sizeof(s->event);
      break;
    case FIELD_SESSION:
      target = s->session_id;
      capacity = sizeof(s->session_id);
      break;
    case FIELD_NOTIFICATION:
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
  s->token = TOKEN_NONE;
}

static void string_byte(agent_hook_scanner_t *s, unsigned char c) {
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
    s->token = TOKEN_NONE;
    return false;
  }
  return true;
}

static void finish_literal(agent_hook_scanner_t *s) {
  if (s->depth == 1 && s->field == FIELD_STOP &&
      (s->literal[0] == 't' || s->literal[0] == 'f')) {
    s->stop_hook_active = s->literal[0] == 't';
    s->valid_fields |= 1U << FIELD_STOP;
  }
  s->token = TOKEN_NONE;
}

static void begin_string(agent_hook_scanner_t *s, bool key) {
  s->token = TOKEN_STRING;
  s->key = key;
  s->text_length = 0;
  s->escaped = s->escape_next = s->overflow = false;
  s->unicode_left = s->utf8_left = 0;
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
    s->stack[s->depth++] = c == '{' ? OBJECT_FIRST : ARRAY_FIRST;
  } else if (c == 't' || c == 'f' || c == 'n') {
    s->token = TOKEN_LITERAL;
    s->literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
    s->literal_pos = 1;
  } else if (c == '-' || digit(c)) {
    s->token = TOKEN_NUMBER;
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
  if (!event || s->failed || !s->complete ||
      !(s->valid_fields & (1U << FIELD_EVENT))) {
    return false;
  }
  static const struct {
    const char *name;
    agent_event_t event;
  } MAPPING[] = {
      {"SessionStart",       AGENT_EVENT_START  },
      {"UserPromptSubmit",   AGENT_EVENT_WORKING},
      {"PreToolUse",         AGENT_EVENT_WORKING},
      {"PostToolUse",        AGENT_EVENT_WORKING},
      {"PostToolUseFailure", AGENT_EVENT_WORKING},
      {"PermissionRequest",  AGENT_EVENT_WAITING},
      {"Stop",               AGENT_EVENT_DONE   },
      {"StopFailure",        AGENT_EVENT_IDLE   },
      {"Interrupt",          AGENT_EVENT_IDLE   },
      {"SessionEnd",         AGENT_EVENT_END    }
  };
  if (strcmp(s->event, "Stop") == 0 && s->stop_present &&
      (!(s->valid_fields & (1U << FIELD_STOP)) || s->stop_hook_active)) {
    return false;
  }
  for (size_t i = 0; i < sizeof(MAPPING) / sizeof(MAPPING[0]); i++) {
    if (strcmp(s->event, MAPPING[i].name) == 0) {
      *event = MAPPING[i].event;
      return true;
    }
  }
  if (strcmp(s->event, "Notification") == 0 &&
      (s->valid_fields & (1U << FIELD_NOTIFICATION))) {
    if (strcmp(s->notification, "idle_prompt") == 0) {
      *event = AGENT_EVENT_REST;
      return true;
    }
    if (strcmp(s->notification, "permission_prompt") == 0 ||
        strcmp(s->notification, "elicitation_dialog") == 0 ||
        strcmp(s->notification, "agent_needs_input") == 0) {
      *event = AGENT_EVENT_WAITING;
      return true;
    }
  }
  return false;
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
  const char *parts[] = {agent, ":",
                         s->valid_fields & (1U << FIELD_SESSION) ? s->session_id
                                                                 : "default"};
  for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
    for (const unsigned char *p = (const unsigned char *)parts[i]; *p; p++) {
      hash ^= *p;
      hash *= UINT64_C(1099511628211);
    }
  }
  return hash ? hash : 1;
}

int agent_hook_parse_stat(const char *line, char *comm, size_t capacity,
                          pid_t *parent) {
  if (!line || !comm || !capacity || !parent) {
    return -1;
  }
  const char *start = strchr(line, '('), *end = strrchr(line, ')');
  if (!start || !end || end <= start || (size_t)(end - start) > capacity) {
    return -1;
  }
  const char *p = end + 1;
  if (*p++ != ' ' || !*p++ || *p++ != ' ' || !digit((unsigned char)*p)) {
    return -1;
  }
  errno = 0;
  char *tail;
  long value = strtol(p, &tail, 10);
  if (errno || (*tail && *tail != ' ') || value < 0 || value > INT_MAX) {
    return -1;
  }
  size_t length = (size_t)(end - start - 1);
  memcpy(comm, start + 1, length);
  comm[length] = '\0';
  *parent = (pid_t)value;
  return 0;
}

static pid_t agent_parent(void) {
  pid_t pid = getppid();
  static const char *const SHELLS[] = {"sh",   "bash", "zsh",    "dash",
                                       "fish", "env",  "timeout"};
  for (int depth = 0; depth < 8 && pid > 1; depth++) {
    char path[64], stat[512], comm[64];
    snprintf(path, sizeof(path), "/proc/%jd/stat", (intmax_t)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return 0;
    }
    ssize_t length = read(fd, stat, sizeof(stat) - 1);
    close(fd);
    if (length <= 0) {
      return 0;
    }
    stat[length] = '\0';
    pid_t parent;
    if (agent_hook_parse_stat(stat, comm, sizeof(comm), &parent) < 0) {
      return 0;
    }
    bool shell = false;
    for (size_t i = 0; i < sizeof(SHELLS) / sizeof(SHELLS[0]); i++) {
      shell |= strcmp(comm, SHELLS[i]) == 0;
    }
    if (!shell) {
      return pid;
    }
    pid = parent;
  }
  return 0;
}

static void hook_timeout(int signum) {
  (void)signum;
  _exit(0);
}

int agent_hook_run(const char *agent) {
  if (!agent_hook_valid_agent(agent)) {
    return 1;
  }
  if (isatty(STDIN_FILENO)) {
    return 0;
  }
  struct sigaction action = {.sa_handler = hook_timeout};
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGALRM, &action, NULL) < 0) {
    return 0;
  }
  alarm(2);
  agent_hook_scanner_t scanner;
  agent_hook_scan_init(&scanner);
  char buffer[4096];
  ssize_t length;
  while ((length = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
    agent_hook_scan_feed(&scanner, buffer, (size_t)length);
    if (scanner.failed) {
      return 0;
    }
  }
  agent_event_t event;
  if (length < 0 || !agent_hook_scan_finish(&scanner) ||
      !agent_hook_event(&scanner, &event)) {
    return 0;
  }
  char request[64];
  static const char *const EVENTS[] = {"idle",  "working", "waiting", "done",
                                       "start", "rest",    "end"};
  snprintf(request, sizeof(request), "ev %s %s %016" PRIx64 " %jd", agent,
           EVENTS[event], agent_hook_key(agent, &scanner),
           (intmax_t)agent_parent());
  const char *debug = getenv("BONGOCAT_HOOK_DEBUG");
  if (debug && strcmp(debug, "1") == 0) {
    fprintf(stderr, "%s\n", request);
  }
  // control_request prints its reply; hooks must never write to stdout.
  if (!freopen("/dev/null", "w", stdout)) {
    return 0;
  }
  control_request(request);
  return 0;
}

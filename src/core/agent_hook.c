#define _POSIX_C_SOURCE 200809L
#include "core/agent_hook.h"

#include "core/control.h"
#include "utils/utf8.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
static bool digit(unsigned char c) {
  return c >= '0' && c <= '9';
}
static bool hex(unsigned char c) {
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
  bool valid =
      !s->overflow &&
      (!s->escaped || (!s->key && (s->field == HOOK_FIELD_CWD ||
                                   s->field == HOOK_FIELD_TRANSCRIPT)));
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
    for (const unsigned char *p = (const unsigned char *)parts[i]; *p; p++) {
      hash ^= *p;
      hash *= UINT64_C(1099511628211);
    }
  }
  return hash ? hash : 1;
}

// Decode JSON escapes only for cwd. Other captured fields keep their strict
// rules.
static bool decode_path(const char *raw, char *decoded, size_t capacity) {
  size_t used = 0;
  while (*raw) {
    uint32_t cp;
    if (*raw != '\\') {
      size_t n = utf8_decode(raw, &cp);
      if (!n || used + n >= capacity)
        return false;
      memcpy(decoded + used, raw, n);
      used += n;
      raw += n;
      continue;
    }
    raw++;
    if (*raw == 'u') {
      raw++;
      cp = 0;
      for (int i = 0; i < 4; i++) {
        if (!hex((unsigned char)*raw))
          return false;
        unsigned c = (unsigned char)*raw++;
        cp = (cp << 4) + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
      }
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (raw[0] != '\\' || raw[1] != 'u')
          return false;
        raw += 2;
        uint32_t low = 0;
        for (int i = 0; i < 4; i++) {
          if (!hex((unsigned char)*raw))
            return false;
          unsigned c = (unsigned char)*raw++;
          low = (low << 4) + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
        }
        if (low < 0xdc00 || low > 0xdfff)
          return false;
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
      } else if (cp >= 0xdc00 && cp <= 0xdfff)
        return false;
    } else {
      char c = *raw++;
      static const char escapes[] = "bfnrt";
      const char *esc = strchr(escapes, c);
      cp = esc ? (uint32_t)(unsigned char)"\b\f\n\r\t"[esc - escapes]
               : (unsigned char)c;
    }
    if (cp == 0)
      return false;
    unsigned n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (used + n >= capacity)
      return false;
    if (n == 1)
      decoded[used] = (char)cp;
    else {
      decoded[used] = (char)((n == 2   ? 0xc0
                              : n == 3 ? 0xe0
                                       : 0xf0) |
                             (cp >> (6 * (n - 1))));
      for (unsigned i = 1; i < n; i++)
        decoded[used + i] = (char)(0x80 | ((cp >> (6 * (n - i - 1))) & 63));
    }
    used += n;
  }
  decoded[used] = '\0';
  return true;
}
bool agent_hook_transcript(const agent_hook_scanner_t *s,
                           char path[AGENT_TRANSCRIPT_PATH_MAX + 1]) {
  return (s->valid_fields & (1U << HOOK_FIELD_TRANSCRIPT)) &&
         decode_path(s->transcript, path, AGENT_TRANSCRIPT_PATH_MAX + 1) &&
         path[0] == '/' && !strchr(path, '\n') && !strchr(path, '\r');
}
static bool copy_label(const char *base, char name[41]) {
  if (!base || !base[0] || !strcmp(base, ".") || !strcmp(base, ".."))
    return false;
  size_t used = 0;
  while (*base) {
    uint32_t cp;
    size_t n = utf8_decode(base, &cp);
    if (!n)
      return false;
    if (!utf8_control(cp)) {
      if (used + n > 40)
        break;
      memcpy(name + used, base, n);
      used += n;
    }
    base += n;
  }
  name[used] = '\0';
  return used > 0;
}

static bool git_here(const char *dir) {
  char git[PATH_MAX];
  int wrote = snprintf(git, sizeof(git), "%s/.git", dir);
  if (wrote < 0 || (size_t)wrote >= sizeof(git))
    return false;
  struct stat st;
  return lstat(git, &st) == 0;
}

static const char *final_component(const char *dir) {
  const char *base = strrchr(dir, '/');
  return base ? base + 1 : dir;
}

bool agent_hook_place_name(const char *dir, char name[41]) {
  name[0] = '\0';
  if (!dir || dir[0] != '/')
    return false;
  char path[PATH_MAX];
  size_t length = strlen(dir);
  if (!length || length >= sizeof(path))
    return false;
  memcpy(path, dir, length + 1);
  while (length > 1 && path[length - 1] == '/')
    path[--length] = '\0';
  char start[PATH_MAX];
  memcpy(start, path, length + 1);
  for (int depth = 0; depth < 16; depth++) {
    if (git_here(path))
      return copy_label(final_component(path), name);
    char *slash = strrchr(path, '/');
    if (!slash || slash == path)
      break;
    *slash = '\0';
  }
  return copy_label(final_component(start), name);
}

bool agent_hook_name(const agent_hook_scanner_t *s, char name[41]) {
  name[0] = '\0';
  if (!(s->valid_fields & (1U << HOOK_FIELD_CWD)))
    return false;
  char cwd[256];
  if (!decode_path(s->cwd, cwd, sizeof(cwd)) || cwd[0] != '/')
    return false;
  return agent_hook_place_name(cwd, name);
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

int agent_hook_stat_tty(const char *line, unsigned long *tty_nr) {
  if (!line || !tty_nr)
    return -1;
  const char *end = strrchr(line, ')');
  if (!end || end[1] != ' ')
    return -1;
  const char *p = end + 2;
  while (*p && *p != ' ')
    p++;
  for (int field = 0; field < 4; field++) {
    if (*p++ != ' ' || !digit((unsigned char)*p))
      return -1;
    errno = 0;
    char *tail = NULL;
    unsigned long value = strtoul(p, &tail, 10);
    if (errno || !tail || tail == p || (*tail && *tail != ' ' && *tail != '\n'))
      return -1;
    if (field == 3) {
      *tty_nr = value;
      return 0;
    }
    p = tail;
  }
  return -1;
}

int agent_process_tty(pid_t pid) {
  if (pid <= 0)
    return -1;
  char path[64];
  snprintf(path, sizeof(path), "/proc/%jd/stat", (intmax_t)pid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  char stat[512];
  ssize_t length = read(fd, stat, sizeof(stat) - 1);
  close(fd);
  if (length <= 0)
    return -1;
  stat[length] = '\0';
  unsigned long tty = 0;
  if (agent_hook_stat_tty(stat, &tty) < 0)
    return -1;
  return tty != 0;
}

pid_t agent_hook_front_process(const char *root, const char *comm,
                               const char *cwd) {
  if (!root || !comm || !cwd || cwd[0] != '/')
    return 0;
  DIR *dir = opendir(root);
  if (!dir)
    return 0;
  pid_t found = 0;
  int seen = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) && seen < 4096) {
    char *tail;
    errno = 0;
    long value = strtol(entry->d_name, &tail, 10);
    if (errno || *tail || value <= 1 || value > INT_MAX)
      continue;
    seen++;
    char path[320], stat[512], name[64], where[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%ld/stat", root, value) >=
        (int)sizeof(path))
      continue;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      continue;
    ssize_t length = read(fd, stat, sizeof(stat) - 1);
    close(fd);
    if (length <= 0)
      continue;
    stat[length] = '\0';
    pid_t parent;
    unsigned long tty = 0;
    if (agent_hook_parse_stat(stat, name, sizeof(name), &parent) < 0 ||
        strcmp(name, comm) || agent_hook_stat_tty(stat, &tty) < 0 || !tty)
      continue;
    snprintf(path, sizeof(path), "%s/%ld/cwd", root, value);
    ssize_t n = readlink(path, where, sizeof(where) - 1);
    if (n <= 0)
      continue;
    where[n] = '\0';
    if (strcmp(where, cwd))
      continue;
    if (found) {
      // Two candidates: a guess could put the sign on the wrong terminal.
      found = 0;
      break;
    }
    found = (pid_t)value;
  }
  closedir(dir);
  return found;
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

int agent_hook_run(const char *agent, const char *event_name) {
  return agent_hook_run_adapter(agent, event_name, agent_adapter_find(agent));
}

int agent_hook_run_adapter(const char *agent, const char *event_name,
                           const agent_adapter_t *adapter) {
  if (!agent_hook_valid_agent(agent)) {
    return 1;
  }
  // Emit the policy response before parsing, including on timeout/failure.
  if (adapter->json_stdout) {
    signal(SIGPIPE, SIG_IGN);
    ssize_t ignored = write(STDOUT_FILENO, "{}\n", 3);
    (void)ignored;
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
  agent_hook_scan_adapter(&scanner, adapter);
  char buffer[4096];
  ssize_t length;
  while ((length = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
    agent_hook_scan_feed(&scanner, buffer, (size_t)length);
    if (scanner.failed) {
      return 0;
    }
  }
  agent_event_t event;
  bool metadata = false;
  if (length < 0 || !agent_hook_scan_finish(&scanner) ||
      !agent_hook_event_override(&scanner, event_name, &event, &metadata)) {
    return 0;
  }
  pid_t pid = adapter->no_pid ? 0 : agent_parent();
  if (adapter->explicit_pid) {
    // The bridge runs in Pi itself. Never watch an arbitrary supplied PID or
    // revive work after that parent has exited while a hook was queued.
    if (!(scanner.valid_fields & (1U << HOOK_FIELD_PID)) ||
        scanner.pid != pid) {
      if (event != AGENT_EVENT_END)
        return 0;
      pid = 0;
    }
  }
  // A daemon with no controlling terminal must not keep a sign forever.
  if (pid > 0 && agent_process_tty(pid) == 0) {
    pid = 0;
    // The hook ran in the agent's background server. The session still has
    // a terminal if exactly one of its processes works in the same place.
    char cwd[256];
    if (adapter->process_name &&
        (scanner.valid_fields & (1U << HOOK_FIELD_CWD)) &&
        decode_path(scanner.cwd, cwd, sizeof(cwd)))
      pid = agent_hook_front_process("/proc", adapter->process_name, cwd);
  }
  char request[64];
  static const char *const EVENTS[] = {"idle", "working",   "waiting",
                                       "done", "start",     "rest",
                                       "end",  "interrupt", "fail"};
  snprintf(request, sizeof(request), "ev %s %s %016" PRIx64 " %jd", agent,
           EVENTS[event], agent_hook_key(agent, &scanner), (intmax_t)pid);
  const char *debug = getenv("HERDCAT_HOOK_DEBUG");
  if (debug && strcmp(debug, "1") == 0) {
    fprintf(stderr, "%s\n", request);
  }
  // control_request prints its reply; hooks must never write to stdout.
  if (!freopen("/dev/null", "w", stdout)) {
    return 0;
  }
  int sent = control_request(request);
  if (!sent && metadata) {
    if (!strcmp(agent, adapter->name) &&
        (adapter->interrupt_source == AGENT_SIGNAL_TRANSCRIPT ||
         adapter->error_source == AGENT_SIGNAL_TRANSCRIPT)) {
      char path[AGENT_TRANSCRIPT_PATH_MAX + 1];
      if (agent_hook_transcript(&scanner, path)) {
        char message[AGENT_TRANSCRIPT_PATH_MAX + 23];
        snprintf(message, sizeof(message), "path %016" PRIx64 " %s",
                 agent_hook_key(agent, &scanner), path);
        control_request(message);
      }
    }
    char name[41];
    if (agent_hook_name(&scanner, name)) {
      snprintf(request, sizeof(request), "name %016" PRIx64 " %s",
               agent_hook_key(agent, &scanner), name);
      if (debug && !strcmp(debug, "1"))
        fprintf(stderr, "%s\n", request);
      control_request(request);
    }
  }
  return 0;
}

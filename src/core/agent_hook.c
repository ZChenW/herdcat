#define _POSIX_C_SOURCE 200809L
#include "core/agent_hook.h"

#include "agent_hook_internal.h"
#include "core/agent_adapters.h"
#include "core/agent_sessions.h"
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "core/control.h"
#include "platform/agent_terminal.h"
#include "utils/path_wire.h"
#include "utils/utf8.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Decode JSON escapes for paths and title text. Identity and event fields
// retain their strict, unescaped rules.
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
bool agent_hook_title(const agent_hook_scanner_t *s,
                      char out[AGENT_TITLE_MAX + 1]) {
  out[0] = 0;
  if (!(s->valid_fields & (1U << HOOK_FIELD_TITLE)) ||
      !decode_path(s->title, out, AGENT_TITLE_MAX + 1) ||
      !utf8_label_valid(out, AGENT_TITLE_MAX) ||
      !strncmp(out, "New session - ", 14)) {
    out[0] = 0;
    return false;
  }
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
  if (*p != ' ')
    return -1;
  p++;
  if (!*p)
    return -1;
  p++;
  if (*p != ' ')
    return -1;
  p++;
  if (!digit((unsigned char)*p)) {
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
    if (*p != ' ')
      return -1;
    p++;
    if (!digit((unsigned char)*p))
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
  while ((entry = readdir(dir))) {
    if (seen >= 4096)
      break;
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
    // readlink() returns at most sizeof(where) - 1 bytes on success.
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
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

static pid_t owner_number(const char *text) {
  if (!text || !*text || strspn(text, "0123456789") != strlen(text))
    return 0;
  errno = 0;
  unsigned long n = strtoul(text, NULL, 10);
  return !errno && n > 1 && n <= 4194304 ? (pid_t)n : 0;
}
pid_t agent_hook_owner_pid(const char *root, pid_t pid) {
  for (size_t i = 0; i < agent_adapter_count(); i++) {
    const char *name = agent_adapter_at(i)->owner_pid_env;
    if (!name)
      continue;
    const char *value = getenv(name);
    // A present but invalid value must not be repaired from another source.
    if (value)
      return owner_number(value);
    if (!root || pid <= 1)
      continue;
    char path[4096], data[65536];
    int n = snprintf(path, sizeof(path), "%s/%jd/environ", root, (intmax_t)pid);
    if (n < 0 || (size_t)n >= sizeof(path))
      continue;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
      continue;
    ssize_t got = read(fd, data, sizeof(data));
    close(fd);
    for (size_t at = 0; got > 0 && at < (size_t)got;) {
      char *end = memchr(data + at, 0, (size_t)got - at);
      if (!end)
        break;
      size_t size = (end - data - at), length = strlen(name);
      if (size > length && !memcmp(data + at, name, length) &&
          data[at + length] == '=')
        return owner_number(data + at + length + 1);
      at += size + 1;
    }
  }
  return 0;
}

static pid_t agent_parent(void) {
  pid_t pid = getppid();
  static const char *const shells[] = {"sh",   "bash", "zsh",    "dash",
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
    for (size_t i = 0; i < sizeof(shells) / sizeof(shells[0]); i++) {
      shell |= strcmp(comm, shells[i]) == 0;
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

static bool title_path(const char *agent, const agent_hook_scanner_t *scanner,
                       char path[AGENT_TRANSCRIPT_PATH_MAX + 1]) {
  if (strcmp(agent, "grok"))
    return agent_hook_transcript(scanner, path);
  const char *home = getenv("HOME"), *base = getenv("GROK_HOME");
  char cwd[256], encoded[768], root[AGENT_TRANSCRIPT_PATH_MAX + 1];
  if (!home || !agent_session_id_valid(scanner->session_id) ||
      !(scanner->valid_fields & (1U << HOOK_FIELD_CWD)) ||
      !decode_path(scanner->cwd, cwd, sizeof(cwd)))
    return false;
  size_t used = 0;
  const char hex[] = "0123456789ABCDEF";
  for (const unsigned char *p = (const unsigned char *)cwd; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9') || strchr("-_.~", *p)) {
      encoded[used++] = (char)*p;
    } else {
      encoded[used++] = '%';
      encoded[used++] = hex[*p >> 4];
      encoded[used++] = hex[*p & 15];
    }
  }
  encoded[used] = 0;
  int n = base && *base ? snprintf(root, sizeof(root), "%s", base)
                        : snprintf(root, sizeof(root), "%s/.grok", home);
  if (n < 0 || (size_t)n >= sizeof(root))
    return false;
  n = snprintf(path, AGENT_TRANSCRIPT_PATH_MAX + 1,
               "%s/sessions/%s/%s/summary.json", root, encoded,
               scanner->session_id);
  return n > 0 && n <= AGENT_TRANSCRIPT_PATH_MAX;
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
  // The bridge runs in Pi itself. Never watch an arbitrary supplied PID or
  // revive work after that parent has exited while a hook was queued.
  if (adapter->explicit_pid &&
      (!(scanner.valid_fields & (1U << HOOK_FIELD_PID)) ||
       scanner.pid != pid)) {
    if (event != AGENT_EVENT_END)
      return 0;
    pid = 0;
  }
  pid_t candidate = pid;
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
  char request[96];
  static const char *const events[] = {"idle", "working",   "waiting",
                                       "done", "start",     "rest",
                                       "end",  "interrupt", "fail"};
  snprintf(request, sizeof(request), "ev %s %s %016" PRIx64 " %jd", agent,
           events[event], agent_hook_key(agent, &scanner), (intmax_t)pid);
  pid_t owner = agent_hook_owner_pid("/proc", candidate);
  if (candidate > 1 && (candidate != pid || owner > 1)) {
    size_t used = strlen(request);
    snprintf(request + used, sizeof(request) - used, " %jd %d",
             (intmax_t)candidate, metadata ? 1 : 0);
    if (owner > 1) {
      used = strlen(request);
      snprintf(request + used, sizeof(request) - used, " %jd", (intmax_t)owner);
    }
  }
  const char *debug = getenv("HERDCAT_HOOK_DEBUG");
  if (debug && strcmp(debug, "1") == 0) {
    fprintf(stderr, "%s\n", request);
  }
  // control_request prints its reply; hooks must never write to stdout.
  if (!freopen("/dev/null", "w", stdout)) {
    return 0;
  }
  int sent = control_request(request);
  // The raw id is only needed to look a title up, so it travels with the
  // events that can change one, not with every tool call.
  if (!sent && (metadata || event == AGENT_EVENT_DONE) &&
      (scanner.valid_fields & (1U << HOOK_FIELD_SESSION)) &&
      agent_session_id_valid(scanner.session_id)) {
    char message[AGENT_SESSION_ID_MAX + 22];
    snprintf(message, sizeof(message), "sid %016" PRIx64 " %.64s",
             agent_hook_key(agent, &scanner), scanner.session_id);
    control_request(message);
  }
  if (!sent && pid > 1 && event != AGENT_EVENT_END) {
    agent_terminal_t terminal;
    char message[384];
    if (agent_terminal_environment(&terminal) &&
        agent_terminal_message(message, sizeof(message),
                               agent_hook_key(agent, &scanner), &terminal))
      control_request(message);
  }
  if (!sent && (metadata || event == AGENT_EVENT_DONE)) {
    if (!strcmp(agent, adapter->name) &&
        (adapter->interrupt_source == AGENT_SIGNAL_TRANSCRIPT ||
         adapter->error_source == AGENT_SIGNAL_TRANSCRIPT ||
         !strcmp(agent, "pi") || !strcmp(agent, "grok"))) {
      char path[AGENT_TRANSCRIPT_PATH_MAX + 1];
      if (title_path(agent, &scanner, path)) {
        char message[AGENT_TRANSCRIPT_PATH_MAX + 23];
        snprintf(message, sizeof(message), "path %016" PRIx64 " %s",
                 agent_hook_key(agent, &scanner), path);
        control_request(message);
      }
    }
    char name[41], cwd[AGENT_CWD_MAX + 1], encoded[AGENT_CWD_MAX * 2 + 1];
    if (agent_hook_name(&scanner, name) &&
        decode_path(scanner.cwd, cwd, sizeof(cwd))) {
      char message[576];
      path_hex(cwd, encoded);
      snprintf(message, sizeof(message), "cwd %016" PRIx64 " %s %s",
               agent_hook_key(agent, &scanner), encoded, name);
      control_request(message);
    }
  }
  if (!sent) {
    char title[AGENT_TITLE_MAX + 1];
    if (event != AGENT_EVENT_END && agent_hook_title(&scanner, title)) {
      char message[AGENT_TITLE_MAX + 22];
      snprintf(message, sizeof(message), "ttl %016" PRIx64 " %s",
               agent_hook_key(agent, &scanner), title);
      control_request(message);
    }
    char prompt[AGENT_TITLE_MAX + 1];
    if (agent_hook_prompt(&scanner, event_name, prompt)) {
      char message[AGENT_TITLE_MAX + 22];
      snprintf(message, sizeof(message), "ask %016" PRIx64 " %s",
               agent_hook_key(agent, &scanner), prompt);
      control_request(message);
    }
  }
  return 0;
}

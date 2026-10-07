#define _GNU_SOURCE
#include "platform/session_store.h"

#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "platform/agent_terminal.h"
#include "platform/agent_watch.h"
#include "platform/transcript_watch.h"
#include "utils/error.h"
#include "utils/path_wire.h"

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

#define STORE_DELAY_MS 1000
#define STORE_LINE_MAX 2304
#define STORE_FILE_MAX (AGENT_SESSIONS_MAX * STORE_LINE_MAX)

static uint64_t seen_generation;
static int64_t due_ms;
static bool dirty;
static bool warned;

static bool pid_alive(pid_t pid) {
  if (pid <= 0)
    return false;
  if (kill(pid, 0) == 0)
    return true;
  return errno == EPERM;
}

static void warn_once(const char *text) {
  if (warned)
    return;
  warned = true;
  herdcat_log_warning("%s", text);
}

static int open_tree(const char *path, bool create_leaf) {
  int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  const char *cursor = path;
  if (cursor && *cursor == '/')
    cursor++;
  while (dir >= 0 && cursor && *cursor) {
    const char *slash = strchr(cursor, '/');
    size_t len = slash ? (size_t)(slash - cursor) : strlen(cursor);
    bool leaf = !slash || slash[1] == '\0';
    if (!len || len >= 256 || (len == 1 && cursor[0] == '.') ||
        (len == 2 && cursor[0] == '.' && cursor[1] == '.')) {
      close(dir);
      errno = EINVAL;
      return -1;
    }
    char part[256];
    memcpy(part, cursor, len);
    part[len] = '\0';
    int next =
        openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create_leaf && leaf &&
        (mkdirat(dir, part, 0700) == 0 || errno == EEXIST)) {
      next = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    int saved = errno;
    close(dir);
    dir = next;
    errno = saved;
    if (!slash)
      break;
    cursor = slash + 1;
  }
  if (dir >= 0) {
    struct stat st;
    if (fstat(dir, &st) || st.st_uid != getuid() || !S_ISDIR(st.st_mode) ||
        (create_leaf && fchmod(dir, 0700))) {
      int saved = errno;
      close(dir);
      errno = saved ? saved : EACCES;
      return -1;
    }
  }
  return dir;
}

static int runtime_dir(bool create) {
  const char *base = getenv("XDG_RUNTIME_DIR");
  char path[PATH_MAX];
  if (!base || base[0] != '/') {
    errno = EINVAL;
    return -1;
  }
  int length = snprintf(path, sizeof(path), "%s/herdcat", base);
  if (length < 0 || (size_t)length >= sizeof(path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return open_tree(path, create);
}

static bool parse_u64(const char *text, uint64_t *out) {
  if (!text || strlen(text) != 16)
    return false;
  char *end = NULL;
  errno = 0;
  unsigned long long value = strtoull(text, &end, 16);
  if (errno || !end || *end || !value)
    return false;
  *out = (uint64_t)value;
  return true;
}

static bool parse_i64(const char *text, int64_t *out) {
  if (!text || !*text || *text == '-')
    return false;
  char *end = NULL;
  errno = 0;
  long long value = strtoll(text, &end, 10);
  if (errno || !end || *end || value < 0)
    return false;
  *out = (int64_t)value;
  return true;
}

static bool parse_pid(const char *text, pid_t *out) {
  int64_t value;
  if (!parse_i64(text, &value) || value > 4194304)
    return false;
  *out = (pid_t)value;
  return true;
}

static bool parse_line(char *line, agent_session_record_t *out) {
  bool version4 = !strncmp(line, "4 ", 2);
  bool version3 = version4 || !strncmp(line, "3 ", 2);
  bool version2 = version3 || !strncmp(line, "2 ", 2);
  if (!version2 && strncmp(line, "1 ", 2))
    return false;
  char *tab = strchr(line, '\t');
  if (!tab)
    return false;
  *tab = '\0';
  char *title = "", *id = "", *number = "0", *temporary = "0";
  if (version2) {
    title = strchr(tab + 1, '\t');
    if (!title)
      return false;
    *title++ = 0;
    id = strchr(title, '\t');
    if (!id)
      return false;
    *id++ = 0;
    number = strchr(id, '\t');
    if (!number)
      return false;
    *number++ = 0;
    if (version3) {
      temporary = strchr(number, '\t');
      if (!temporary)
        return false;
      *temporary++ = 0;
    }
  }
  char *cwd = "";
  if (version4) {
    cwd = strchr(temporary, '\t');
    if (!cwd)
      return false;
    *cwd++ = 0;
  }
  if (strchr(tab + 1, '\t') || strlen(tab + 1) > AGENT_TRANSCRIPT_PATH_MAX ||
      strlen(title) > AGENT_TITLE_MAX || strlen(id) > AGENT_SESSION_ID_MAX)
    return false;
  char *save = NULL;
  char *key = strtok_r(line + 2, " ", &save);
  char *agent = strtok_r(NULL, " ", &save);
  char *pid = strtok_r(NULL, " ", &save);
  char *state = strtok_r(NULL, " ", &save);
  char *unread = strtok_r(NULL, " ", &save);
  char *updated = strtok_r(NULL, " ", &save);
  char *name = save;
  agent_session_record_t row = {0};
  if (!parse_u64(key, &row.key) || !agent || strlen(agent) > AGENT_NAME_MAX ||
      !parse_pid(pid, &row.pid) || agent_state_parse(state, &row.state) ||
      !unread || (strcmp(unread, "0") && strcmp(unread, "1")) ||
      !parse_i64(updated, &row.updated_ms) || !name || !*name ||
      strlen(name) >= sizeof(row.name))
    return false;
  memcpy(row.agent, agent, strlen(agent) + 1);
  memcpy(row.name, name, strlen(name) + 1);
  if (tab[1])
    memcpy(row.transcript, tab + 1, strlen(tab + 1) + 1);
  // Validate and discard the retired name-number field in v2/v3 records.
  int64_t reserved;
  if (!parse_i64(number, &reserved) || reserved > UINT_MAX)
    return false;
  if (strcmp(temporary, "0") && strcmp(temporary, "1"))
    return false;
  row.title_temporary = temporary[0] == '1';
  if (*cwd && !path_unhex(cwd, row.start_cwd))
    return false;
  snprintf(row.title, sizeof(row.title), "%s", title);
  snprintf(row.session_id, sizeof(row.session_id), "%s", id);
  row.unread = unread[0] == '1';
  *out = row;
  return true;
}

static void note_terminal(uint64_t key, pid_t pid) {
  uint64_t window = 0;
  char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
  pid_t kitty_pid = 0;
  if (agent_terminal_lookup("/proc", pid, &window, listen, sizeof(listen),
                            &kitty_pid))
    agent_sessions_set_kitty(key, kitty_pid, window, listen);
  agent_terminal_t terminal;
  if (agent_terminal_lookup_all("/proc", pid, &terminal))
    agent_sessions_set_terminal(key, &terminal);
}

static void remember_path(const agent_session_record_t *row, int64_t now_ms) {
  if (row->transcript[0])
    transcript_watch_path(row->key, row->transcript, now_ms);
}

static void rearm(void) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++) {
    if (views[i].pid > 0) {
      agent_sessions_set_watched(views[i].key,
                                 agent_watch_add(views[i].pid) == 0);
    }
  }
}

static int read_file(int dir, int64_t now_ms, int done_timeout_s) {
  int fd =
      openat(dir, "sessions", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    return errno == ENOENT ? 0 : -1;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
      st.st_size > STORE_FILE_MAX) {
    close(fd);
    errno = EINVAL;
    return -1;
  }
  char contents[STORE_FILE_MAX + 1];
  ssize_t bytes = read(fd, contents, sizeof(contents));
  close(fd);
  if (bytes < 0 || (size_t)bytes == sizeof(contents) ||
      memchr(contents, '\0', (size_t)bytes)) {
    errno = EIO;
    return -1;
  }
  contents[bytes] = '\0';
  for (char *line = contents, *next; line; line = next) {
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    if (!*line)
      continue;
    agent_session_record_t row;
    if (strlen(line) >= STORE_LINE_MAX || !parse_line(line, &row)) {
      warn_once("Skipping malformed agent session line");
      continue;
    }
    if (row.pid > 0 && !pid_alive(row.pid))
      continue;
    // A restored daemon with no terminal would stay up until it exits.
    if (row.pid > 0 && agent_process_tty(row.pid) == 0)
      continue;
    if (agent_sessions_restore(&row, now_ms, done_timeout_s) < 0)
      warn_once("Skipping agent session that could not be restored");
    else if (row.pid > 0)
      note_terminal(row.key, row.pid);
  }
  agent_session_record_t rows[AGENT_SESSIONS_MAX];
  int count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    remember_path(&rows[i], now_ms);
  rearm();
  return 0;
}

static int write_file(int dir) {
  static unsigned sequence;
  char temp[64];
  int fd = -1;
  for (int i = 0; i < 16 && fd < 0; i++) {
    snprintf(temp, sizeof(temp), ".sessions.%ld.%u", (long)getpid(),
             sequence++);
    fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                0600);
    if (fd < 0 && errno != EEXIST)
      break;
  }
  if (fd < 0)
    return -1;
  int result = fchmod(fd, 0600);
  agent_session_record_t rows[AGENT_SESSIONS_MAX];
  int count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  for (int i = 0; !result && i < count; i++) {
    const char *path = rows[i].transcript;
    if (strchr(path, '\n') || strchr(path, '\t'))
      path = "";
    char cwd[AGENT_CWD_MAX * 2 + 1];
    path_hex(rows[i].start_cwd, cwd);
    if (dprintf(fd,
                "4 %016" PRIx64 " %s %ld %s %d %" PRId64
                " %s\t%s\t%s\t%s\t0\t%d\t%s\n",
                rows[i].key, rows[i].agent, (long)rows[i].pid,
                agent_state_name(rows[i].state), rows[i].unread ? 1 : 0,
                rows[i].updated_ms, rows[i].name, path, rows[i].title,
                rows[i].session_id, rows[i].title_temporary ? 1 : 0, cwd) < 0)
      result = -1;
  }
  if (!result && fsync(fd))
    result = -1;
  if (close(fd))
    result = -1;
  if (!result)
    result = renameat(dir, temp, dir, "sessions");
  if (result)
    unlinkat(dir, temp, 0);
  return result;
}

void session_store_load(int64_t now_ms, int done_timeout_s) {
  int dir = runtime_dir(false);
  if (dir < 0) {
    if (errno != ENOENT && errno != EINVAL)
      warn_once("Cannot read saved agent sessions");
    seen_generation = agent_sessions_generation();
    dirty = false;
    return;
  }
  if (read_file(dir, now_ms, done_timeout_s))
    warn_once("Cannot read saved agent sessions");
  close(dir);
  seen_generation = agent_sessions_generation();
  dirty = false;
}

static void note(uint64_t generation, int64_t now_ms) {
  if (generation == seen_generation)
    return;
  seen_generation = generation;
  if (!dirty) {
    dirty = true;
    due_ms = now_ms + STORE_DELAY_MS;
  }
}

void session_store_flush(uint64_t generation, int64_t now_ms, bool force) {
  note(generation, now_ms);
  if (!dirty || (!force && now_ms < due_ms))
    return;
  int dir = runtime_dir(true);
  if (dir < 0) {
    // A missing or refused runtime dir cannot recover by polling.
    warn_once("Cannot save agent sessions");
    dirty = false;
    return;
  }
  if (write_file(dir)) {
    close(dir);
    warn_once("Cannot save agent sessions");
    due_ms = now_ms + STORE_DELAY_MS;
    return;
  }
  close(dir);
  dirty = false;
}

int session_store_timeout(int64_t now_ms) {
  if (!dirty)
    return -1;
  if (now_ms >= due_ms)
    return 0;
  int64_t left = due_ms - now_ms;
  return left > INT_MAX ? INT_MAX : (int)left;
}

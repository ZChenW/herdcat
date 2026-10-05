#define _GNU_SOURCE
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "platform/agent_watch.h"
#include "platform/session_store.h"
#include "platform/transcript_watch.h"
#include "test_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char home[] = "/tmp/herdcat-ss-home-XXXXXX";
static char runtime[] = "/tmp/herdcat-ss-run-XXXXXX";

static void apply(uint64_t key, const char *agent, agent_event_t event,
                  pid_t pid) {
  TEST_ASSERT(agent_sessions_apply(key, agent, event, pid, 1000, 5, NULL) == 0);
}

static pid_t spawn_paused(bool with_tty, int *master) {
  char *name = NULL;
  *master = -1;
  if (with_tty) {
    *master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    TEST_ASSERT(*master >= 0 && grantpt(*master) == 0 &&
                unlockpt(*master) == 0);
    name = ptsname(*master);
    TEST_ASSERT(name);
  }
  int ready[2];
  TEST_ASSERT(pipe2(ready, O_CLOEXEC) == 0);
  int master_fd = *master;
  pid_t pid = fork();
  TEST_ASSERT(pid >= 0);
  if (pid == 0) {
    close(ready[0]);
    if (master_fd >= 0)
      close(master_fd);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1)
      _exit(0);
    if (setsid() < 0)
      _exit(1);
    if (with_tty) {
      int slave = open(name, O_RDWR);
      if (slave < 0 || ioctl(slave, TIOCSCTTY, 0) < 0)
        _exit(1);
    }
    char ok = 1;
    if (write(ready[1], &ok, 1) != 1)
      _exit(1);
    for (;;)
      pause();
  }
  close(ready[1]);
  char ok = 0;
  TEST_ASSERT(read(ready[0], &ok, 1) == 1);
  close(ready[0]);
  return pid;
}

static void stop_child(pid_t pid, int master) {
  if (pid > 0)
    kill(pid, SIGKILL);
  if (master >= 0)
    close(master);
  if (pid > 0)
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
}

static pid_t dead_pid(void) {
  for (pid_t pid = 100000; pid < 100400; pid++) {
    if (kill(pid, 0) < 0 && errno == ESRCH)
      return pid;
  }
  return 0;
}

static void sessions_path(char *out, size_t n) {
  int wrote = snprintf(out, n, "%s/herdcat/sessions", runtime);
  TEST_ASSERT(wrote > 0 && (size_t)wrote < n);
}

static void read_sessions(char *buf, size_t n) {
  char path[160];
  sessions_path(path, sizeof(path));
  FILE *file = fopen(path, "r");
  TEST_ASSERT(file);
  size_t got = fread(buf, 1, n - 1, file);
  buf[got] = '\0';
  TEST_ASSERT(!ferror(file));
  fclose(file);
}

static const agent_session_record_t *find_key(agent_session_record_t *rows,
                                              int count, uint64_t key) {
  for (int i = 0; i < count; i++) {
    if (rows[i].key == key)
      return &rows[i];
  }
  return NULL;
}

static void cleanup_dirs(const char *jsonl) {
  char path[160];
  sessions_path(path, sizeof(path));
  unlink(path);
  snprintf(path, sizeof(path), "%s/herdcat", runtime);
  rmdir(path);
  rmdir(runtime);
  unlink(jsonl);
  rmdir(home);
}

int main(void) {
  TEST_ASSERT(mkdtemp(home) && mkdtemp(runtime));
  TEST_ASSERT(setenv("HOME", home, 1) == 0);
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
  TEST_ASSERT(strstr(getenv("XDG_RUNTIME_DIR"), "herdcat-ss-run"));
  char jsonl[128];
  snprintf(jsonl, sizeof(jsonl), "%s/turn.jsonl", home);
  int fd = open(jsonl, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
  TEST_ASSERT(fd >= 0);
  close(fd);
  TEST_ASSERT(agent_watch_init() == 0);

  int live_master = -1, daemon_master = -1;
  pid_t live = spawn_paused(true, &live_master);
  pid_t daemon = spawn_paused(false, &daemon_master);
  pid_t dead = dead_pid();
  TEST_ASSERT(dead > 0 && live > 1 && daemon > 1 && dead != live &&
              dead != daemon && live != daemon);
  TEST_ASSERT(agent_process_tty(live) == 1);
  TEST_ASSERT(agent_process_tty(daemon) == 0);

  agent_sessions_reset();
  apply(1, "claude", AGENT_EVENT_WORKING, live);
  TEST_ASSERT(agent_sessions_set_name(1, "proj") == 0);
  transcript_watch_sync(true, 1000);
  transcript_watch_path(1, jsonl, 1000);
  apply(2, "codex", AGENT_EVENT_DONE, live);
  TEST_ASSERT(agent_sessions_set_name(2, "other") == 0);
  apply(3, "kimi", AGENT_EVENT_START, dead);
  apply(4, "opencode", AGENT_EVENT_START, 0);
  TEST_ASSERT(agent_sessions_set_name(4, "svc") == 0);
  apply(5, "grok", AGENT_EVENT_START, daemon);
  TEST_ASSERT(agent_sessions_set_name(5, "daemon") == 0);
  TEST_ASSERT(agent_sessions_count() == 5);

  char path[160];
  sessions_path(path, sizeof(path));
  session_store_flush(agent_sessions_generation(), 1000, false);
  TEST_ASSERT(access(path, F_OK) < 0);
  session_store_flush(agent_sessions_generation(), 2000, false);
  struct stat st;
  TEST_ASSERT(stat(path, &st) == 0);
  TEST_ASSERT(S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
  char dir[160];
  snprintf(dir, sizeof(dir), "%s/herdcat", runtime);
  TEST_ASSERT(stat(dir, &st) == 0);
  TEST_ASSERT(S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);
  char before[4096];
  read_sessions(before, sizeof(before));
  TEST_ASSERT(strstr(before, " working "));
  TEST_ASSERT(strstr(before, " done "));
  TEST_ASSERT(strstr(before, jsonl));
  TEST_ASSERT(strncmp(path, runtime, strlen(runtime)) == 0);

  TEST_ASSERT(agent_sessions_set_name(4, "svc2") == 0);
  session_store_flush(agent_sessions_generation(), 2000, false);
  char held[4096];
  read_sessions(held, sizeof(held));
  TEST_ASSERT(strcmp(before, held) == 0);
  session_store_flush(agent_sessions_generation(), 2500, true);
  read_sessions(held, sizeof(held));
  TEST_ASSERT(strstr(held, "svc2"));
  TEST_ASSERT(!strstr(held, " svc "));

  agent_sessions_reset();
  transcript_watch_cleanup();
  session_store_load(5000, 5);
  TEST_ASSERT(agent_sessions_count() == 3);
  agent_session_record_t rows[AGENT_SESSIONS_MAX];
  int count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  const agent_session_record_t *claude = find_key(rows, count, 1);
  const agent_session_record_t *codex = find_key(rows, count, 2);
  const agent_session_record_t *kimi = find_key(rows, count, 3);
  const agent_session_record_t *open = find_key(rows, count, 4);
  TEST_ASSERT(claude && claude->state == AGENT_STATE_IDLE &&
              claude->pid == live && !claude->unread &&
              !strcmp(claude->name, "proj") &&
              !strcmp(claude->transcript, jsonl));
  TEST_ASSERT(codex && codex->state == AGENT_STATE_DONE && codex->unread &&
              codex->pid == live);
  TEST_ASSERT(!kimi);
  TEST_ASSERT(!find_key(rows, count, 5));
  TEST_ASSERT(open && open->pid == 0 && open->state == AGENT_STATE_IDLE &&
              !strcmp(open->name, "svc2"));
  TEST_ASSERT(agent_sessions_next_deadline(600) == 601000);
  TEST_ASSERT(transcript_watch_count() == 0);
  apply(1, "claude", AGENT_EVENT_WORKING, live);
  transcript_watch_sync(true, 6000);
  TEST_ASSERT(transcript_watch_count() == 1);

  agent_sessions_reset();
  transcript_watch_cleanup();
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  fprintf(file, "not a session\n");
  fprintf(file, "1 %016x claude %ld idle 0 1000 first\t\n", 0x11, (long)live);
  fprintf(file, "1 %016x claude %ld idle 0 2000 second\t\n", 0x22, (long)live);
  fclose(file);
  session_store_load(3000, 5);
  TEST_ASSERT(agent_sessions_count() == 1);
  count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  TEST_ASSERT(count == 1 && rows[0].key == 0x22 &&
              !strcmp(rows[0].name, "second") && rows[0].pid == live);

  agent_sessions_reset();
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", "relative", 1) == 0);
  apply(9, "claude", AGENT_EVENT_START, live);
  session_store_flush(agent_sessions_generation(), 4000, true);
  TEST_ASSERT(session_store_timeout(4000) == -1);
  TEST_ASSERT(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
  TEST_ASSERT(access(path, F_OK) == 0);

  transcript_watch_cleanup();
  agent_watch_cleanup();
  agent_sessions_reset();
  stop_child(live, live_master);
  stop_child(daemon, daemon_master);
  cleanup_dirs(jsonl);
  return 0;
}

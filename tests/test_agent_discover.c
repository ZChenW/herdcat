#define _GNU_SOURCE
#include "core/agent_adapters.h"
#include "core/agent_sessions.h"
#include "platform/agent_discover.h"
#include "test_helpers.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/bongocat-disc-XXXXXX";

static void rm_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st))
    return;
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
    unlink(path);
    return;
  }
  DIR *dir = opendir(path);
  if (!dir)
    return;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char child[512];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    rm_tree(child);
  }
  closedir(dir);
  rmdir(path);
}

static void add_proc_tty(const char *tree, pid_t pid, const char *comm,
                         pid_t parent, const char *base, unsigned long tty) {
  char dir[320], path[360], target[320];
  snprintf(dir, sizeof(dir), "%s/%s/%ld", root, tree, (long)pid);
  TEST_ASSERT(mkdir(dir, 0700) == 0);
  snprintf(path, sizeof(path), "%s/comm", dir);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fprintf(file, "%s\n", comm) > 0);
  fclose(file);
  snprintf(path, sizeof(path), "%s/stat", dir);
  file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fprintf(file, "%ld (%s) S %ld %ld %ld %lu\n", (long)pid, comm,
                      (long)parent, (long)pid, (long)pid, tty) > 0);
  fclose(file);
  if (!base)
    return;
  snprintf(target, sizeof(target), "%s/names/%s", root, base);
  mkdir(target, 0700);
  snprintf(path, sizeof(path), "%s/cwd", dir);
  TEST_ASSERT(symlink(target, path) == 0);
}

static void add_proc(const char *tree, pid_t pid, const char *comm,
                     pid_t parent, const char *base) {
  add_proc_tty(tree, pid, comm, parent, base, 34817);
}

static void make_tree(const char *tree) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", root, tree);
  TEST_ASSERT(mkdir(path, 0700) == 0);
}

static int scan(const char *tree, const focus_window_t *windows, size_t count,
                int max_processes, int max_new) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", root, tree);
  return agent_discover_scan(path, windows, count, max_processes, max_new, 1000,
                             5);
}

static agent_session_record_t by_pid(pid_t pid) {
  agent_session_record_t rows[AGENT_SESSIONS_MAX], none = {0};
  int count = agent_sessions_export(rows, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (rows[i].pid == pid)
      return rows[i];
  return none;
}

static void expect_idle(pid_t pid, const char *agent, const char *name) {
  agent_session_record_t row = by_pid(pid);
  TEST_ASSERT(row.pid == pid && row.state == AGENT_STATE_IDLE &&
              !strcmp(row.agent, agent) && !strcmp(row.name, name));
}

static void names(void) {
  TEST_ASSERT(!strcmp(agent_adapter_find("claude")->process_name, "claude"));
  TEST_ASSERT(!strcmp(agent_adapter_find("codex")->process_name, "codex"));
  TEST_ASSERT(!strcmp(agent_adapter_find("grok")->process_name, "grok"));
  TEST_ASSERT(!strcmp(agent_adapter_find("kimi")->process_name, "kimi"));
  TEST_ASSERT(!agent_adapter_find("opencode")->process_name);
  TEST_ASSERT(!agent_adapter_find("cursor")->process_name);
  TEST_ASSERT(!agent_adapter_find("copilot")->process_name);
  TEST_ASSERT(!agent_adapter_find("pi")->process_name);
}

static void matched(void) {
  agent_sessions_reset();
  make_tree("match");
  add_proc("match", 10, "kitty", 1, NULL);
  add_proc("match", 50, "zsh", 10, NULL);
  add_proc("match", 100, "codex", 50, "proj");
  focus_window_t window = {.id = 7, .pid = 10};
  TEST_ASSERT(scan("match", &window, 1, 100, 5) == 1);
  expect_idle(100, "codex", "proj");
  uint64_t placeholder = by_pid(100).key;
  TEST_ASSERT(placeholder && scan("match", &window, 1, 100, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_apply(0xabc, "codex", AGENT_EVENT_WORKING, 100,
                                   2000, 5, NULL) == 0);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(agent_sessions_pid(0xabc) == 100);
  TEST_ASSERT(agent_sessions_pid(placeholder) == 0);
  TEST_ASSERT(agent_sessions_resolve() == AGENT_STATE_WORKING);

  agent_sessions_reset();
  TEST_ASSERT(agent_sessions_apply(0x11, "grok", AGENT_EVENT_START, 100, 1000,
                                   5, NULL) == 0);
  TEST_ASSERT(agent_sessions_set_name(0x11, "keep") == 0);
  TEST_ASSERT(scan("match", &window, 1, 100, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == 1);
  TEST_ASSERT(!strcmp(by_pid(100).agent, "grok"));
  TEST_ASSERT(!strcmp(by_pid(100).name, "keep"));
}

static void skipped(void) {
  agent_sessions_reset();
  make_tree("skip");
  add_proc("skip", 10, "kitty", 1, NULL);
  add_proc("skip", 104, "opencode", 10, "nope");
  add_proc("skip", 105, "node-MainThread", 10, "nope");
  add_proc("skip", 106, "MainThread", 10, "nope");
  add_proc("skip", 107, "node", 10, "nope");
  add_proc("skip", 108, "cursor", 10, "nope");
  add_proc("skip", 109, "pi", 10, "nope");
  add_proc("skip", 110, "codex", 1, "orphan");
  add_proc("skip", 81, "codex", 10, NULL);
  add_proc_tty("skip", 111, "claude", 10, "ext", 0);
  focus_window_t window = {.id = 1, .pid = 10};
  TEST_ASSERT(scan("skip", &window, 1, 100, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == 0);
  TEST_ASSERT(scan("skip", NULL, 0, 100, 5) == 0);
  TEST_ASSERT(agent_discover_scan("relative", &window, 1, 100, 5, 1000, 5) ==
              0);
  TEST_ASSERT(agent_discover_scan(NULL, &window, 1, 100, 5, 1000, 5) == 0);
  char path[320];
  snprintf(path, sizeof(path), "%s/skip", root);
  TEST_ASSERT(agent_discover_scan(path, &window, 1, 100, 0, 1000, 5) == 0);
  TEST_ASSERT(agent_discover_scan(path, &window, 1, 100, 5, 1000, -1) == 0);
}

static void several(void) {
  agent_sessions_reset();
  make_tree("agents");
  add_proc("agents", 10, "kitty", 1, NULL);
  add_proc("agents", 102, "claude", 10, "cws");
  add_proc("agents", 103, "grok", 10, "gws");
  add_proc("agents", 101, "kimi", 10, "kws");
  add_proc("agents", 80, "codex", 1, "self");
  focus_window_t windows[2] = {
      {.id = 1, .pid = 10},
      {.id = 2, .pid = 80}
  };
  TEST_ASSERT(scan("agents", windows, 2, 100, 5) == 4);
  expect_idle(102, "claude", "cws");
  expect_idle(103, "grok", "gws");
  expect_idle(101, "kimi", "kws");
  expect_idle(80, "codex", "self");
}

static void caps(void) {
  agent_sessions_reset();
  make_tree("caps");
  add_proc("caps", 201, "codex", 10, "a");
  add_proc("caps", 202, "codex", 10, "b");
  add_proc("caps", 203, "codex", 10, "c");
  focus_window_t window = {.id = 1, .pid = 10};
  TEST_ASSERT(scan("caps", &window, 1, 100, 2) == 2);
  TEST_ASSERT(agent_sessions_count() == 2);
  agent_sessions_reset();
  TEST_ASSERT(scan("caps", &window, 1, 1, 5) == 1);
  agent_sessions_reset();
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    TEST_ASSERT(agent_sessions_apply((uint64_t)(i + 1), "claude",
                                     AGENT_EVENT_START, 1000 + i, 1000, 5,
                                     NULL) == 0);
  TEST_ASSERT(agent_sessions_count() == AGENT_SESSIONS_MAX);
  TEST_ASSERT(scan("caps", &window, 1, 100, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == AGENT_SESSIONS_MAX);
}

static void depth(void) {
  agent_sessions_reset();
  make_tree("depth");
  for (pid_t pid = 400; pid <= 416; pid++) {
    const char *comm = pid == 400 ? "codex" : "zsh";
    const char *base = pid == 400 ? "deep" : NULL;
    add_proc("depth", pid, comm, pid == 416 ? 1 : pid + 1, base);
  }
  focus_window_t near = {.id = 1, .pid = 415};
  TEST_ASSERT(scan("depth", &near, 1, 100, 5) == 1);
  expect_idle(400, "codex", "deep");
  agent_sessions_reset();
  focus_window_t far = {.id = 2, .pid = 416};
  TEST_ASSERT(scan("depth", &far, 1, 100, 5) == 0);
  TEST_ASSERT(agent_sessions_count() == 0);
}

static void write_environ(const char *tree, pid_t pid, const void *bytes,
                          size_t length) {
  char path[360];
  snprintf(path, sizeof(path), "%s/%s/%ld/environ", root, tree, (long)pid);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fwrite(bytes, 1, length, file) == length);
  TEST_ASSERT(fclose(file) == 0);
}

static void kitty_split(void) {
  agent_sessions_reset();
  make_tree("kit");
  char base[320];
  snprintf(base, sizeof(base), "%s/names/split", root);
  TEST_ASSERT(mkdir(base, 0700) == 0);
  add_proc("kit", 10, "kitty", 1, "split");
  add_proc("kit", 141, "grok", 10, "split");
  const char env[] = "SECRET=hidden\0KITTY_PID=2556\0KITTY_WINDOW_ID=12\0"
                     "KITTY_LISTEN_ON=unix:/tmp/kit.sock\0";
  write_environ("kit", 141, env, sizeof(env));
  focus_window_t window = {.id = 1, .pid = 10};
  TEST_ASSERT(scan("kit", &window, 1, 100, 5) == 1);
  expect_idle(141, "grok", "split");
  uint64_t id = 0;
  char listen[128];
  TEST_ASSERT(agent_sessions_kitty(141, &id, listen, sizeof(listen)));
  TEST_ASSERT(id == 12 && !strcmp(listen, "unix:/tmp/kit.sock"));
  TEST_ASSERT(!strstr(listen, "SECRET"));
  agent_session_view_t views[4];
  TEST_ASSERT(agent_sessions_snapshot(views, 4) == 1);
  TEST_ASSERT(views[0].kitty_pid == 2556 && views[0].kitty_window == 12);

  agent_sessions_reset();
  make_tree("one");
  add_proc("one", 10, "kitty", 1, "split");
  add_proc("one", 142, "codex", 10, "split");
  const char only[] = "KITTY_WINDOW_ID=4";
  write_environ("one", 142, only, sizeof(only));
  TEST_ASSERT(scan("one", &window, 1, 100, 5) == 1);
  expect_idle(142, "codex", "split");
  TEST_ASSERT(!agent_sessions_kitty(142, &id, listen, sizeof(listen)));

  agent_sessions_reset();
  make_tree("badpid");
  add_proc("badpid", 10, "kitty", 1, "split");
  add_proc("badpid", 143, "grok", 10, "split");
  const char bad[] =
      "KITTY_PID=01\0KITTY_WINDOW_ID=9\0KITTY_LISTEN_ON=unix:/tmp/bad.sock\0";
  write_environ("badpid", 143, bad, sizeof(bad));
  TEST_ASSERT(scan("badpid", &window, 1, 100, 5) == 1);
  TEST_ASSERT(agent_sessions_kitty(143, &id, listen, sizeof(listen)));
  TEST_ASSERT(id == 9 && !strcmp(listen, "unix:/tmp/bad.sock"));
  TEST_ASSERT(agent_sessions_snapshot(views, 4) == 1);
  TEST_ASSERT(views[0].kitty_pid == 0 && views[0].kitty_window == 9);
}

static void repository(void) {
  agent_sessions_reset();
  make_tree("repo");
  char base[320], src[360], git[360], link[360];
  snprintf(base, sizeof(base), "%s/names/repo", root);
  snprintf(src, sizeof(src), "%s/src", base);
  snprintf(git, sizeof(git), "%s/.git", base);
  TEST_ASSERT(mkdir(base, 0700) == 0 && mkdir(git, 0700) == 0 &&
              mkdir(src, 0700) == 0);
  add_proc("repo", 10, "kitty", 1, NULL);
  add_proc("repo", 140, "codex", 10, NULL);
  snprintf(link, sizeof(link), "%s/repo/140/cwd", root);
  TEST_ASSERT(symlink(src, link) == 0);
  focus_window_t window = {.id = 1, .pid = 10};
  TEST_ASSERT(scan("repo", &window, 1, 100, 5) == 1);
  expect_idle(140, "codex", "repo");
}

int main(void) {
  TEST_ASSERT(mkdtemp(root));
  char names_dir[320];
  snprintf(names_dir, sizeof(names_dir), "%s/names", root);
  TEST_ASSERT(mkdir(names_dir, 0700) == 0);
  names();
  matched();
  skipped();
  several();
  caps();
  depth();
  repository();
  kitty_split();
  rm_tree(root);
  TEST_ASSERT(access(root, F_OK) < 0);
  return 0;
}

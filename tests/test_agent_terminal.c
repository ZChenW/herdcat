#define _GNU_SOURCE
#include "platform/agent_terminal.h"
#include "test_helpers.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/herdcat-term-XXXXXX";

static void remove_tree(const char *path) {
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
    remove_tree(child);
  }
  closedir(dir);
  rmdir(path);
}

static void write_file(const char *path, const void *bytes, size_t length) {
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file);
  TEST_ASSERT(fwrite(bytes, 1, length, file) == length);
  TEST_ASSERT(fclose(file) == 0);
}

static bool parse(const void *bytes, size_t length, uint64_t *window,
                  char *listen, size_t capacity) {
  return agent_terminal_parse(bytes, length, window, listen, capacity, NULL);
}

static void test_parse(void) {
  uint64_t window = 99;
  char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
  const char both[] = "SECRET=hidden\0KITTY_LISTEN_ON=unix:/run/kitty\0"
                      "KITTY_WINDOW_ID=007\0";
  TEST_ASSERT(parse(both, sizeof(both), &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 7 && !strcmp(listen, "unix:/run/kitty"));
  TEST_ASSERT(!strstr(listen, "SECRET"));

  const char window_only[] = "KITTY_WINDOW_ID=3";
  TEST_ASSERT(!parse(window_only, sizeof(window_only), &window, listen,
                     sizeof(listen)));
  const char socket_only[] = "KITTY_LISTEN_ON=unix:/run/kitty";
  TEST_ASSERT(!parse(socket_only, sizeof(socket_only), &window, listen,
                     sizeof(listen)));
  const char neither[] = "HOME=/tmp\0PATH=/bin\0";
  TEST_ASSERT(
      !parse(neither, sizeof(neither), &window, listen, sizeof(listen)));
  TEST_ASSERT(!parse(NULL, 0, &window, listen, sizeof(listen)));
  TEST_ASSERT(!parse("", 0, &window, listen, sizeof(listen)));

  const char partial[] = "KITTY_WINDOW_ID=3\0KITTY_LISTEN_ON=unix:/tmp/k";
  TEST_ASSERT(
      !parse(partial, sizeof(partial) - 1, &window, listen, sizeof(listen)));
  const char tail[] = "KITTY_WINDOW_ID=3\0KITTY_LISTEN_ON=unix:/tmp/k\0TAIL";
  TEST_ASSERT(parse(tail, sizeof(tail) - 1, &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 3 && !strcmp(listen, "unix:/tmp/k"));

  const char last[] =
      "KITTY_WINDOW_ID=1\0KITTY_WINDOW_ID=4\0KITTY_LISTEN_ON=unix:/a\0";
  TEST_ASSERT(parse(last, sizeof(last), &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 4 && !strcmp(listen, "unix:/a"));

  char tiny[8];
  TEST_ASSERT(!parse(both, sizeof(both), &window, tiny, sizeof(tiny)));

  char over[160];
  memset(over, 'p', sizeof(over));
  memcpy(over, "unix:", 5);
  over[128] = '\0';
  char env[200];
  int wrote = snprintf(env, sizeof(env), "KITTY_WINDOW_ID=2");
  TEST_ASSERT(wrote > 0);
  env[wrote] = '\0';
  memcpy(env + wrote + 1, "KITTY_LISTEN_ON=", 16);
  memcpy(env + wrote + 1 + 16, over, 129);
  size_t length = (size_t)wrote + 1 + 16 + 129;
  TEST_ASSERT(!parse(env, length, &window, listen, sizeof(listen)));
  over[127] = '\0';
  memcpy(env + wrote + 1 + 16, over, 128);
  length = (size_t)wrote + 1 + 16 + 128;
  TEST_ASSERT(parse(env, length, &window, listen, sizeof(listen)));
  TEST_ASSERT(window == 2 && strlen(listen) == 127);

  TEST_ASSERT(!agent_terminal_accept("123456789012345678901", "unix:/a",
                                     &window, listen, sizeof(listen)));
  TEST_ASSERT(!agent_terminal_accept("18446744073709551616", "unix:/a", &window,
                                     listen, sizeof(listen)));
  TEST_ASSERT(agent_terminal_accept("18446744073709551615", "unix:/a", &window,
                                    listen, sizeof(listen)));
  TEST_ASSERT(window == UINT64_MAX);
  TEST_ASSERT(agent_terminal_accept("00000000000000000000", "unix:", &window,
                                    listen, sizeof(listen)));
  TEST_ASSERT(window == 0 && !strcmp(listen, "unix:"));
  TEST_ASSERT(
      !agent_terminal_accept("", "unix:/a", &window, listen, sizeof(listen)));
  TEST_ASSERT(
      !agent_terminal_accept("+3", "unix:/a", &window, listen, sizeof(listen)));
  TEST_ASSERT(!agent_terminal_accept("3", "unix:/a\nb", &window, listen,
                                     sizeof(listen)));
  TEST_ASSERT(!agent_terminal_accept("3",
                                     "unix:/a\x7f"
                                     "b",
                                     &window, listen, sizeof(listen)));
  TEST_ASSERT(!agent_terminal_accept("3", "tcp:127.0.0.1:1", &window, listen,
                                     sizeof(listen)));

  pid_t kitty = 9;
  const char pid_ok[] = "KITTY_PID=2556\0KITTY_WINDOW_ID=8\0"
                        "KITTY_LISTEN_ON=unix:/k\0";
  TEST_ASSERT(agent_terminal_parse(pid_ok, sizeof(pid_ok), &window, listen,
                                   sizeof(listen), &kitty));
  TEST_ASSERT(kitty == 2556 && window == 8);
  const char pid_last[] = "KITTY_PID=2\0KITTY_PID=4194304\0KITTY_WINDOW_ID=8\0"
                          "KITTY_LISTEN_ON=unix:/k\0";
  TEST_ASSERT(agent_terminal_parse(pid_last, sizeof(pid_last), &window, listen,
                                   sizeof(listen), &kitty));
  TEST_ASSERT(kitty == 4194304);
  const char pid_missing[] = "KITTY_WINDOW_ID=6\0KITTY_LISTEN_ON=unix:/k\0";
  TEST_ASSERT(agent_terminal_parse(pid_missing, sizeof(pid_missing), &window,
                                   listen, sizeof(listen), &kitty));
  TEST_ASSERT(kitty == 0 && window == 6);
  const char *bad_pids[] = {"01",      "0",     "00",    "1", "+2",
                            "4194305", "2556x", "2556 ", ""};
  for (size_t i = 0; i < sizeof(bad_pids) / sizeof(bad_pids[0]); i++) {
    char pid_env[80];
    int pid_wrote =
        snprintf(pid_env, sizeof(pid_env), "KITTY_PID=%s", bad_pids[i]);
    TEST_ASSERT(pid_wrote > 0 && (size_t)pid_wrote + 40 < sizeof(pid_env));
    size_t used = (size_t)pid_wrote;
    pid_env[used++] = '\0';
    memcpy(pid_env + used, "KITTY_WINDOW_ID=5", 17);
    used += 17;
    pid_env[used++] = '\0';
    memcpy(pid_env + used, "KITTY_LISTEN_ON=unix:/k", 21);
    used += 21;
    pid_env[used++] = '\0';
    kitty = 9;
    TEST_ASSERT(agent_terminal_parse(pid_env, used, &window, listen,
                                     sizeof(listen), &kitty));
    TEST_ASSERT(kitty == 0 && window == 5);
  }
  kitty = 9;
  const char window_only_pid[] = "KITTY_PID=2556\0KITTY_WINDOW_ID=3";
  TEST_ASSERT(!agent_terminal_parse(window_only_pid, sizeof(window_only_pid),
                                    &window, listen, sizeof(listen), &kitty));
  TEST_ASSERT(kitty == 9);
}

static void test_read(void) {
  TEST_ASSERT(mkdtemp(root));
  char dir[256], path[300];
  snprintf(dir, sizeof(dir), "%s/42", root);
  TEST_ASSERT(mkdir(dir, 0700) == 0);
  snprintf(path, sizeof(path), "%s/environ", dir);
  const char both[] = "OTHER=secret\0KITTY_PID=2556\0KITTY_WINDOW_ID=15\0"
                      "KITTY_LISTEN_ON=unix:/run/kitty\0";
  write_file(path, both, sizeof(both));
  snprintf(dir, sizeof(dir), "%s/43", root);
  TEST_ASSERT(mkdir(dir, 0700) == 0);
  snprintf(path, sizeof(path), "%s/environ", dir);
  const char only[] = "KITTY_LISTEN_ON=unix:/run/kitty";
  write_file(path, only, sizeof(only));
  snprintf(dir, sizeof(dir), "%s/44", root);
  TEST_ASSERT(mkdir(dir, 0700) == 0);
  snprintf(path, sizeof(path), "%s/environ", dir);
  const char none[] = "HOME=/tmp";
  write_file(path, none, sizeof(none));

  int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  TEST_ASSERT(fd >= 0);
  uint64_t window = 0;
  char listen[128];
  pid_t kitty = 0;
  TEST_ASSERT(
      agent_terminal_read(fd, 42, &window, listen, sizeof(listen), &kitty));
  TEST_ASSERT(window == 15 && kitty == 2556 &&
              !strcmp(listen, "unix:/run/kitty"));
  TEST_ASSERT(!strstr(listen, "secret"));
  TEST_ASSERT(
      !agent_terminal_read(fd, 43, &window, listen, sizeof(listen), NULL));
  TEST_ASSERT(
      !agent_terminal_read(fd, 44, &window, listen, sizeof(listen), NULL));
  TEST_ASSERT(
      !agent_terminal_read(fd, 99, &window, listen, sizeof(listen), NULL));
  close(fd);

  snprintf(path, sizeof(path), "%s/outside", root);
  const char leaked[] = "KITTY_WINDOW_ID=8\0KITTY_LISTEN_ON=unix:/tmp/leaked\0";
  write_file(path, leaked, sizeof(leaked));
  char link[300];
  snprintf(link, sizeof(link), "%s/42/environ", root);
  TEST_ASSERT(unlink(link) == 0);
  TEST_ASSERT(symlink(path, link) == 0);
  fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  TEST_ASSERT(fd >= 0);
  TEST_ASSERT(
      !agent_terminal_read(fd, 42, &window, listen, sizeof(listen), NULL));
  close(fd);
  TEST_ASSERT(!agent_terminal_lookup("/no/such/proc", 42, &window, listen,
                                     sizeof(listen), NULL));
  remove_tree(root);
  TEST_ASSERT(access(root, F_OK) < 0);
}

int main(void) {
  test_parse();
  test_read();
  return 0;
}

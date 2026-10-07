#define _GNU_SOURCE
#include "platform/input.h"
#include "test_helpers.h"

#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

static bool available;
static bool executable;
static int spawn_count;

int __wrap_access(const char *path, int mode);
int __wrap_socketpair(int domain, int type, int protocol, int sockets[2]);
int __wrap_posix_spawn(pid_t *pid, const char *path,
                       const posix_spawn_file_actions_t *actions,
                       const posix_spawnattr_t *attributes, char *const args[],
                       char *const environment[]);
int __wrap_close(int fd);
int __real_close(int fd);
int __wrap_kill(pid_t pid, int sig);
pid_t __wrap_waitpid(pid_t pid, int *status, int options);
int __wrap_access(const char *path, int mode) {
  TEST_ASSERT(strstr(path, "herdcat-input") && mode == X_OK);
  return available && executable ? 0 : -1;
}
int __wrap_socketpair(int domain, int type, int protocol, int sockets[2]) {
  TEST_ASSERT(domain == AF_UNIX &&
              type == (SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK) &&
              protocol == 0);
  sockets[0] = 10000;
  sockets[1] = 10001;
  return 0;
}
int __wrap_posix_spawn(pid_t *pid, const char *path,
                       const posix_spawn_file_actions_t *actions,
                       const posix_spawnattr_t *attributes, char *const args[],
                       char *const environment[]) {
  (void)actions;
  (void)attributes;
  *pid = 1000000;
  spawn_count++;
  if (available && executable) {
    TEST_ASSERT(strstr(path, "herdcat-input"));
    TEST_ASSERT(!strcmp(args[1], "3") && !strcmp(args[2], "30"));
    TEST_ASSERT(!strcmp(args[3], "1") && !strcmp(args[4], "1"));
    TEST_ASSERT(!strcmp(args[5], "/dev/input/event123456") && args[6] == NULL);
    TEST_ASSERT(environment[0] == NULL);
  } else {
    TEST_ASSERT(!strcmp(path, "/proc/self/exe"));
    TEST_ASSERT(!strcmp(args[1], "--input-helper") && !strcmp(args[2], "3"));
    TEST_ASSERT(!strcmp(args[3], "1") && !strcmp(args[4], "0"));
    TEST_ASSERT(!strcmp(args[5], "30"));
    TEST_ASSERT(!strcmp(args[6], "/dev/input/event123456") && args[7] == NULL);
  }
  return 0;
}
int __wrap_close(int fd) {
  return fd == 10000 || fd == 10001 ? 0 : __real_close(fd);
}
int __wrap_kill(pid_t pid, int sig) {
  TEST_ASSERT(pid == 1000000 && sig == SIGTERM);
  return 0;
}
pid_t __wrap_waitpid(pid_t pid, int *status, int options) {
  (void)options;
  TEST_ASSERT(pid == 1000000);
  *status = 0;
  return pid;
}
int main(void) {
  char *paths[] = {"/dev/input/event123456"};
  available = true;
  executable = true;
  TEST_ASSERT(input_helper_path());
  TEST_ASSERT(!strcmp(input_mode_name(), "standalone"));
  TEST_ASSERT(strstr(input_mode_hint(), "setgid"));
  TEST_ASSERT(input_start_monitoring(paths, 1, NULL, 0, 30, 0) ==
              HERDCAT_SUCCESS);
  TEST_ASSERT(!strcmp(input_mode_name(), "standalone"));
  input_cleanup();
  available = false;
  TEST_ASSERT(!input_helper_path());
  TEST_ASSERT(input_start_monitoring(paths, 1, NULL, 0, 30, 0) ==
              HERDCAT_SUCCESS);
  TEST_ASSERT(!strcmp(input_mode_name(), "in-process"));
  TEST_ASSERT(strstr(input_mode_hint(), "missing or not executable"));
  input_cleanup();
  available = true;
  executable = false;
  TEST_ASSERT(input_start_monitoring(paths, 1, NULL, 0, 30, 0) ==
              HERDCAT_SUCCESS);
  TEST_ASSERT(!strcmp(input_mode_name(), "in-process"));
  input_cleanup();
  TEST_ASSERT(spawn_count == 3);
  puts("input helper preference, fallback argv and diagnostic hints passed");
  return 0;
}

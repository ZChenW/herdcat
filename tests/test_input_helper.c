#define _GNU_SOURCE
// Include the small executable to test private validators without widening
// its public interface or adding test hooks to privileged production code.
#define main input_helper_entry
int input_helper_entry(int argc, char **argv);
#include "../src/input/input_helper.c"
#undef main

#include "test_helpers.h"

#include <sys/eventfd.h>
#include <sys/wait.h>

static int simulated_node;
static bool simulate_open;
static unsigned group_changes;
static bool fail_group;
static bool mismatch_group;
static gid_t test_real = 1000;
static gid_t test_effective = 1000;
static gid_t test_saved = 1000;

int __wrap_open(const char *path, int flags, ...);
int __wrap_openat(int fd, const char *path, int flags, ...);
int __wrap_fstat(int fd, struct stat *metadata);
int __wrap_setresgid(gid_t real, gid_t effective, gid_t saved);
int __wrap_getresgid(gid_t *real, gid_t *effective, gid_t *saved);
int __real_open(const char *path, int flags, ...);
int __real_fstat(int fd, struct stat *metadata);

int __wrap_open(const char *path, int flags, ...) {
  if (simulate_open && !strcmp(path, "/dev/input")) {
    TEST_ASSERT(flags ==
                (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    return 10000;
  }
  return __real_open(path, flags);
}
int __wrap_openat(int fd, const char *path, int flags, ...) {
  TEST_ASSERT(simulate_open && fd == 10000);
  TEST_ASSERT(!strcmp(path, "event1"));
  TEST_ASSERT(flags == (O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  TEST_ASSERT(test_effective == saved_group);
  if (simulated_node == 1) {
    errno = ELOOP;  // Kernel's O_NOFOLLOW response for a symlink.
    return -1;
  }
  return 10001;
}
int __wrap_fstat(int fd, struct stat *metadata) {
  if (simulate_open && fd == 10001) {
    TEST_ASSERT(test_effective == real_group);
    *metadata =
        (struct stat){.st_mode = simulated_node == 2 ? S_IFREG : S_IFCHR,
                      .st_rdev = makedev(simulated_node == 3 ? 1 : 13, 64)};
    return 0;
  }
  return __real_fstat(fd, metadata);
}
int __wrap_setresgid(gid_t real, gid_t effective, gid_t saved) {
  if (fail_group) {
    return -1;
  }
  test_real = real;
  test_effective = effective;
  test_saved = saved;
  group_changes++;
  return 0;
}
int __wrap_getresgid(gid_t *real, gid_t *effective, gid_t *saved) {
  *real = test_real;
  *effective = test_effective;
  *saved = mismatch_group ? test_saved + 1 : test_saved;
  return 0;
}

static void test_rejections(void) {
  int value;
  const char *bad_numbers[] = {
      "",   "-1", "+1",   " 1",
      "1 ", "1x", "3601", "999999999999999999999999999999"};
  for (size_t i = 0; i < sizeof(bad_numbers) / sizeof(bad_numbers[0]); i++) {
    TEST_ASSERT(!input_number(bad_numbers[i], 3600, &value));
  }
  TEST_ASSERT(input_number("3600", 3600, &value) && value == 3600);
  TEST_ASSERT(input_number("0", 0, &value) && value == 0);
  TEST_ASSERT(!input_number("1", 0, &value));
  const char *bad_paths[] = {
      "",
      "/dev/input/event",
      "/dev/input/event-1",
      "/dev/input/event1/a",
      "/dev/input/event1/../event2",
      "/dev/input/../input/event1",
      "/dev/input//event1",
      "dev/input/event1",
      "/dev/input/by-id/keyboard",
      "/dev/input/event1x",
      "/tmp/event1",
      "/dev/input/event1\n",
      "/dev/input/event11111111111111111111111111111111111111111111111111111"};
  for (size_t i = 0; i < sizeof(bad_paths) / sizeof(bad_paths[0]); i++) {
    TEST_ASSERT(!input_event_path(bad_paths[i]));
  }
  TEST_ASSERT(input_event_path("/dev/input/event0"));
  TEST_ASSERT(input_event_path("/dev/input/event123"));
  int pipe_fds[2];
  TEST_ASSERT(pipe2(pipe_fds, O_CLOEXEC) == 0);
  TEST_ASSERT(!input_output(pipe_fds[0]));
  TEST_ASSERT(input_output(pipe_fds[1]));
  TEST_ASSERT(!input_output(-1));
  TEST_ASSERT(!input_output(10000));
  TEST_ASSERT(!input_output(0));
  char temporary[] = "/tmp/herdcat-output-fd-XXXXXX";
  int regular = mkstemp(temporary);
  TEST_ASSERT(regular >= 0 && unlink(temporary) == 0);
  TEST_ASSERT(regular >= 0 && !input_output(regular));
  close(regular);
  int terminal = __real_open("/dev/null", O_RDWR | O_CLOEXEC);
  TEST_ASSERT(terminal >= 0 && !input_output(terminal));
  close(terminal);
  int counter = eventfd(0, EFD_CLOEXEC);
  TEST_ASSERT(counter >= 0 && !input_output(counter));
  close(counter);
  char fifo_path[] = "/tmp/herdcat-output-fifo-XXXXXX";
  int placeholder = mkstemp(fifo_path);
  TEST_ASSERT(placeholder >= 0);
  close(placeholder);
  TEST_ASSERT(unlink(fifo_path) == 0 && mkfifo(fifo_path, 0600) == 0);
  int self_reader = __real_open(fifo_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  TEST_ASSERT(self_reader >= 0 && !input_output(self_reader));
  close(self_reader);
  TEST_ASSERT(unlink(fifo_path) == 0);
  char fd_text[32];
  snprintf(fd_text, sizeof(fd_text), "%d", pipe_fds[1]);
  char *args[] = {"helper", fd_text, "3600", "1", "1", "/dev/input/event1"};
  input_options_t options;
  TEST_ASSERT(input_arguments(6, args, &options));
  TEST_ASSERT(!input_arguments(5, args, &options));
  args[4] = "33";
  TEST_ASSERT(!input_arguments(6, args, &options));
  args[4] = "1";
  args[3] = "0";
  TEST_ASSERT(!input_arguments(6, args, &options));
  args[3] = "2";
  TEST_ASSERT(!input_arguments(6, args, &options));
  args[3] = "1";
  args[2] = "3601";
  TEST_ASSERT(!input_arguments(6, args, &options));
  args[2] = "0";
  args[1] = "2147483648";
  TEST_ASSERT(!input_arguments(6, args, &options));
  args[1] = fd_text;
  args[5] = "/dev/input/event1/../event2";
  TEST_ASSERT(!input_arguments(6, args, &options));
  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

static void test_device_and_group(void) {
  real_group = 1000;
  saved_group = 992;
  simulate_open = true;
  for (int node = 1; node <= 4; node++) {
    simulated_node = node;
    int fd = input_open("/dev/input/event1");
    TEST_ASSERT(node == 4 ? fd == 10001 : fd == -1);
    TEST_ASSERT(test_effective == 1000 && test_saved == 992);
  }
  TEST_ASSERT(group_changes == 8);
  TEST_ASSERT(input_group(false, true));
  TEST_ASSERT(test_real == 1000 && test_effective == 1000 &&
              test_saved == 1000);
  TEST_ASSERT(prctl(PR_GET_DUMPABLE) == 0);
  simulate_open = false;
  fail_group = true;
  TEST_ASSERT(!input_group(false, true));
  fail_group = false;
  mismatch_group = true;
  TEST_ASSERT(!input_group(false, true));
  mismatch_group = false;
}

static void test_filter(int operation) {
  int pipe_fds[2];
  TEST_ASSERT(pipe(pipe_fds) == 0);
  pid_t pid = fork();
  TEST_ASSERT(pid >= 0);
  if (!pid) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || !input_seccomp(pipe_fds[1])) {
      _exit(2);
    }
    if (operation == 0) {
      _exit(input_emit(pipe_fds[1], PAW_BOTH, 2, 0) ? 0 : 3);
    }
    if (operation == 1) {
      syscall(SYS_openat, AT_FDCWD, "/tmp/herdcat-forbidden-write",
              O_WRONLY | O_CREAT, 0600);
    } else if (operation == 2) {
      syscall(SYS_ioctl, -1, EVIOCGRAB, 1);
    } else if (operation == 3) {
      syscall(SYS_write, STDOUT_FILENO, "forbidden", 9);
    } else {
      syscall(SYS_execve, "/nonexistent", NULL, NULL);
    }
    _exit(4);
  }
  int status;
  TEST_ASSERT(waitpid(pid, &status, 0) == pid);
  if (!operation) {
    TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    input_message_t message;
    TEST_ASSERT(read(pipe_fds[0], &message, sizeof(message)) ==
                sizeof(message));
    TEST_ASSERT(message.paws == PAW_BOTH && message.reserved == 0);
  } else {
    TEST_ASSERT(WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS);
  }
  close(pipe_fds[0]);
  close(pipe_fds[1]);
}

int main(void) {
  TEST_ASSERT(sizeof(input_message_t) == 24);
  TEST_ASSERT(offsetof(input_message_t, monotonic_ns) == 16);
  test_rejections();
  test_device_and_group();
  for (int operation = 0; operation <= 4; operation++) {
    test_filter(operation);
  }
  TEST_ASSERT(setenv("HERDCAT_DEBUG", "1", 1) == 0);
  char *args[] = {"helper", NULL};
  TEST_ASSERT(input_helper_entry(1, args) == 1);
  TEST_ASSERT(environ == NULL);
  puts("input helper validators, gid transitions and seccomp passed");
  return 0;
}

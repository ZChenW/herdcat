#define _GNU_SOURCE
#include "platform/agent_output.h"
#include "test_helpers.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  uint64_t wchar;
  TEST_ASSERT(!agent_output_wchar(0, &wchar));
  TEST_ASSERT(!agent_output_wchar(-1, &wchar));
  TEST_ASSERT(!agent_output_wchar(getpid(), NULL));
  int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  TEST_ASSERT(master >= 0);
  TEST_ASSERT(grantpt(master) == 0 && unlockpt(master) == 0);
  const char *terminal = ptsname(master);
  TEST_ASSERT(terminal);
  int slave = open(terminal, O_RDWR | O_NOCTTY | O_CLOEXEC);
  TEST_ASSERT(slave >= 0);
  int ready[2], command[2];
  TEST_ASSERT(pipe(ready) == 0 && pipe(command) == 0);
  pid_t child = fork();
  TEST_ASSERT(child >= 0);
  if (!child) {
    close(ready[0]);
    close(command[1]);
    close(master);
    TEST_ASSERT(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
    close(slave);
    TEST_ASSERT(write(1, "output", 6) == 6);
    TEST_ASSERT(write(ready[1], "r", 1) == 1);
    char byte;
    TEST_ASSERT(read(command[0], &byte, 1) == 1);
    int null = open("/dev/null", O_WRONLY);
    TEST_ASSERT(null >= 0 && dup2(null, 1) == 1);
    close(null);
    TEST_ASSERT(write(ready[1], "r", 1) == 1);
    TEST_ASSERT(read(command[0], &byte, 1) == 1);
    close(ready[1]);
    close(command[0]);
    _exit(0);
  }
  close(ready[1]);
  close(command[0]);
  close(slave);
  char byte;
  TEST_ASSERT(read(ready[0], &byte, 1) == 1);
  TEST_ASSERT(agent_output_wchar(child, &wchar));
  // wchar includes all write syscalls, including the one-byte readiness pipe.
  TEST_ASSERT(wchar == 7);
  TEST_ASSERT(write(command[1], "c", 1) == 1);
  TEST_ASSERT(read(ready[0], &byte, 1) == 1);
  TEST_ASSERT(!agent_output_wchar(child, &wchar));
  TEST_ASSERT(write(command[1], "c", 1) == 1);
  int status;
  TEST_ASSERT(waitpid(child, &status, 0) == child);
  TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  TEST_ASSERT(!agent_output_wchar(child, &wchar));
  close(command[1]);
  close(ready[0]);
  close(master);
  return 0;
}

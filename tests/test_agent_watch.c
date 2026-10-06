#define _GNU_SOURCE
#include "platform/agent_watch.h"
#include "test_helpers.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t exited_pid;
static int exits;

static void process_exited(pid_t pid) {
  exited_pid = pid;
  exits++;
}

static int fd_count(void) {
  DIR *dir = opendir("/proc/self/fd");
  TEST_ASSERT(dir != NULL);
  int count = 0;
  while (readdir(dir)) {
    count++;
  }
  closedir(dir);
  return count;
}

int main(void) {
  TEST_ASSERT(agent_watch_fd() == -1);
  TEST_ASSERT(agent_watch_add(getpid()) == -1);
  int before = fd_count();
  TEST_ASSERT(agent_watch_init() == 0);
  TEST_ASSERT(agent_watch_init() == 0);
  TEST_ASSERT(fd_count() == before + 1);
  TEST_ASSERT(fcntl(agent_watch_fd(), F_GETFD) & FD_CLOEXEC);
  TEST_ASSERT(agent_watch_add(getpid()) == 0);
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT(agent_watch_add(getpid()) == 0);
  }
  TEST_ASSERT(fd_count() == before + 2);
  pid_t retained = getpid();
  agent_watch_retain(&retained, 1);
  TEST_ASSERT(fd_count() == before + 2);
  agent_watch_retain(NULL, 0);
  TEST_ASSERT(fd_count() == before + 1);
  TEST_ASSERT(agent_watch_add(getpid()) == 0);
  struct pollfd pfd = {.fd = agent_watch_fd(), .events = POLLIN};
  TEST_ASSERT(poll(&pfd, 1, 0) == 0);
  agent_watch_process(process_exited);
  TEST_ASSERT(exits == 0);
  agent_watch_remove(getpid());
  TEST_ASSERT(fd_count() == before + 1);

  int gate[2];
  TEST_ASSERT(pipe2(gate, O_CLOEXEC) == 0);
  pid_t child = fork();
  TEST_ASSERT(child >= 0);
  if (!child) {
    close(gate[1]);
    char byte;
    ssize_t result = read(gate[0], &byte, 1);
    close(gate[0]);
    _exit(result == 1 ? 0 : 1);
  }
  close(gate[0]);
  TEST_ASSERT(agent_watch_add(child) == 0);
  TEST_ASSERT(agent_watch_add(child) == 0);
  TEST_ASSERT(poll(&pfd, 1, 0) == 0);
  TEST_ASSERT(write(gate[1], "x", 1) == 1);
  close(gate[1]);
  TEST_ASSERT(poll(&pfd, 1, 2000) == 1);
  int status;
  TEST_ASSERT(waitpid(child, &status, 0) == child);
  TEST_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  agent_watch_process(process_exited);
  TEST_ASSERT(exits == 1 && exited_pid == child);
  TEST_ASSERT(poll(&pfd, 1, 0) == 0);
  agent_watch_process(process_exited);
  TEST_ASSERT(exits == 1);
  TEST_ASSERT(agent_watch_add(child) == -1 && errno == ESRCH);
  TEST_ASSERT(agent_watch_add(0) == -1);
  TEST_ASSERT(fd_count() == before + 1);
  TEST_ASSERT(agent_watch_add(getpid()) == 0);
  agent_watch_cleanup();
  agent_watch_cleanup();
  TEST_ASSERT(agent_watch_fd() == -1);
  TEST_ASSERT(fd_count() == before);
  TEST_ASSERT(agent_watch_init() == 0);
  TEST_ASSERT(agent_watch_add(getpid()) == 0);
  agent_watch_cleanup();
  TEST_ASSERT(fd_count() == before);
  return 0;
}

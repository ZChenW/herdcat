#define _GNU_SOURCE
#include "core/setup_hint.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool collect(pid_t pid, int fd, char *output, size_t size) {
  int64_t deadline = now_ms() + 2000;
  size_t used = 0;
  bool eof = false, reaped = false, success = false;
  while (now_ms() < deadline) {
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      reaped = true;
      success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    } else if (result < 0 && errno != EINTR && !reaped) {
      break;
    }
    if (!eof) {
      ssize_t bytes = read(fd, output + used, size - used - 1);
      if (bytes > 0) {
        used += (size_t)bytes;
        if (used == size - 1)
          break;
      } else if (!bytes) {
        eof = true;
      } else if (errno != EAGAIN && errno != EINTR) {
        break;
      }
    }
    if (eof && reaped) {
      output[used] = '\0';
      return success && used > 0 && output[used - 1] == '\n' &&
             !memchr(output, '\n', used - 1) && !memchr(output, '\0', used);
    }
    int64_t left = deadline - now_ms();
    if (left <= 0)
      break;
    struct pollfd pfd = {.fd = eof ? -1 : fd, .events = POLLIN};
    // A pipe can close just before waitpid observes exit. Bound that race.
    poll(&pfd, 1, eof || reaped ? 1 : (int)left);
  }
  if (!reaped) {
    kill(pid, SIGKILL);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
  }
  return false;
}

void setup_hint_print(void) {
  int pipes[2];
  if (pipe2(pipes, O_CLOEXEC) < 0)
    return;
  if (fcntl(pipes[0], F_SETFL, O_NONBLOCK) < 0) {
    close(pipes[0]);
    close(pipes[1]);
    return;
  }
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (!error)
      error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
                                               "/dev/null", O_RDONLY, 0);
    if (!error)
      error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                               "/dev/null", O_WRONLY, 0);
    pid_t pid;
    char *argv[] = {"herdcat-setup", "--status-hint", NULL};
    if (!error)
      error = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipes[1]);
    pipes[1] = -1;
    if (!error) {
      char output[1024];
      if (collect(pid, pipes[0], output, sizeof(output)))
        fputs(output, stdout);
    }
  }
  close(pipes[0]);
  if (pipes[1] >= 0)
    close(pipes[1]);
}

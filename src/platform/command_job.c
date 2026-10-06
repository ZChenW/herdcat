#define _GNU_SOURCE
#include "platform/command_job.h"

#include "platform/agent_terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
void job_cleanup(command_job_t *job) {
  if (job->fd >= 0) {
    close(job->fd);
  }
  if (job->pid > 0 && !job->exited) {
    kill(job->pid, SIGKILL);
    while (waitpid(job->pid, &job->status, 0) < 0 && errno == EINTR) {}
  }
  job->fd = -1;
  job->pid = 0;
}
const char *command_socket;
int job_start(command_job_t *job, const char *const argv[]) {
  *job = (command_job_t){.fd = -1};
  int pipes[2];
  if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) < 0) {
    return -1;
  }
  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (!error) {
      error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
                                               "/dev/null", O_WRONLY, 0);
    }
    if (!error) {
      error = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    }
    if (!error) {
      // owned is what this call allocated, whatever command_socket says later.
      char **environment = environ, **owned = NULL;
      char socket_env[AGENT_TERMINAL_LISTEN_MAX + 32];
      if (command_socket) {
        size_t count = 0;
        while (environ[count])
          count++;
        owned = calloc(count + 2, sizeof(*owned));
        if (!owned) {
          error = ENOMEM;
        } else {
          environment = owned;
          size_t used = 0;
          for (size_t i = 0; i < count; i++)
            if (strncmp(environ[i], "WEZTERM_UNIX_SOCKET=", 20))
              environment[used++] = environ[i];
          snprintf(socket_env, sizeof(socket_env), "WEZTERM_UNIX_SOCKET=%s",
                   command_socket);
          environment[used] = socket_env;
        }
      }
      if (!error)
        error = posix_spawnp(&job->pid, argv[0], &actions, NULL,
                             (char *const *)argv, environment);
      free(owned);
    }
    posix_spawn_file_actions_destroy(&actions);
  }
  close(pipes[1]);
  if (error) {
    close(pipes[0]);
    job->pid = 0;
    return -1;
  }
  job->fd = pipes[0];
  job->deadline = now_ms() + 1000;
  return 0;
}
// Returns 0 while pending, 1 for complete output, -1 for any failure.
int job_process(command_job_t *job) {
  char chunk[1024];
  ssize_t count;
  while ((count = read(job->fd, chunk, sizeof(chunk))) > 0) {
    if ((size_t)count > sizeof(job->buffer) - 1 - job->used) {
      job->failed = true;
      break;
    }
    memcpy(job->buffer + job->used, chunk, (size_t)count);
    job->used += (size_t)count;
  }
  if (!count) {
    job->eof = true;
  } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
    job->failed = true;
  }
  if (!job->exited) {
    pid_t result = waitpid(job->pid, &job->status, WNOHANG);
    if (result == job->pid) {
      job->exited = true;
    } else if (result < 0 && errno != EINTR) {
      job->failed = true;
    }
  }
  if (job->failed || now_ms() >= job->deadline) {
    job_cleanup(job);
    return -1;
  }
  if (!job->eof || !job->exited) {
    return 0;
  }
  bool success = WIFEXITED(job->status) && WEXITSTATUS(job->status) == 0;
  job->buffer[job->used] = '\0';
  job_cleanup(job);
  return (int)success ? 1 : -1;
}

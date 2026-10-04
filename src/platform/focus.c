#define _GNU_SOURCE
#include "platform/focus.h"

#include "core/agent_hook.h"

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

typedef struct {
  pid_t pid;
  int fd;
  int64_t deadline;
  size_t used;
  bool eof, failed, exited;
  int status;
  char buffer[65536];
} command_job_t;
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}
static void job_cleanup(command_job_t *job) {
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
static int job_start(command_job_t *job, const char *const argv[]) {
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
      error = posix_spawnp(&job->pid, argv[0], &actions, NULL,
                           (char *const *)argv, environ);
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
static int job_process(command_job_t *job) {
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

static command_job_t job = {.fd = -1};
static pid_t target_pid;
static bool focusing;
static focus_result_t result;
bool focus_available(void) {
  const char *socket = getenv("NIRI_SOCKET");
  struct stat st;
  return socket && *socket && stat(socket, &st) == 0 && S_ISSOCK(st.st_mode);
}
bool focus_find_window(pid_t pid, const focus_window_t *windows, size_t count,
                       uint64_t *id) {
  for (int depth = 0; depth < 16 && pid > 1; depth++) {
    for (size_t i = 0; i < count; i++) {
      if (windows[i].pid == pid) {
        *id = windows[i].id;
        return true;
      }
    }
    char path[64], buffer[1024], comm[256];
    pid_t parent;
    snprintf(path, sizeof(path), "/proc/%jd/stat", (intmax_t)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      return false;
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0)
      return false;
    buffer[n] = 0;
    if (agent_hook_parse_stat(buffer, comm, sizeof(comm), &parent) < 0 ||
        parent == pid)
      return false;
    pid = parent;
  }
  return false;
}
int focus_session_window(pid_t pid) {
  if (job.pid || pid <= 1 || !focus_available())
    return -1;
  const char *args[] = {"niri", "msg", "-j", "windows", NULL};
  if (job_start(&job, args) < 0)
    return -1;
  target_pid = pid;
  focusing = false;
  result = FOCUS_PENDING;
  return 0;
}
void focus_poll(void) {
  if (!job.pid)
    return;
  int done = job_process(&job);
  if (!done)
    return;
  if (done < 0) {
    result = FOCUS_UNAVAILABLE;
    return;
  }
  if (focusing) {
    result = FOCUS_SUCCESS;
    return;
  }
  focus_window_t windows[256];
  uint64_t id;
  int count = focus_parse_windows(job.buffer, job.used, windows, 256);
  if (count < 0) {
    result = FOCUS_UNAVAILABLE;
    return;
  }
  if (!focus_find_window(target_pid, windows, (size_t)count, &id)) {
    result = FOCUS_NOT_FOUND;
    return;
  }
  char identifier[32];
  snprintf(identifier, sizeof(identifier), "%" PRIu64, id);
  const char *args[] = {"niri", "msg",      "action", "focus-window",
                        "--id", identifier, NULL};
  int64_t deadline = job.deadline;
  if (job_start(&job, args) < 0) {
    result = FOCUS_UNAVAILABLE;
    return;
  }
  job.deadline = deadline;
  focusing = true;
}
int focus_poll_fd(void) {
  return job.eof ? -1 : job.fd;
}
int focus_timeout(void) {
  if (!job.pid)
    return -1;
  int64_t left = job.deadline - now_ms();
  if (left <= 0)
    return 0;
  return job.eof && left > 10 ? 10 : (int)left;
}
focus_result_t focus_take_result(void) {
  focus_result_t value = result;
  result = FOCUS_PENDING;
  return value;
}
void focus_cleanup(void) {
  job_cleanup(&job);
  result = FOCUS_PENDING;
}

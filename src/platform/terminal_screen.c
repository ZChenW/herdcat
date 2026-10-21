#define _GNU_SOURCE
#include "platform/terminal_screen.h"

#include "core/agent_sessions.h"
#include "core/cursor_approval.h"
#include "platform/agent_terminal.h"
#include "platform/agent_watch.h"
#include "platform/command_job.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>  // IWYU pragma: keep
#include <sys/types.h>

#define SCREEN_TOKEN 0x7363726eU
static command_job_t job = {.fd = -1};

bool terminal_screen_target(pid_t pid, agent_terminal_t *target) {
  if (!target || pid <= 1)
    return false;
  // A mux session belongs to its own pane, never the outer kitty screen.
  if (agent_sessions_terminal(pid, target, NULL, 0))
    return target->kind == TERMINAL_TMUX;
  *target = (agent_terminal_t){.kind = TERMINAL_KITTY};
  return agent_sessions_kitty(pid, &target->pane, target->socket,
                              sizeof(target->socket));
}

bool terminal_screen_start(const agent_terminal_t *target) {
  if (!target || job.pid > 0)
    return false;
  char pane[32];
  if (target->kind == TERMINAL_TMUX &&
      agent_terminal_socket_ok(target->socket)) {
    snprintf(pane, sizeof(pane), "%%%" PRIu64, target->pane);
    // No -S/-E: capture only the visible screen, never scrollback history.
    const char *args[] = {"tmux", "-S", target->socket, "capture-pane",
                          "-p",   "-t", pane,           NULL};
    if (job_start(&job, args) < 0)
      return false;
  } else if (target->kind == TERMINAL_KITTY &&
             agent_terminal_listen_ok(target->socket)) {
    snprintf(pane, sizeof(pane), "id:%" PRIu64, target->pane);
    const char *args[] = {
        "kitty",   "@",  "--to",     target->socket, "get-text",
        "--match", pane, "--extent", "screen",       "--add-wrap-markers",
        NULL};
    if (job_start(&job, args) < 0)
      return false;
  } else {
    return false;
  }
  if (agent_watch_listen(job.fd, SCREEN_TOKEN, EPOLLIN | EPOLLHUP) < 0) {
    terminal_screen_cleanup();
    return false;
  }
  return true;
}

int terminal_screen_poll(bool *waiting) {
  if (!waiting || job.pid <= 0)
    return -1;
  int fd = job.fd;
  int result = job_process(&job);
  if (!result) {
    if (job.eof)
      agent_watch_unlisten(fd);
    return 0;
  }
  agent_watch_unlisten(fd);
  *waiting = result > 0 && cursor_approval_screen(job.buffer, job.used);
  // Screen text has no consumer beyond the pure matcher, even on failure.
  explicit_bzero(job.buffer, sizeof(job.buffer));
  job.used = 0;
  return result;
}

int terminal_screen_timeout(int64_t now_ms) {
  if (job.pid <= 0)
    return -1;
  int64_t left = job.deadline - now_ms;
  if (left <= 0)
    return 0;
  // EOF can precede waitpid's exit observation. Unlisten and reap soon.
  return job.eof && left > 10 ? 10 : (int)left;
}

void terminal_screen_cleanup(void) {
  agent_watch_unlisten(job.fd);
  job_cleanup(&job);
  explicit_bzero(job.buffer, sizeof(job.buffer));
  job.used = 0;
}

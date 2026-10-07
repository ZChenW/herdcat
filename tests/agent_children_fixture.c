// Socket fixture for the production session parser, model and pidfd loop.
// proc_root injection never becomes an environment override in the product.
#define _GNU_SOURCE
#include "core/agent_sessions.h"
#include "graphics/sign_names.h"
#include "platform/agent_watch.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static bool contains(const pid_t *pids, int count, pid_t pid) {
  for (int i = 0; i < count; i++)
    if (pids[i] == pid)
      return true;
  return false;
}
static void reconcile(const pid_t *before, int n) {
  pid_t after[AGENT_SESSIONS_MAX];
  int m = agent_sessions_pids(after, AGENT_SESSIONS_MAX);
  for (int i = 0; i < n; i++)
    if (!contains(after, m, before[i]))
      agent_watch_remove(before[i]);
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < count; i++)
    if (views[i].pid > 0) {
      int result = agent_watch_add(views[i].pid);
      agent_sessions_set_watched(views[i].key, result == 0);
      if (result < 0 && errno == ESRCH)
        agent_sessions_remove_pid(views[i].pid);
    }
}
int main(int argc, char **argv) {
  if (argc != 3 || agent_watch_init() < 0)
    return 1;
  int socket = atoi(argv[2]);  // inherited SOCK_SEQPACKET from test driver
  agent_sessions_reset();
  struct pollfd fds[2] = {
      {.fd = socket,           .events = POLLIN},
      {.fd = agent_watch_fd(), .events = POLLIN}
  };
  for (;;) {
    if (poll(fds, 2, 100) < 0)
      return 1;
    pid_t before[AGENT_SESSIONS_MAX];
    int n = agent_sessions_pids(before, AGENT_SESSIONS_MAX);
    agent_watch_process(agent_sessions_remove_pid);
    agent_sessions_expire(now_ms(), 60);
    reconcile(before, n);
    if (!(fds[0].revents & POLLIN))
      continue;
    char request[1280], response[4096] = "failed";
    ssize_t bytes = recv(socket, request, sizeof(request) - 1, 0);
    if (bytes <= 0)
      break;
    request[bytes] = 0;
    if (!strcmp(request, "stop"))
      break;
    char agent[9];
    agent_event_t event;
    uint64_t key;
    pid_t pid, candidate, owner;
    bool metadata;
    n = agent_sessions_pids(before, AGENT_SESSIONS_MAX);
    if (agent_event_owner_request(request, &key, agent, &event, &pid,
                                  &candidate, &metadata, &owner)) {
      if (!agent_sessions_apply_owned(key, agent, event, pid, candidate,
                                      metadata, owner, argv[1], now_ms(), 1)) {
        strcpy(response, "ok");
      }
    } else if (!strncmp(request, "name ", 5)) {
      char *end;
      key = strtoull(request + 5, &end, 16);
      if (end - request == 21 && *end == ' ' &&
          !agent_sessions_set_name(key, end + 1)) {
        agent_sessions_process(key, 0, true, argv[1]);
        strcpy(response, "ok");
      }
    } else if (!strcmp(request, "sessions")) {
      agent_sessions_format(response, sizeof(response), now_ms());
    } else if (!strcmp(request, "labels")) {
      agent_session_view_t views[AGENT_SESSIONS_MAX];
      int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
      size_t used = 0;
      response[0] = 0;
      for (int i = 0; i < count; i++) {
        if (views[i].parent)
          continue;
        char fan[64], post[64];
        sign_agent_label(&views[i], false, fan);
        sign_agent_label(&views[i], true, post);
        used += (size_t)snprintf(response + used, sizeof(response) - used,
                                 "%s | %s\n", fan, post);
      }
    } else if (!strcmp(request, "state")) {
      snprintf(response, sizeof(response), "%s",
               agent_state_name(agent_sessions_resolve()));
    }
    reconcile(before, n);
    if (send(socket, response, strlen(response), MSG_NOSIGNAL) < 0)
      break;
  }
  agent_watch_cleanup();
  close(socket);
  return 0;
}

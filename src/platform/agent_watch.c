#define _GNU_SOURCE
#include "platform/agent_watch.h"

#include "core/agent_sessions.h"

#include <errno.h>
#include <sys/epoll.h>
#include <sys/pidfd.h>
#include <unistd.h>

typedef struct {
  pid_t pid;
  int fd;
} process_watch_t;

static int epoll_fd = -1;
static process_watch_t watches[AGENT_SESSIONS_MAX];

int agent_watch_init(void) {
  if (epoll_fd >= 0) {
    return 0;
  }
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  return epoll_fd >= 0 ? 0 : -1;
}

void agent_watch_cleanup(void) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (watches[i].pid > 0) {
      close(watches[i].fd);
      watches[i] = (process_watch_t){0};
    }
  }
  if (epoll_fd >= 0) {
    close(epoll_fd);
    epoll_fd = -1;
  }
}

int agent_watch_fd(void) {
  return epoll_fd;
}

int agent_watch_add(pid_t pid) {
  if (epoll_fd < 0 || pid <= 0) {
    errno = EINVAL;
    return -1;
  }
  int slot = -1;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (watches[i].pid == pid) {
      return 0;
    }
    if (!watches[i].pid && slot < 0) {
      slot = i;
    }
  }
  if (slot < 0) {
    errno = ENOSPC;
    return -1;
  }
  int fd = pidfd_open(pid, 0);
  if (fd < 0) {
    return -1;
  }
  struct epoll_event event = {.events = EPOLLIN, .data.u32 = (uint32_t)pid};
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
  }
  watches[slot] = (process_watch_t){.pid = pid, .fd = fd};
  return 0;
}

void agent_watch_remove(pid_t pid) {
  if (pid <= 0) {
    return;
  }
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    if (watches[i].pid == pid) {
      epoll_ctl(epoll_fd, EPOLL_CTL_DEL, watches[i].fd, NULL);
      close(watches[i].fd);
      watches[i] = (process_watch_t){0};
      return;
    }
  }
}

void agent_watch_process(void (*exited)(pid_t pid)) {
  if (epoll_fd < 0) {
    return;
  }
  struct epoll_event ready[AGENT_SESSIONS_MAX];
  int count;
  do {
    count = epoll_wait(epoll_fd, ready, AGENT_SESSIONS_MAX, 0);
  } while (count < 0 && errno == EINTR);
  for (int i = 0; i < count; i++) {
    pid_t pid = (pid_t)ready[i].data.u32;
    agent_watch_remove(pid);
    if (exited) {
      exited(pid);
    }
  }
}

#define _GNU_SOURCE
#include "platform/agent_watch.h"

#include "core/agent_sessions.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/pidfd.h>
#include <sys/types.h>
#include <unistd.h>

#define EXTRA_WATCHES 6

typedef struct {
  pid_t pid;
  int fd;
} process_watch_t;
typedef struct {
  int fd;
  uint32_t token;
} extra_watch_t;

static int epoll_fd = -1;
static process_watch_t watches[AGENT_SESSIONS_MAX];
static extra_watch_t extras[EXTRA_WATCHES];
static void (*extra_ready)(uint32_t token);

static void clear_extras(void) {
  for (int i = 0; i < EXTRA_WATCHES; i++) {
    extras[i].fd = -1;
    extras[i].token = 0;
  }
}

int agent_watch_init(void) {
  if (epoll_fd >= 0) {
    return 0;
  }
  clear_extras();
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
  clear_extras();
  extra_ready = NULL;
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
  // The upper half distinguishes extra fds; initialize the entire union.
  struct epoll_event event = {.events = EPOLLIN, .data.u64 = (uint64_t)pid};
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

void agent_watch_retain(const pid_t *pids, size_t count) {
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    pid_t pid = watches[i].pid;
    bool keep = false;
    for (size_t j = 0; pids && j < count; j++)
      keep |= pids[j] == pid;
    if (pid > 0 && !keep)
      agent_watch_remove(pid);
  }
}

int agent_watch_listen(int fd, uint32_t token, uint32_t events) {
  if (epoll_fd < 0 || fd < 0) {
    errno = EINVAL;
    return -1;
  }
  int slot = -1;
  for (int i = 0; i < EXTRA_WATCHES; i++) {
    if (extras[i].fd == fd) {
      return 0;
    }
    if (extras[i].fd < 0 && slot < 0) {
      slot = i;
    }
  }
  if (slot < 0) {
    errno = ENOSPC;
    return -1;
  }
  struct epoll_event event = {.events = events,
                              .data.u64 = (1ULL << 32) | token};
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
    return -1;
  }
  extras[slot] = (extra_watch_t){.fd = fd, .token = token};
  return 0;
}

void agent_watch_unlisten(int fd) {
  if (epoll_fd < 0 || fd < 0) {
    return;
  }
  for (int i = 0; i < EXTRA_WATCHES; i++) {
    if (extras[i].fd == fd) {
      epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
      extras[i].fd = -1;
      extras[i].token = 0;
      return;
    }
  }
}

void agent_watch_on_ready(void (*ready)(uint32_t token)) {
  extra_ready = ready;
}

void agent_watch_process(void (*exited)(pid_t pid)) {
  if (epoll_fd < 0) {
    return;
  }
  struct epoll_event ready[AGENT_SESSIONS_MAX + EXTRA_WATCHES];
  int count;
  do {
    count = epoll_wait(epoll_fd, ready, AGENT_SESSIONS_MAX + EXTRA_WATCHES, 0);
  } while (count < 0 && errno == EINTR);
  for (int i = 0; i < count; i++) {
    uint64_t tag = ready[i].data.u64;
    if (tag >> 32) {
      if (extra_ready) {
        extra_ready((uint32_t)tag);
      }
      continue;
    }
    pid_t pid = (pid_t)tag;
    agent_watch_remove(pid);
    if (exited) {
      exited(pid);
    }
  }
}

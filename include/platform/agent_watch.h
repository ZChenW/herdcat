#ifndef AGENT_WATCH_H
#define AGENT_WATCH_H

#include <stdint.h>
#include <sys/types.h>

int agent_watch_init(void);
void agent_watch_cleanup(void);
int agent_watch_fd(void);
// Duplicate pids share a watch. Failure leaves the caller in stale fallback.
int agent_watch_add(pid_t pid);
void agent_watch_remove(pid_t pid);
// Share the epoll the event loop already polls. The caller owns fd and must
// read it to EAGAIN inside ready. events are epoll flags.
int agent_watch_listen(int fd, uint32_t token, uint32_t events);
void agent_watch_unlisten(int fd);
void agent_watch_on_ready(void (*ready)(uint32_t token));
// Drain ready process exits without blocking; remove each watch before
// callback. Extra fds stay registered and are handed to the ready callback.
void agent_watch_process(void (*exited)(pid_t pid));

#endif  // AGENT_WATCH_H

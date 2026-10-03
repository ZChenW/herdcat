#ifndef AGENT_WATCH_H
#define AGENT_WATCH_H

#include <sys/types.h>

int agent_watch_init(void);
void agent_watch_cleanup(void);
int agent_watch_fd(void);
// Duplicate pids share a watch. Failure leaves the caller in stale fallback.
int agent_watch_add(pid_t pid);
void agent_watch_remove(pid_t pid);
// Drain ready process exits without blocking; remove each watch before
// callback.
void agent_watch_process(void (*exited)(pid_t pid));

#endif  // AGENT_WATCH_H

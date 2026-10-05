#ifndef AGENT_DISCOVER_H
#define AGENT_DISCOVER_H

#include "platform/focus.h"

#include <stddef.h>
#include <stdint.h>

#define AGENT_DISCOVER_PROCESS_MAX 4096
#define AGENT_DISCOVER_CREATE_MAX  5

// One pass over proc_root. At most max_processes numeric pids are inspected,
// and at most max_new idle sessions are created. Returns how many were created.
int agent_discover_scan(const char *proc_root, const focus_window_t *windows,
                        size_t window_count, int max_processes, int max_new,
                        int64_t now_ms, int done_timeout_s);

#endif

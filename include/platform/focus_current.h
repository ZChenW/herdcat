#ifndef FOCUS_CURRENT_H
#define FOCUS_CURRENT_H

#include "core/agent_sessions.h"
#include "platform/focus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define FOCUS_PANE_MAX 16

typedef struct {
  pid_t pid;
  uint64_t split;
} focus_pane_t;

// Decimal tokens: no sign, no leading zero, pid from 2 to 4194304.
bool focus_pane_fields(const char *pid_text, const char *split_text, pid_t *pid,
                       uint64_t *split);
// "pane <pid> <split>" and nothing else.
bool focus_pane_parse(const char *request, pid_t *pid, uint64_t *split);

void focus_pane_reset(void);
// Rejects pid <= 1 and a zero split. At most FOCUS_PANE_MAX processes.
bool focus_pane_set(pid_t pid, uint64_t split);
bool focus_pane_get(pid_t pid, uint64_t *split);
size_t focus_pane_copy(focus_pane_t *out, size_t capacity);

// One click for one niri window. observe() drops it when focus leaves.
void focus_current_reset(void);
void focus_current_click(uint64_t window, uint64_t key);
void focus_current_observe(uint64_t window);
void focus_current_clicked(uint64_t *window, uint64_t *key);

// First answer wins: reported split, then the click, then the newest
// session. A report that matches no session answers 0.
uint64_t focus_current_choose(uint64_t focused, const focus_window_t *windows,
                              size_t windows_count,
                              const agent_session_view_t *sessions,
                              size_t count, const focus_pane_t *panes,
                              size_t pane_count, uint64_t clicked_window,
                              uint64_t clicked_key);
// A split report narrows this to that split. Otherwise every session in
// the window. The click is ignored.
int focus_current_seen(uint64_t focused, const focus_window_t *windows,
                       size_t windows_count,
                       const agent_session_view_t *sessions, size_t count,
                       const focus_pane_t *panes, size_t pane_count,
                       uint64_t *keys, size_t capacity);

// Both answers from one ancestry lookup per session. No ancestry survives
// this call, so reparenting and window/PID changes are seen on the next query.
int focus_current_query(uint64_t focused, const focus_window_t *windows,
                        size_t windows_count,
                        const agent_session_view_t *sessions, size_t count,
                        const focus_pane_t *panes, size_t pane_count,
                        uint64_t clicked_window, uint64_t clicked_key,
                        uint64_t *keys, size_t capacity, uint64_t *chosen);

#endif

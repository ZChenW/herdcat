#ifndef HERDCAT_TERMINAL_SCREEN_H
#define HERDCAT_TERMINAL_SCREEN_H

#include "platform/agent_terminal.h"

// Reuse the click-to-jump location; tmux takes precedence over outer kitty.
bool terminal_screen_target(pid_t pid, agent_terminal_t *target);
bool terminal_screen_start(const agent_terminal_t *target);
// 0 pending, 1 boolean result, -1 unavailable/failed. Never returns screen
// text.
int terminal_screen_poll(bool *waiting);
int terminal_screen_timeout(int64_t now_ms);
void terminal_screen_cleanup(void);

#endif

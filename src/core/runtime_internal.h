#ifndef HERDCAT_RUNTIME_INTERNAL_H
#define HERDCAT_RUNTIME_INTERNAL_H
#include "config/config.h"
#include "platform/focus.h"

#include <signal.h>
extern volatile sig_atomic_t running;
extern config_t config;
extern char *config_path;
extern int64_t rest_deadline;
int64_t monotonic_ms(void);
int reload(void);
void agent_refresh(void);
void discover_expanded(void);
bool kitty_for_session(pid_t pid, uint64_t *window, char *listen,
                       size_t capacity);
void terminal_resolved(pid_t pid, const agent_terminal_t *terminal);
void terminal_current(pid_t pid, uint64_t window, const char *socket,
                      const focus_wezterm_pane_t *panes, size_t count);
int command(const char *request, char *response, size_t capacity);
void note_key(void);
void note_window_focus(void);
void note_window_rest(void);
void resolve_restored_terminals(void);
#endif

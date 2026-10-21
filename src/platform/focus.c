#define _GNU_SOURCE
#include "platform/focus.h"

#include "platform/agent_terminal.h"
#include "platform/command_job.h"
#include "platform/compositor.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

enum {
  JOB_WINDOWS = 0,
  JOB_NIRI,
  JOB_HYPR_LUA,
  JOB_KITTY,
  JOB_TMUX_SESSION,
  JOB_TMUX_CLIENTS,
  JOB_TMUX_SWITCH,
  JOB_WEZ_ACTIVATE,
  JOB_WEZ_LIST,
  JOB_WEZ_CURRENT
};

static command_job_t job = {.fd = -1};
static pid_t target_pid;
static int job_kind;
static bool dispatching;
static focus_result_t result;
static focus_kitty_fn_t kitty_lookup;
static focus_terminal_fn_t terminal_lookup;
static focus_title_fn_t title_lookup;
static char target_title[97];
static focus_terminal_note_fn_t terminal_note;
static focus_current_fn_t current_note;
static agent_terminal_t terminal;
static char target_name[48], tmux_session[256], tmux_tty[128];
static bool foreground;
static bool kitty_client;
static int64_t focus_deadline;

typedef struct {
  pid_t pid;
  uint64_t window;
  bool current;
} request_t;
static request_t requests[64];
static size_t request_count;
typedef struct {
  uint64_t window;
  int64_t queried;
} current_stamp_t;
static current_stamp_t current_stamps[16];
static uint64_t current_window;
static void start_next(void);
static bool start_windows(void);
static bool start_tmux_switch(void);

void focus_set_title(focus_title_fn_t lookup) {
  title_lookup = lookup;
}
void focus_set_current(focus_current_fn_t note) {
  current_note = note;
}
void focus_set_terminal(focus_terminal_fn_t lookup,
                        focus_terminal_note_fn_t note) {
  terminal_lookup = lookup;
  terminal_note = note;
}
static void queue_request(pid_t pid, uint64_t window, bool current) {
  if (!terminal_lookup || pid <= 1)
    return;
  for (size_t i = 0; i < request_count; i++)
    if (requests[i].pid == pid && requests[i].current == current &&
        (!current || requests[i].window == window))
      return;
  if (request_count < sizeof(requests) / sizeof(requests[0]))
    requests[request_count++] = (request_t){pid, window, current};
  if (!job.pid && !dispatching)
    start_next();
}
void focus_terminal_resolve(pid_t pid) {
  queue_request(pid, 0, false);
}
void focus_wezterm_current(pid_t pid, uint64_t window) {
  if (!window)
    return;
  size_t oldest = 0;
  int64_t now = now_ms();
  for (size_t i = 0; i < 16; i++) {
    if (current_stamps[i].window == window) {
      if (now - current_stamps[i].queried < 500)
        return;
      oldest = i;
      break;
    }
    if (current_stamps[i].queried < current_stamps[oldest].queried)
      oldest = i;
  }
  current_stamps[oldest] = (current_stamp_t){window, now};
  agent_terminal_t t;
  if (current_note && terminal_lookup && terminal_lookup(pid, &t, NULL, 0) &&
      t.kind == TERMINAL_WEZTERM)
    current_note(pid, window, t.socket, NULL, 0);
  queue_request(pid, window, true);
}

void focus_set_kitty(focus_kitty_fn_t fn) {
  kitty_lookup = fn;
}
bool focus_kitty_target(const char *window_text, const char *listen,
                        char *match, size_t capacity) {
  uint64_t window = 0;
  char socket[AGENT_TERMINAL_LISTEN_MAX + 1];
  if (!match || !agent_terminal_accept(window_text, listen, &window, socket,
                                       sizeof(socket)))
    return false;
  int wrote = snprintf(match, capacity, "id:%" PRIu64, window);
  return wrote > 0 && (size_t)wrote < capacity;
}
static bool start_kitty(void) {
  if (!kitty_lookup)
    return false;
  uint64_t window = 0;
  char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
  char text[32], match[32];
  if (terminal.kind == TERMINAL_TMUX) {
    if (!agent_terminal_lookup("/proc", terminal.client_pid, &window, listen,
                               sizeof(listen), NULL))
      return false;
  } else if (!kitty_lookup(target_pid, &window, listen, sizeof(listen))) {
    return false;
  }
  int wrote = snprintf(text, sizeof(text), "%" PRIu64, window);
  if (wrote < 0 || (size_t)wrote >= sizeof(text) ||
      !focus_kitty_target(text, listen, match, sizeof(match)))
    return false;
  const char *args[] = {"kitten",       "@",       "--to", listen,
                        "focus-window", "--match", match,  NULL};
  if (job_start(&job, args) < 0)
    return false;
  job_kind = JOB_KITTY;
  return true;
}
#ifdef TEST_BUILD
static bool test_available;
void focus_test_available(bool available) {
  test_available = available;
}
#endif
bool focus_available(void) {
#ifdef TEST_BUILD
  if (test_available)
    return true;
#endif
  const compositor_ops_t *ops = compositor_selected();
  return ops && ops->detect();
}
static bool start_kind(int kind, const char *const argv[]) {
  command_socket =
      terminal.kind == TERMINAL_WEZTERM && !strcmp(argv[0], "wezterm")
          ? terminal.socket
          : NULL;
  int error = job_start(&job, argv);
  command_socket = NULL;
  if (error < 0)
    return false;
  job_kind = kind;
  return true;
}
static bool start_windows(void) {
  const compositor_ops_t *ops = compositor_selected();
  if (!ops)
    ops = &COMPOSITOR_NIRI;
  const char *args[8] = {0};
  ops->windows(args);
  return start_kind(JOB_WINDOWS, args);
}
static bool start_tmux_switch(void) {
  char pane[32];
  snprintf(pane, sizeof(pane), "%%%ju", (uintmax_t)terminal.pane);
  const char *args[] = {
      "tmux", "-S", terminal.socket, "switch-client", "-c", tmux_tty, "-t",
      pane,   NULL};
  return start_kind(JOB_TMUX_SWITCH, args);
}
static bool start_terminal(bool activate) {
  if (terminal.kind == TERMINAL_TMUX) {
    char pane[32];
    snprintf(pane, sizeof(pane), "%%%ju", (uintmax_t)terminal.pane);
    const char *args[] = {"tmux",
                          "-S",
                          terminal.socket,
                          "display-message",
                          "-p",
                          "-t",
                          pane,
                          "#{session_name}",
                          NULL};
    return start_kind(JOB_TMUX_SESSION, args);
  }
  if (terminal.kind == TERMINAL_WEZTERM) {
    if (activate) {
      char pane[32];
      snprintf(pane, sizeof(pane), "%ju", (uintmax_t)terminal.pane);
      const char *args[] = {"wezterm",   "cli", "activate-pane",
                            "--pane-id", pane,  NULL};
      return start_kind(JOB_WEZ_ACTIVATE, args);
    }
    const char *args[] = {"wezterm", "cli", "list", "--format", "json", NULL};
    return start_kind(JOB_WEZ_LIST, args);
  }
  return foreground && start_windows();
}
static void start_next(void) {
  while (!job.pid && request_count) {
    request_t request = requests[0];
    memmove(requests, requests + 1, --request_count * sizeof(requests[0]));
    terminal = (agent_terminal_t){.kind = TERMINAL_NONE};
    target_pid = request.pid;
    target_title[0] = 0;
    if (title_lookup)
      title_lookup(target_pid, target_title, sizeof(target_title));
    foreground = false;
    if (!terminal_lookup(target_pid, &terminal, target_name,
                         sizeof(target_name)))
      continue;
    if (request.current && terminal.kind == TERMINAL_WEZTERM) {
      current_window = request.window;
      const char *args[] = {"wezterm",  "cli",  "list-clients",
                            "--format", "json", NULL};
      if (start_kind(JOB_WEZ_CURRENT, args))
        break;
    } else if (start_terminal(false)) {
      break;
    }
  }
}
int focus_session_window(pid_t pid) {
  if (job.pid || pid <= 1 || !focus_available())
    return -1;
  target_pid = pid;
  target_title[0] = 0;
  if (title_lookup)
    title_lookup(pid, target_title, sizeof(target_title));
  terminal = (agent_terminal_t){.kind = TERMINAL_NONE};
  agent_terminal_t found;
  if (terminal_lookup &&
      terminal_lookup(pid, &found, target_name, sizeof(target_name)))
    terminal = found;
  if (terminal.kind == TERMINAL_WEZTERM) {
    terminal.window = 0;
    terminal.current_known = false;
  }
  foreground = true;
  kitty_client = false;
  result = FOCUS_PENDING;
  if (!start_terminal(true))
    return -1;
  focus_deadline = job.deadline;
  return 0;
}

bool focus_tmux_client(const char *text, const char *session, pid_t *pid,
                       char *tty, size_t capacity) {
  if (!text || !session || !pid || !tty)
    return false;
  // A client already showing the pane's session is preferred. With none,
  // the most recently used client of the server is switched over to it:
  // switch-client reaches a pane in any session.
  bool found = false, attached = false;
  uint64_t newest = 0;
  while (*text) {
    const char *end = strchr(text, '\n');
    size_t n = end ? (size_t)(end - text) : strlen(text);
    if (n < 1024) {
      char line[1024];
      memcpy(line, text, n);
      line[n] = 0;
      char *activity = strrchr(line, ' ');
      if (activity) {
        *activity++ = 0;
        char *tty_text = strrchr(line, ' ');
        char *session_text = strchr(line, ' ');
        if (tty_text && session_text && tty_text > session_text) {
          *tty_text++ = 0;
          *session_text++ = 0;
          uint64_t client, when;
          if (agent_terminal_number(line, false, &client) && client > 1 &&
              client <= 4194304 &&
              agent_terminal_number(activity, false, &when) &&
              !strncmp(tty_text, "/dev/", 5) && strlen(tty_text) < capacity &&
              !strpbrk(tty_text, "\r\t")) {
            bool same = !strcmp(session_text, session);
            if (!found || (same && !attached) ||
                (same == attached && when > newest)) {
              newest = when;
              attached = same;
              *pid = (pid_t)client;
              memcpy(tty, tty_text, strlen(tty_text) + 1);
              found = true;
            }
          }
        }
      }
    }
    if (!end)
      break;
    text = end + 1;
  }
  return found;
}

static void finish(focus_result_t value) {
  if (foreground)
    result = value;
  start_next();
}
static void complete_failed_job(void) {
  // Lua-configured Hyprland explicitly rejects the legacy dispatcher.
  // Retry once with a typed selector, sharing the original job deadline.
  if (job_kind == JOB_NIRI && compositor_selected() == &COMPOSITOR_HYPRLAND &&
      job.exited && WIFEXITED(job.status) && WEXITSTATUS(job.status) != 0 &&
      strstr(job.buffer,
             "dispatch in lua is a shorthand for hl.dispatch(...)")) {
    int64_t deadline = job.deadline;
    char text[80];
    int n = snprintf(text, sizeof(text),
                     "hl.dsp.focus({window=\"address:0x%" PRIx64 "\"})",
                     terminal.window);
    const char *args[] = {"hyprctl", "dispatch", text, NULL};
    if (now_ms() < deadline && n > 0 && (size_t)n < sizeof(text) &&
        start_kind(JOB_HYPR_LUA, args)) {
      job.deadline = deadline;
      return;
    }
  }
  if (job_kind == JOB_WEZ_CURRENT && current_note)
    current_note(target_pid, current_window, terminal.socket, NULL, 0);
  finish(FOCUS_UNAVAILABLE);
}

static void complete_tmux_session(void) {
  size_t n = job.used;
  while (n && (job.buffer[n - 1] == '\n' || job.buffer[n - 1] == '\r'))
    n--;
  if (!n || n >= sizeof(tmux_session) || memchr(job.buffer, '\n', n)) {
    finish(FOCUS_NOT_FOUND);
    return;
  }
  memcpy(tmux_session, job.buffer, n);
  tmux_session[n] = 0;
  const char *args[] = {
      "tmux",
      "-S",
      terminal.socket,
      "list-clients",
      "-F",
      "#{client_pid} #{client_session} #{client_tty} #{client_activity}",
      NULL};
  if (!start_kind(JOB_TMUX_CLIENTS, args))
    finish(FOCUS_UNAVAILABLE);
}

static void complete_tmux_clients(void) {
  terminal.client_pid = 0;
  bool found = focus_tmux_client(job.buffer, tmux_session, &terminal.client_pid,
                                 tmux_tty, sizeof(tmux_tty));
  // An empty successful reply means no clients. Malformed output is not
  // evidence of detachment and must not change the last known state.
  if (!found && job.used && strspn(job.buffer, "\r\n \t") != job.used) {
    finish(FOCUS_UNAVAILABLE);
    return;
  }
  terminal.detached = !found;
  char kitty_socket[AGENT_TERMINAL_LISTEN_MAX + 1];
  terminal.outer_kitty_pid = 0;
  terminal.outer_kitty_pane = 0;
  if (found)
    agent_terminal_lookup("/proc", terminal.client_pid,
                          &terminal.outer_kitty_pane, kitty_socket,
                          sizeof(kitty_socket), &terminal.outer_kitty_pid);
  if (terminal_note)
    terminal_note(target_pid, &terminal);
  if (!found) {
    finish(FOCUS_NOT_FOUND);
  } else if (!foreground) {
    finish(FOCUS_SUCCESS);
  } else if (!start_windows()) {
    finish(FOCUS_UNAVAILABLE);
  }
}

static void complete_wezterm_list(void) {
  focus_wezterm_pane_t panes[256];
  int count = focus_parse_wezterm(job.buffer, job.used,
                                  job_kind == JOB_WEZ_CURRENT, panes, 256);
  if (job_kind == JOB_WEZ_CURRENT) {
    if (current_note)
      current_note(target_pid, current_window, terminal.socket, panes,
                   count > 0 ? (size_t)count : 0);
    finish(count >= 0 ? FOCUS_SUCCESS : FOCUS_UNAVAILABLE);
    return;
  }
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (panes[i].pane == terminal.pane) {
      memcpy(terminal.title, panes[i].title, sizeof(terminal.title));
      terminal.native_window = panes[i].window;
      terminal.native_window_known = panes[i].has_window;
      found = true;
      break;
    }
  }
  if (!found) {
    finish(count < 0 ? FOCUS_UNAVAILABLE : FOCUS_NOT_FOUND);
  } else {
    if (terminal_note)
      terminal_note(target_pid, &terminal);
    if (!foreground)
      finish(FOCUS_SUCCESS);
    else if (!start_windows())
      finish(FOCUS_UNAVAILABLE);
  }
}

static void complete_window_lookup(void) {
  focus_window_t windows[256];
  uint64_t id;
  const compositor_ops_t *ops = compositor_selected();
  if (!ops)
    ops = &COMPOSITOR_NIRI;
  int count = ops->parse_windows(job.buffer, job.used, windows, 256);
  if (count < 0) {
    finish(FOCUS_UNAVAILABLE);
    return;
  }
  if (!focus_terminal_window_title(target_pid, &terminal, target_name,
                                   target_title, windows, (size_t)count, &id)) {
    finish(FOCUS_NOT_FOUND);
    return;
  }
  terminal.window = id;
  if (terminal_note)
    terminal_note(target_pid, &terminal);
  char identifier[64];
  const char *args[8] = {0};
  if (!ops->focus_window(id, args, identifier, sizeof(identifier)) ||
      !start_kind(JOB_NIRI, args)) {
    finish(FOCUS_UNAVAILABLE);
    return;
  }
  if (terminal.kind == TERMINAL_NONE)
    job.deadline = focus_deadline;
}

static void job_completed(int done) {
  // Preserve kitty's existing best-effort result after niri succeeds.
  if (job_kind == JOB_KITTY) {
    if (kitty_client) {
      kitty_client = false;
      if (!start_tmux_switch())
        finish(FOCUS_UNAVAILABLE);
    } else {
      finish(FOCUS_SUCCESS);
    }
    return;
  }
  if (done < 0) {
    complete_failed_job();
    return;
  }
  if (job_kind == JOB_TMUX_SESSION) {
    complete_tmux_session();
    return;
  }
  if (job_kind == JOB_TMUX_CLIENTS) {
    complete_tmux_clients();
    return;
  }
  if (job_kind == JOB_WEZ_ACTIVATE) {
    if (!start_terminal(false))
      finish(FOCUS_UNAVAILABLE);
    return;
  }
  if (job_kind == JOB_WEZ_LIST || job_kind == JOB_WEZ_CURRENT) {
    complete_wezterm_list();
    return;
  }
  if (job_kind == JOB_TMUX_SWITCH) {
    if (terminal_note)
      terminal_note(target_pid, &terminal);
    finish(FOCUS_SUCCESS);
    return;
  }
  if (job_kind == JOB_NIRI || job_kind == JOB_HYPR_LUA) {
    if (terminal.kind == TERMINAL_WEZTERM) {
      terminal.current_known = true;
      terminal.current_pane = terminal.pane;
      if (terminal_note)
        terminal_note(target_pid, &terminal);
    }
    if (terminal.kind == TERMINAL_TMUX) {
      if (start_kitty())
        kitty_client = true;
      else if (!start_tmux_switch())
        finish(FOCUS_UNAVAILABLE);
    } else if (!start_kitty()) {
      finish(FOCUS_SUCCESS);
    }
    return;
  }
  complete_window_lookup();
}
void focus_poll(void) {
  if (!job.pid) {
    start_next();
    return;
  }
  int done = job_process(&job);
  if (!done)
    return;
  // Completion callbacks may enqueue work, but must not replace the active
  // command chain while its completed output is still being dispatched.
  dispatching = true;
  job_completed(done);
  dispatching = false;
  if (!job.pid)
    start_next();
}
int focus_poll_fd(void) {
  return job.eof ? -1 : job.fd;
}
int focus_timeout(void) {
  if (!job.pid)
    return -1;
  int64_t left = job.deadline - now_ms();
  if (left <= 0)
    return 0;
  return job.eof && left > 10 ? 10 : (int)left;
}
focus_result_t focus_take_result(void) {
  focus_result_t value = result;
  result = FOCUS_PENDING;
  return value;
}
void focus_cleanup(void) {
  job_cleanup(&job);
  result = FOCUS_PENDING;
  job_kind = JOB_WINDOWS;
  request_count = 0;
  dispatching = false;
  memset(current_stamps, 0, sizeof(current_stamps));
}

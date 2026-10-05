#define _GNU_SOURCE
#include "config/config.h"
#include "config/sign_options.h"
#include "core/agent_hook.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "core/bongocat.h"
#include "core/control.h"
#include "graphics/animation.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/agent_discover.h"
#include "platform/agent_terminal.h"
#include "platform/agent_watch.h"
#include "platform/focus.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "platform/hyprland.h"
#include "platform/input.h"
#include "platform/overlay_signs.h"
#include "platform/prefs.h"
#include "platform/session_store.h"
#include "platform/transcript_watch.h"
#include "platform/wayland.h"
#include "utils/error.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static int signal_fd = -1;
static config_t config;
static ConfigWatcher watcher = {.inotify_fd = -1, .watch_fd = -1};
static char *config_path;
static const char *monitor_override;
static bool hidden, paused;
static bool reload_pending;
static int64_t input_retry_at;
static int64_t monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

static void stop_signal(int signal) {
  (void)signal;
  int saved = errno;
  running = 0;
  uint64_t value = 1;
  if (signal_fd >= 0) {
    ssize_t result = write(signal_fd, &value, sizeof(value));
    (void)result;
  }
  errno = saved;
}
static int setup_signals(void) {
  signal_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (signal_fd < 0) {
    return -1;
  }
  struct sigaction action = {.sa_handler = stop_signal};
  sigemptyset(&action.sa_mask);
  int signals[] = {SIGTERM, SIGINT, SIGQUIT, SIGHUP};
  for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
    if (sigaction(signals[i], &action, NULL) < 0) {
      return -1;
    }
  }
  signal(SIGPIPE, SIG_IGN);
  return 0;
}
static bool arrays_equal(char **a, int na, char **b, int nb) {
  if (na != nb) {
    return false;
  }
  for (int i = 0; i < na; i++) {
    if (strcmp(a[i], b[i]) != 0) {
      return false;
    }
  }
  return true;
}
static bongocat_error_t force_monitor(config_t *settings) {
  if (!monitor_override) {
    return BONGOCAT_SUCCESS;
  }
  char *name = strdup(monitor_override);
  if (!name) {
    return BONGOCAT_ERROR_MEMORY;
  }
  free(settings->output_name);
  settings->output_name = name;
  for (int i = 0; i < settings->num_output_names; i++) {
    free(settings->output_names[i]);
  }
  free((void *)settings->output_names);
  settings->output_names = NULL;
  settings->num_output_names = 0;
  return BONGOCAT_SUCCESS;
}
static void sign_policy(void) {
  transcript_watch_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_sessions_configure_done(config.sign_done == SIGN_DONE_STICKY,
                                monotonic_ms(), config.agent_done_timeout);
}
static int reload(void) {
  config_t next = {0};
  bongocat_error_t result = load_config_strict(&next, config_path);
  if (result == BONGOCAT_SUCCESS) {
    result = force_monitor(&next);
  }
  if (result != BONGOCAT_SUCCESS) {
    config_cleanup_full(&next);
    bongocat_error_init(config.enable_debug);
    bongocat_log_warning("Reload rejected; keeping current configuration");
    return 1;
  }
  bool input_changed =
      (next.hotplug_scan_interval != config.hotplug_scan_interval ||
       !arrays_equal(next.keyboard_devices, next.num_keyboard_devices,
                     config.keyboard_devices, config.num_keyboard_devices) ||
       !arrays_equal(next.keyboard_names, next.num_names, config.keyboard_names,
                     config.num_names)) != 0;
  config_t old = config;
  config = next;
  if (prefs_resolve(&config.sign_style, &config.sign_language, config.sign_font,
                    sizeof(config.sign_font)))
    bongocat_log_warning("Menu preferences were not updated");
  overlay_signs_use_config();
  sign_policy();
  if (strcmp(old.sign_font, config.sign_font)) {
    sign_draw_cleanup();
    if (text_init(config.sign_font) != 0)
      bongocat_log_warning("Sign text unavailable; boards will omit labels");
  }
  wayland_update_config(&config);
  if (input_changed) {
    result = input_restart_monitoring(config.keyboard_devices,
                                      config.num_keyboard_devices,
                                      config.keyboard_names, config.num_names,
                                      config.hotplug_scan_interval, 0);
    if (result != BONGOCAT_SUCCESS) {
      bongocat_log_warning("Input helper restart failed; retrying");
    }
  }
  config_cleanup_full(&old);
  bongocat_error_init(config.enable_debug);
  return 0;
}
static void changed(const char *path) {
  (void)path;
  reload_pending = true;
}
static void agent_refresh(void) {
  transcript_watch_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_state_t state = agent_sessions_resolve();
  if (state != animation_get_agent_state()) {
    animation_set_agent_state(state);
  }
  animation_use_agent_frames(config.sign_style == SIGN_STYLE_OFF);
}

static bool has_pid(const pid_t *pids, int count, pid_t pid) {
  for (int i = 0; i < count; i++) {
    if (pids[i] == pid) {
      return true;
    }
  }
  return false;
}

static void discover_expanded(void) {
  // focus_watch keeps at most 128 windows.
  focus_window_t windows[128];
  size_t count = focus_watch_windows(windows, 128);
  int created = agent_discover_scan(
      "/proc", windows, count, AGENT_DISCOVER_PROCESS_MAX,
      AGENT_DISCOVER_CREATE_MAX, monotonic_ms(), config.agent_done_timeout);
  if (created <= 0)
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int sessions = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < sessions; i++) {
    pid_t pid = views[i].pid;
    if (pid <= 0)
      continue;
    if (agent_watch_add(pid) == 0) {
      agent_sessions_set_watched(views[i].key, true);
      continue;
    }
    if (errno == ESRCH)
      agent_sessions_remove_pid(pid);
  }
}

static bool kitty_for_session(pid_t pid, uint64_t *window, char *listen,
                              size_t capacity) {
  return agent_sessions_kitty(pid, window, listen, capacity);
}

static void capture_kitty(uint64_t key, pid_t pid) {
  uint64_t window = 0;
  char listen[AGENT_TERMINAL_LISTEN_MAX + 1];
  pid_t kitty_pid = 0;
  if (agent_terminal_lookup("/proc", pid, &window, listen, sizeof(listen),
                            &kitty_pid))
    agent_sessions_set_kitty(key, kitty_pid, window, listen);
}

static int agent_apply(uint64_t key, const char *agent, agent_event_t event,
                       pid_t pid) {
  pid_t before[AGENT_SESSIONS_MAX], after[AGENT_SESSIONS_MAX];
  int before_count = agent_sessions_pids(before, AGENT_SESSIONS_MAX);
  pid_t previous = agent_sessions_pid(key);
  if (agent_sessions_apply(key, agent, event, pid, monotonic_ms(),
                           config.agent_done_timeout, NULL) < 0) {
    return 1;
  }
  if (event == AGENT_EVENT_WORKING)
    overlay_signs_note_working(key);
  int after_count = agent_sessions_pids(after, AGENT_SESSIONS_MAX);
  for (int i = 0; i < before_count; i++) {
    if (!has_pid(after, after_count, before[i])) {
      agent_watch_remove(before[i]);
    }
  }
  pid_t current = agent_sessions_pid(key);
  if (current > 0 && current != previous)
    capture_kitty(key, current);
  if (current > 0)
    agent_sessions_set_watched(key, agent_watch_add(current) == 0);
  agent_refresh();
  return 0;
}

static int agent_command(const char *request) {
  char agent[AGENT_NAME_MAX + 1], event_name[10], key_text[17], pid_text[8];
  int agent_end = 0, event_end = 0, key_end = 0, end = 0;
  if (sscanf(request, "ev %8[a-z]%n %9[a-z]%n %16[0-9a-fA-F]%n %7[0-9]%n",
             agent, &agent_end, event_name, &event_end, key_text, &key_end,
             pid_text, &end) != 4 ||
      request[agent_end] != ' ' || request[event_end] != ' ' ||
      request[key_end] != ' ' || request[end] != '\0' ||
      strlen(key_text) != 16) {
    return 1;
  }
  agent_event_t event;
  if (agent_event_parse(event_name, &event) < 0) {
    return 1;
  }
  uint64_t key = strtoull(key_text, NULL, 16);
  unsigned long pid = strtoul(pid_text, NULL, 10);
  if (!key || pid > 4194304UL) {
    return 1;
  }
  return agent_apply(key, agent, event, (pid_t)pid);
}

static int focus_command(const char *key) {
  size_t length = strlen(key);
  if ((length != 8 && length != 16) ||
      strspn(key, "0123456789abcdefABCDEF") != length)
    return 1;
  uint64_t requested = strtoull(key, NULL, 16);
  agent_session_view_t sessions[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(sessions, AGENT_SESSIONS_MAX);
  pid_t pid = 0;
  bool found = false;
  for (int i = 0; i < count; i++) {
    uint64_t candidate = length == 8 ? sessions[i].key >> 32 : sessions[i].key;
    if (candidate != requested)
      continue;
    if (found)
      return 1;
    found = true;
    pid = sessions[i].pid;
  }
  return !found || focus_session_window(pid) < 0;
}

static void note_window_focus(void);
static int command(const char *request, char *response, size_t capacity) {
  int result = 0;
  if (strcmp(request, "stop") == 0) {
    {
      running = 0;
    }
  } else if (strcmp(request, "hide") == 0) {
    hidden = true;
    wayland_set_hidden(true);
  } else if (strcmp(request, "show") == 0) {
    hidden = false;
    wayland_set_hidden(false);
  } else if (strcmp(request, "pause") == 0 || strcmp(request, "resume") == 0) {
    paused = strcmp(request, "pause") == 0;
    input_process_events();
    if (pending_paws) {
      atomic_store(pending_paws, 0);
    }
    animation_set_paused(paused);
    wayland_request_redraw();
  } else if (strcmp(request, "reset-position") == 0) {
    result = wayland_reset_position();
  } else if (strcmp(request, "reload") == 0) {
    { result = reload(); }
  } else if (strncmp(request, "state ", 6) == 0) {
    agent_state_t state;
    if (agent_state_parse(request + 6, &state) == 0) {
      agent_event_t event;
      agent_event_parse(request + 6, &event);
      result = agent_apply(
          0, "manual", state == AGENT_STATE_IDLE ? AGENT_EVENT_END : event, 0);
    } else {
      result = 1;
    }
  } else if (strncmp(request, "ev ", 3) == 0) {
    result = agent_command(request);
  } else if (strncmp(request, "path ", 5) == 0) {
    result = transcript_watch_command(request, monotonic_ms());
  } else if (strncmp(request, "name ", 5) == 0) {
    char key[17];
    int end = 0;
    if (strlen(request) > 63 ||
        sscanf(request + 5, "%16[0-9a-fA-F]%n", key, &end) != 1 || end != 16 ||
        request[21] != ' ' || !strtoull(key, NULL, 16))
      result = 1;
    else
      result =
          agent_sessions_set_name(strtoull(key, NULL, 16), request + 22) < 0;
  } else if (strncmp(request, "focus ", 6) == 0) {
    result = focus_command(request + 6);
  } else if (strncmp(request, "pane ", 5) == 0) {
    pid_t pane_pid = 0;
    uint64_t pane_split = 0;
    if (!focus_pane_parse(request, &pane_pid, &pane_split) ||
        !focus_pane_set(pane_pid, pane_split))
      result = 1;
    else
      note_window_focus();
  } else if (strcmp(request, "sessions") == 0) {
    if (agent_sessions_format(response, capacity, monotonic_ms()) == 0) {
      snprintf(response, capacity, "No agent sessions");
    }
    return 0;
  } else if (strcmp(request, "status") == 0) {
    snprintf(
        response, capacity,
        "running pid=%ld hidden=%s paused=%s input=%s devices=%u config=%s "
        "agent=%s sessions=%d",
        (long)getpid(), (int)hidden ? "yes" : "no", (int)paused ? "yes" : "no",
        (int)input_child_is_alive() ? "connected" : "restarting",
        input_device_count(), config_path,
        agent_state_name(animation_get_agent_state()), agent_sessions_count());
    return 0;
  } else {
    { result = 1; }
  }
  snprintf(response, capacity, "%s", result ? "request failed" : "ok");
  return result;
}
static void note_window_focus(void) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  uint64_t keys[AGENT_SESSIONS_MAX];
  int focused =
      focus_watch_focused_keys(views, (size_t)count, keys, AGENT_SESSIONS_MAX);
  bool watching = focus_watch_available();
  agent_sessions_observe_focus(watching, keys, (size_t)focused);
  uint64_t newest = 0;
  if (watching)
    newest = focus_watch_focused_session(views, (size_t)count);
  overlay_signs_sync_focus(newest);
  int64_t now = monotonic_ms();
  for (int i = 0; i < focused; i++) {
    agent_sessions_note_focused(keys[i], now, config.agent_done_timeout);
  }
}
static void extra_ready(uint32_t token) {
  focus_watch_ready(token);
  transcript_watch_ready(token, monotonic_ms());
}
static void tick(void) {
  hypr_poll();
  focus_poll();
  overlay_signs_note_focus(focus_take_result(), monotonic_ms());
  config_watcher_process(&watcher);
  if (reload_pending) {
    reload_pending = false;
    reload();
  }
  agent_watch_process(agent_sessions_remove_pid);
  focus_watch_poll();
  note_window_focus();
  agent_sessions_expire(monotonic_ms(), config.agent_stale_timeout);
  agent_refresh();
  control_process(command);
  session_store_flush(agent_sessions_generation(), monotonic_ms(), false);
  if (!input_child_is_alive() && monotonic_ms() >= input_retry_at) {
    input_retry_at = monotonic_ms() + 5000;
    bongocat_error_t result = input_restart_monitoring(
        config.keyboard_devices, config.num_keyboard_devices,
        config.keyboard_names, config.num_names, config.hotplug_scan_interval,
        0);
    if (result != BONGOCAT_SUCCESS) {
      bongocat_log_warning("Input helper unavailable; retrying in 5s");
    }
  }
}
static int runtime_timeout(void) {
  int candidates[] = {config_watcher_timeout(&watcher),
                      control_timeout(),
                      hypr_timeout(),
                      -1,
                      -1,
                      focus_timeout(),
                      focus_watch_timeout(),
                      session_store_timeout(monotonic_ms())};
  if (!input_child_is_alive()) {
    int64_t remaining = input_retry_at - monotonic_ms();
    candidates[3] = remaining > 0 ? (int)remaining : 0;
  }
  int64_t until = agent_sessions_next_deadline(config.agent_stale_timeout);
  if (until) {
    int64_t remaining = until - monotonic_ms();
    candidates[4] = remaining <= 0        ? 0
                    : remaining > INT_MAX ? INT_MAX
                                          : (int)remaining;
  }
  int timeout = -1;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (candidates[i] >= 0 && (timeout < 0 || candidates[i] < timeout)) {
      timeout = candidates[i];
    }
  }
  return timeout;
}
static int runtime_fds(int *fds, size_t capacity) {
  size_t count = 0;
  // Six basics plus the control socket fill the seven external poll slots.
  // The niri event stream is registered on agent_watch's epoll, not here.
  int basic[] = {signal_fd,      input_get_wake_fd(), watcher.inotify_fd,
                 hypr_poll_fd(), agent_watch_fd(),    focus_poll_fd()};
  for (size_t i = 0; i < sizeof(basic) / sizeof(basic[0]) && count < capacity;
       i++) {
    if (basic[i] >= 0) {
      fds[count++] = basic[i];
    }
  }
  return (int)count + control_fds(fds + count, capacity - count);
}
static void help(const char *program) {
  printf(
      "Usage: %s [options]\n"
      "  -c, --config FILE    Configuration path (XDG search by default)\n"
      "  -w, --watch-config   Reload 300 ms after config changes settle\n"
      "  -m, --monitor NAME   Override configured output selection\n"
      "  -t, --toggle         Start or stop the running application\n"
      "  --hide, --show       Control visibility of every overlay\n"
      "  --pause, --resume    Display idle frame or resume input animation\n"
      "  --focus KEY          Focus a session terminal (full key or unique "
      "prefix)\n"
      "  --pane PID ID        Report the focused kitty split\n"
      "  --state NAME         Set manual state: idle, working, waiting, done\n"
      "  --sessions           List tracked agent sessions\n"
      "  --reset-position     Restore configured positions on every output\n"
      "  --event NAME         Override stdin event (after --hook AGENT)\n"
      "  --hook AGENT         Read one agent lifecycle event from stdin\n"
      "  --reload, --status   Reload config or query running application\n"
      "  --check-config       Strict validation without Wayland or input "
      "access\n"
      "  --list-devices       List evdev devices and keyboard capabilities\n"
      "  --list-monitors      List Wayland outputs, dimensions and scales\n"
      "  --doctor             Check config, protocols, devices and "
      "permissions\n"
      "  -h, --help           Show help\n"
      "  -v, --version        Show version\n",
      program);
}
static void menu_style(sign_style_t style) {
  if (prefs_choose_style(style))
    bongocat_log_warning("Menu style was not saved");
  config.sign_style = style;
  wayland_update_config(&config);
}
static void menu_language(sign_language_t language) {
  if (prefs_choose_language(language))
    bongocat_log_warning("Menu language was not saved");
  config.sign_language = language;
  wayland_update_config(&config);
}
static void menu_paw(unsigned paw) {
  animation_tap(paw, 220);
}
static void menu_font(const char *family, bool save) {
  snprintf(config.sign_font, sizeof(config.sign_font), "%s",
           family ? family : "");
  if (text_set_family(config.sign_font))
    bongocat_log_warning("Sign font was not changed");
  if (save && prefs_choose_font(config.sign_font))
    bongocat_log_warning("Menu font was not saved");
  wayland_update_config(&config);
}
static int run_application(bool watch, bongocat_error_t result) {
  int exit_code = 1;
  if (result != BONGOCAT_SUCCESS ||
      force_monitor(&config) != BONGOCAT_SUCCESS) {
    goto cleanup;
  }
  if (prefs_resolve(&config.sign_style, &config.sign_language, config.sign_font,
                    sizeof(config.sign_font)))
    bongocat_log_warning("Menu preferences were ignored");
  if (instance_lock() < 0) {
    bongocat_log_error("Cannot lock instance: %s", strerror(errno));
    goto cleanup;
  }
  if (control_start() < 0 || setup_signals() < 0) {
    goto cleanup;
  }
  if (agent_watch_init() < 0) {
    bongocat_log_warning("Agent process watches unavailable; using timeouts");
  }
  sign_policy();
  focus_set_kitty(kitty_for_session);
  session_store_load(monotonic_ms(), config.agent_done_timeout);
  focus_watch_init();
  overlay_signs_on_expand(discover_expanded);
  overlay_signs_on_menu(menu_style, menu_language, menu_paw, menu_font);
  agent_watch_on_ready(extra_ready);
  if (watch && config_watcher_init(&watcher, config_path, changed) == 0) {
    config_watcher_start(&watcher);
  }
  result = animation_init(&config);
  if (result != BONGOCAT_SUCCESS) {
    goto cleanup;
  }
  animation_set_key_hook(overlay_signs_note_key);
  result = wayland_init(&config);
  if (result != BONGOCAT_SUCCESS) {
    goto cleanup;
  }
  result = input_start_monitoring(
      config.keyboard_devices, config.num_keyboard_devices,
      config.keyboard_names, config.num_names, config.hotplug_scan_interval, 0);
  if (result != BONGOCAT_SUCCESS) {
    goto cleanup;
  }
  wayland_set_tick_callback(tick);
  wayland_set_runtime_fds(runtime_fds);
  wayland_set_runtime_timeout(runtime_timeout);
  result = wayland_run(&running);
  if (result != BONGOCAT_SUCCESS) {
    bongocat_log_error("Runtime stopped: %s", bongocat_error_string(result));
  }
  exit_code = result == BONGOCAT_SUCCESS ? 0 : 1;
cleanup:
  session_store_flush(agent_sessions_generation(), monotonic_ms(), true);
  config_watcher_cleanup(&watcher);
  hypr_cleanup();
  focus_cleanup();
  input_cleanup();
  wayland_cleanup();
  animation_cleanup();
  focus_watch_cleanup();
  transcript_watch_cleanup();
  agent_watch_cleanup();
  agent_sessions_reset();
  control_cleanup();
  if (signal_fd >= 0) {
    int fd = signal_fd;
    signal_fd = -1;
    close(fd);
  }
  config_cleanup_full(&config);
  free(config_path);
  instance_unlock();
  return exit_code;
}

int main(int argc, char **argv) {
  input_privilege_init();
  if (argc > 1 && strcmp(argv[1], "--input-helper") == 0) {
    return input_helper_main(argc, argv);
  }
  input_privilege_drop();
  bongocat_error_init(0);
  const char *explicit_path = NULL;
  const char *request = NULL;
  const char *hook_agent = NULL;
  const char *hook_event = NULL;
  static char state_request[32];
  static char pane_request[64];
  bool watch = false;
  bool toggle = false;
  bool check = false;
  bool devices = false;
  bool monitors = false;
  bool doctor = false;
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
      help(argv[0]);
      return 0;
    }
    if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
      puts(BONGOCAT_VERSION);
      return 0;
    }
    if (!strcmp(arg, "--config") || !strcmp(arg, "-c") ||
        !strcmp(arg, "--monitor") || !strcmp(arg, "-m")) {
      if (++i >= argc) {
        fprintf(stderr, "%s requires a value\n", arg);
        return 1;
      }
      if (!strcmp(arg, "--config") || !strcmp(arg, "-c")) {
        explicit_path = argv[i];
      } else {
        monitor_override = argv[i];
      }
    } else if (!strcmp(arg, "--hook")) {
      if (request || hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      if (++i >= argc || !agent_hook_valid_agent(argv[i])) {
        fprintf(stderr, "--hook requires an agent name matching [a-z]{1,8}\n");
        return 1;
      }
      hook_agent = argv[i];
    } else if (!strcmp(arg, "--event")) {
      if (!hook_agent || hook_event || ++i >= argc || !argv[i][0] ||
          strlen(argv[i]) >= 64) {
        fprintf(stderr, "--event requires a name after --hook AGENT\n");
        return 1;
      }
      hook_event = argv[i];
    } else if (!strcmp(arg, "--pane")) {
      if (request || hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      if (i + 2 >= argc) {
        fprintf(stderr, "--pane requires a process and a split\n");
        return 1;
      }
      pid_t pane_pid = 0;
      uint64_t pane_split = 0;
      if (!focus_pane_fields(argv[i + 1], argv[i + 2], &pane_pid,
                             &pane_split)) {
        fprintf(stderr, "--pane requires a process and a split\n");
        return 1;
      }
      i += 2;
      snprintf(pane_request, sizeof(pane_request), "pane %ld %llu",
               (long)pane_pid, (unsigned long long)pane_split);
      request = pane_request;
    } else if (!strcmp(arg, "--state") || !strcmp(arg, "--focus")) {
      if (request || hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      if (++i >= argc) {
        fprintf(stderr, "%s requires a value\n", arg);
        return 1;
      }
      if (strlen(argv[i]) > 24) {
        fprintf(stderr, "Unknown agent state\n");
        return 1;
      }
      snprintf(state_request, sizeof(state_request), "%s %s", arg + 2, argv[i]);
      request = state_request;
    } else if (!strcmp(arg, "--watch-config") || !strcmp(arg, "-w")) {
      watch = true;
    } else if (!strcmp(arg, "--toggle") || !strcmp(arg, "-t")) {
      toggle = true;
    } else if (!strcmp(arg, "--check-config")) {
      check = true;
    } else if (!strcmp(arg, "--list-devices")) {
      devices = true;
    } else if (!strcmp(arg, "--list-monitors")) {
      monitors = true;
    } else if (!strcmp(arg, "--doctor")) {
      doctor = true;
    } else if (!strcmp(arg, "--hide") || !strcmp(arg, "--show") ||
               !strcmp(arg, "--pause") || !strcmp(arg, "--resume") ||
               !strcmp(arg, "--reload") || !strcmp(arg, "--status") ||
               !strcmp(arg, "--sessions") || !strcmp(arg, "--reset-position")) {
      if (request || hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      request = arg + 2;
    } else {
      fprintf(stderr, "Unknown option: %s\n", arg);
      return 1;
    }
  }
  if (hook_agent) {
    return agent_hook_run(hook_agent, hook_event);
  }
  if (request) {
    return control_request(request) == 0 ? 0 : 1;
  }
  if (toggle) {
    int result = control_request("stop");
    if (result != 2) {
      return result;
    }
  }
  if (devices && !doctor) {
    return input_list_devices();
  }
  if (monitors && !doctor) {
    return wayland_list_monitors(false);
  }
  config_path = config_resolve_path(explicit_path);
  if (!config_path) {
    config_path = strdup("bongocat.conf");
  }
  if (!config_path) {
    return 1;
  }
  bongocat_error_t result = (check || doctor)
                                ? load_config_strict(&config, config_path)
                                : load_config(&config, config_path);
  if (check || doctor) {
    printf("Config: %s (%s)\n", config_path,
           result == BONGOCAT_SUCCESS ? "valid" : "invalid");
    int failure = result != BONGOCAT_SUCCESS;
    if (doctor) {
      printf("focus=%s\n", focus_available() ? "niri" : "none");
      failure |= input_list_devices();
      failure |= wayland_list_monitors(true);
    }
    config_cleanup_full(&config);
    free(config_path);
    return failure;
  }
  return run_application(watch, result);
}

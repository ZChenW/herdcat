#define _GNU_SOURCE
#include "config/config.h"
#include "config/sign_options.h"
#include "core/agent_hook.h"
#include "core/agent_quiet.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "core/agent_title.h"
#include "core/control.h"
#include "core/herdcat.h"
#include "graphics/animation.h"
#include "graphics/sign_draw.h"
#include "graphics/text.h"
#include "platform/agent_terminal.h"
#include "platform/agent_watch.h"
#include "platform/compositor.h"
#include "platform/focus.h"
#include "platform/focus_current.h"
#include "platform/focus_watch.h"
#include "platform/hyprland.h"
#include "platform/input.h"
#include "platform/overlay_signs.h"
#include "platform/prefs.h"
#include "platform/session_store.h"
#include "platform/theme_watch.h"
#include "platform/transcript_watch.h"
#include "platform/wayland.h"
#include "runtime_internal.h"
#include "utils/error.h"

#include <errno.h>
#include <fcntl.h>
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

volatile sig_atomic_t running = 1;
static int signal_fd = -1;
config_t config;
static config_watcher_t watcher = {.inotify_fd = -1, .watch_fd = -1};
char *config_path;
static const char *monitor_override;
static bool reload_pending;
static int64_t input_retry_at;
int64_t monotonic_ms(void) {
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
static herdcat_error_t force_monitor(config_t *settings) {
  if (!monitor_override) {
    return HERDCAT_SUCCESS;
  }
  char *name = strdup(monitor_override);
  if (!name) {
    return HERDCAT_ERROR_MEMORY;
  }
  free(settings->output_name);
  settings->output_name = name;
  for (int i = 0; i < settings->num_output_names; i++) {
    free(settings->output_names[i]);
  }
  free((void *)settings->output_names);
  settings->output_names = NULL;
  settings->num_output_names = 0;
  return HERDCAT_SUCCESS;
}
static void sign_policy(void) {
  transcript_watch_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_quiet_sync(config.agent_interrupt_detect, monotonic_ms());
  agent_sessions_configure_done(config.sign_done == SIGN_DONE_STICKY,
                                monotonic_ms(), config.agent_done_timeout);
}
int reload(void) {
  config_t next = {0};
  herdcat_error_t result = load_config_strict(&next, config_path);
  if (result == HERDCAT_SUCCESS) {
    result = force_monitor(&next);
  }
  if (result != HERDCAT_SUCCESS) {
    config_cleanup_full(&next);
    herdcat_error_init(config.enable_debug);
    herdcat_log_warning("Reload rejected; keeping current configuration");
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
                    sizeof(config.sign_font), &config.sign_theme))
    herdcat_log_warning("Menu preferences were not updated");
  if (old.compositor_experimental != config.compositor_experimental) {
    compositor_configure(config.compositor_experimental != 0);
    focus_watch_init();
  }
  overlay_signs_use_config();
  sign_policy();
  theme_watch_configure(config.sign_theme);
  if (strcmp(old.sign_font, config.sign_font)) {
    sign_draw_cleanup();
    if (text_init(config.sign_font) != 0)
      herdcat_log_warning("Sign text unavailable; boards will omit labels");
  }
  wayland_update_config(&config);
  if (input_changed) {
    result = input_restart_monitoring(config.keyboard_devices,
                                      config.num_keyboard_devices,
                                      config.keyboard_names, config.num_names,
                                      config.hotplug_scan_interval, 0);
    if (result != HERDCAT_SUCCESS) {
      herdcat_log_warning("Input helper restart failed; retrying");
    }
  }
  config_cleanup_full(&old);
  herdcat_error_init(config.enable_debug);
  return 0;
}
static void changed(const char *path) {
  (void)path;
  reload_pending = true;
}
static void extra_ready(uint32_t token) {
  focus_watch_ready(token);
  theme_watch_ready(token);
  transcript_watch_ready(token, monotonic_ms());
}
static void tick(void) {
  input_refresh_selection(config.keyboard_devices, config.num_keyboard_devices,
                          config.keyboard_names, config.num_names,
                          config.hotplug_scan_interval);
  hypr_poll();
  transcript_prompt_poll();
  focus_poll();
  overlay_signs_note_focus(focus_take_result(), monotonic_ms());
  config_watcher_process(&watcher);
  if (reload_pending) {
    reload_pending = false;
    reload();
  }
  agent_watch_process(agent_sessions_remove_pid);
  theme_watch_poll();
  focus_watch_poll();
  note_window_focus();
  agent_sessions_expire(monotonic_ms(), config.agent_stale_timeout);
  agent_refresh();
  control_process(command);
  sign_draw_cache_update(agent_sessions_resolve() == AGENT_STATE_WAITING,
                         monotonic_ms());
  session_store_flush(agent_sessions_generation(), monotonic_ms(), false);
  if (!input_child_is_alive() && monotonic_ms() >= input_retry_at) {
    input_retry_at = monotonic_ms() + 5000;
    herdcat_error_t result = input_restart_monitoring(
        config.keyboard_devices, config.num_keyboard_devices,
        config.keyboard_names, config.num_names, config.hotplug_scan_interval,
        0);
    if (result != HERDCAT_SUCCESS) {
      herdcat_log_warning("Input helper unavailable; retrying in 5s");
    }
  }
}
static int runtime_timeout(void) {
  int quiet_wait = -1;
  if (agent_quiet_deadline()) {
    int64_t left = agent_quiet_deadline() - monotonic_ms();
    quiet_wait = left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
  }
  int candidates[] = {config_watcher_timeout(&watcher),
                      control_timeout(),
                      hypr_timeout(),
                      -1,
                      -1,
                      focus_timeout(),
                      focus_watch_timeout(),
                      theme_watch_timeout(),
                      transcript_prompt_timeout(monotonic_ms()),
                      quiet_wait,
                      sign_draw_cache_timeout(monotonic_ms()),
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
      "  setup [AGENT ...]    Connect agents (herdcat-setup; Python 3)\n"
      "  -c, --config FILE    Configuration path (XDG search by default)\n"
      "  -w, --watch-config   Reload 300 ms after config changes settle\n"
      "  -m, --monitor NAME   Override configured output selection\n"
      "  -t, --toggle         Start or stop the running application\n"
      "  --hide, --show       Control visibility of every overlay\n"
      "  --pause, --resume    Display idle frame or resume input animation\n"
      "  --focus KEY          Focus a session terminal (full key or unique "
      "prefix)\n"
      "  --pane PID ID        Report the focused kitty/tmux split\n"
      "  --tmux               Refresh sessions on the TMUX server\n"
      "  --state NAME         Set manual state: idle, working, waiting, done,\n"
      "                       error\n"
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
    herdcat_log_warning("Menu style was not saved");
  config.sign_style = style;
  wayland_update_config(&config);
}
static void menu_language(sign_language_t language) {
  if (prefs_choose_language(language))
    herdcat_log_warning("Menu language was not saved");
  config.sign_language = language;
  wayland_update_config(&config);
}
static void menu_theme(sign_theme_t theme) {
  if (prefs_choose_theme(theme))
    herdcat_log_warning("Menu theme was not saved");
  config.sign_theme = theme;
  theme_watch_configure(theme);
  wayland_update_config(&config);
}
static void menu_paw(unsigned paw) {
  animation_tap(paw, 220);
}
static void menu_font(const char *family, bool save) {
  snprintf(config.sign_font, sizeof(config.sign_font), "%s",
           family ? family : "");
  if (text_set_family(config.sign_font))
    herdcat_log_warning("Sign font was not changed");
  if (save && prefs_choose_font(config.sign_font))
    herdcat_log_warning("Menu font was not saved");
  wayland_update_config(&config);
}
static int run_application(bool watch, herdcat_error_t result) {
  int exit_code = 1;
  if (result != HERDCAT_SUCCESS || force_monitor(&config) != HERDCAT_SUCCESS) {
    goto cleanup;
  }
  if (prefs_resolve(&config.sign_style, &config.sign_language, config.sign_font,
                    sizeof(config.sign_font), &config.sign_theme))
    herdcat_log_warning("Menu preferences were ignored");
  if (instance_lock() < 0) {
    herdcat_log_error("Cannot lock instance: %s", strerror(errno));
    goto cleanup;
  }
  if (control_start() < 0 || setup_signals() < 0) {
    goto cleanup;
  }
  if (agent_watch_init() < 0) {
    herdcat_log_warning("Agent process watches unavailable; using timeouts");
  }
  sign_policy();
  theme_watch_configure(config.sign_theme);
  compositor_configure(config.compositor_experimental != 0);
  focus_set_kitty(kitty_for_session);
  focus_set_terminal(agent_sessions_terminal, terminal_resolved);
  focus_set_title(agent_sessions_title);
  focus_set_current(terminal_current);
  session_store_load(monotonic_ms(), config.agent_done_timeout);
  resolve_restored_terminals();
  focus_watch_init();
  overlay_signs_on_expand(discover_expanded);
  overlay_signs_on_menu(menu_style, menu_language, menu_paw, menu_font,
                        menu_theme);
  agent_watch_on_ready(extra_ready);
  if (watch && config_watcher_init(&watcher, config_path, changed) == 0) {
    config_watcher_start(&watcher);
  }
  result = animation_init(&config);
  if (result != HERDCAT_SUCCESS) {
    goto cleanup;
  }
  animation_set_key_hook(note_key);
  result = wayland_init(&config);
  if (result != HERDCAT_SUCCESS) {
    goto cleanup;
  }
  result = input_start_monitoring(
      config.keyboard_devices, config.num_keyboard_devices,
      config.keyboard_names, config.num_names, config.hotplug_scan_interval, 0);
  if (result != HERDCAT_SUCCESS) {
    goto cleanup;
  }
  wayland_set_tick_callback(tick);
  wayland_set_runtime_fds(runtime_fds);
  wayland_set_runtime_timeout(runtime_timeout);
  result = wayland_run(&running);
  if (result != HERDCAT_SUCCESS) {
    herdcat_log_error("Runtime stopped: %s", herdcat_error_string(result));
  }
  exit_code = result == HERDCAT_SUCCESS ? 0 : 1;
cleanup:
  session_store_flush(agent_sessions_generation(), monotonic_ms(), true);
  config_watcher_cleanup(&watcher);
  hypr_cleanup();
  focus_cleanup();
  input_cleanup();
  wayland_cleanup();
  animation_cleanup();
  theme_watch_cleanup();
  focus_watch_cleanup();
  transcript_watch_cleanup();
  agent_watch_cleanup();
  agent_sessions_reset();
  agent_quiet_reset();
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

typedef struct {
  const char *explicit_path;
  const char *request;
  const char *hook_agent;
  const char *hook_event;
  char state_request[32];
  char pane_request[384];
  bool watch;
  bool toggle;
  bool check;
  bool devices;
  bool monitors;
  bool doctor;
} cli_options_t;

static int run_setup(char **argv) {
  argv[1] = "herdcat-setup";
  // The executable is fixed; forwarding the user's setup arguments is intended.
  // NOLINTNEXTLINE(clang-analyzer-optin.taint.GenericTaint)
  execvp(argv[1], &argv[1]);
  fprintf(stderr,
          "Cannot run herdcat-setup: %s. Install the setup script and "
          "Python 3, and include its bin directory in PATH.\n",
          strerror(errno));
  return 1;
}

static int parse_pane_argument(int argc, char **argv, cli_options_t *options,
                               int *index) {
  int i = *index;
  if (options->request || options->hook_agent) {
    fprintf(stderr, "Select one control command\n");
    return 1;
  }
  if (i + 2 >= argc) {
    fprintf(stderr, "--pane requires a process and a split\n");
    return 1;
  }
  pid_t pane_pid = 0;
  uint64_t pane_split = 0;
  agent_terminal_t terminal;
  bool tmux =
      agent_terminal_environment(&terminal) && terminal.kind == TERMINAL_TMUX;
  if (!(tmux ? focus_tmux_pane_fields(argv[i + 1], argv[i + 2], &pane_pid,
                                      &pane_split)
             : focus_pane_fields(argv[i + 1], argv[i + 2], &pane_pid,
                                 &pane_split))) {
    fprintf(stderr, "--pane requires a process and a split\n");
    return 1;
  }
  i += 2;
  if (tmux) {
    if (!agent_terminal_pane_message(options->pane_request,
                                     sizeof(options->pane_request), pane_pid,
                                     pane_split, &terminal))
      return 1;
  } else {
    snprintf(options->pane_request, sizeof(options->pane_request),
             "pane %ld %llu", (long)pane_pid, (unsigned long long)pane_split);
  }
  options->request = options->pane_request;
  *index = i;
  return 0;
}

static int parse_hook_argument(int argc, char **argv, cli_options_t *options,
                               int *index) {
  if (options->request || options->hook_agent) {
    fprintf(stderr, "Select one control command\n");
    return 1;
  }
  int i = ++*index;
  if (i >= argc || !agent_hook_valid_agent(argv[i])) {
    fprintf(stderr, "--hook requires an agent name matching [a-z]{1,8}\n");
    return 1;
  }
  options->hook_agent = argv[i];
  return 0;
}

static int parse_hook_event(int argc, char **argv, cli_options_t *options,
                            int *index) {
  int i = *index;
  bool invalid = !options->hook_agent || options->hook_event;
  if (!invalid)
    i = ++*index;
  if (invalid || i >= argc || !argv[i][0] || strlen(argv[i]) >= 64) {
    fprintf(stderr, "--event requires a name after --hook AGENT\n");
    return 1;
  }
  options->hook_event = argv[i];
  return 0;
}

static int parse_arguments(int argc, char **argv, cli_options_t *options) {
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
      help(argv[0]);
      return 0;
    }
    if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
      puts(HERDCAT_VERSION);
      return 0;
    }
    if (!strcmp(arg, "--config") || !strcmp(arg, "-c") ||
        !strcmp(arg, "--monitor") || !strcmp(arg, "-m")) {
      i++;
      if (i >= argc) {
        fprintf(stderr, "%s requires a value\n", arg);
        return 1;
      }
      if (!strcmp(arg, "--config") || !strcmp(arg, "-c")) {
        options->explicit_path = argv[i];
      } else {
        monitor_override = argv[i];
      }
    } else if (!strcmp(arg, "--hook")) {
      if (parse_hook_argument(argc, argv, options, &i))
        return 1;
    } else if (!strcmp(arg, "--event")) {
      if (parse_hook_event(argc, argv, options, &i))
        return 1;
    } else if (!strcmp(arg, "--tmux")) {
      if (options->request || options->hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      if (!agent_terminal_tmux_message(options->pane_request,
                                       sizeof(options->pane_request))) {
        fprintf(stderr, "--tmux requires a valid TMUX server environment\n");
        return 1;
      }
      options->request = options->pane_request;
    } else if (!strcmp(arg, "--pane")) {
      if (parse_pane_argument(argc, argv, options, &i))
        return 1;
    } else if (!strcmp(arg, "--state") || !strcmp(arg, "--focus")) {
      if (options->request || options->hook_agent) {
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
      snprintf(options->state_request, sizeof(options->state_request), "%s %s",
               arg + 2, argv[i]);
      options->request = options->state_request;
    } else if (!strcmp(arg, "--watch-config") || !strcmp(arg, "-w")) {
      options->watch = true;
    } else if (!strcmp(arg, "--toggle") || !strcmp(arg, "-t")) {
      options->toggle = true;
    } else if (!strcmp(arg, "--check-config")) {
      options->check = true;
    } else if (!strcmp(arg, "--list-devices")) {
      options->devices = true;
    } else if (!strcmp(arg, "--list-monitors")) {
      options->monitors = true;
    } else if (!strcmp(arg, "--doctor")) {
      options->doctor = true;
    } else if (!strcmp(arg, "--hide") || !strcmp(arg, "--show") ||
               !strcmp(arg, "--pause") || !strcmp(arg, "--resume") ||
               !strcmp(arg, "--reload") || !strcmp(arg, "--status") ||
               !strcmp(arg, "--sessions") || !strcmp(arg, "--reset-position")) {
      if (options->request || options->hook_agent) {
        fprintf(stderr, "Select one control command\n");
        return 1;
      }
      options->request = arg + 2;
    } else {
      fprintf(stderr, "Unknown option: %s\n", arg);
      return 1;
    }
  }
  return -1;
}

static bool dispatch_client(const cli_options_t *options, int *exit_code) {
  if (options->hook_agent) {
    *exit_code = agent_hook_run(options->hook_agent, options->hook_event);
  } else if (options->request) {
    int result = control_request(options->request);
    if (result != 0 && !strcmp(options->request, "status")) {
      printf("input-helper=%s: %s\n", input_mode_name(), input_mode_hint());
    }
    *exit_code = result == 0 ? 0 : 1;
  } else {
    if (options->toggle) {
      int result = control_request("stop");
      if (result != 2) {
        *exit_code = result;
        return true;
      }
    }
    if (options->devices && !options->doctor) {
      *exit_code = input_list_devices();
    } else if (options->monitors && !options->doctor) {
      *exit_code = wayland_list_monitors(false);
    } else {
      return false;
    }
  }
  return true;
}

static int run_configured_application(const cli_options_t *options) {
  config_path = config_resolve_path(options->explicit_path);
  if (!config_path) {
    config_path = strdup("herdcat.conf");
  }
  if (!config_path) {
    return 1;
  }
  herdcat_error_t result = (options->check || options->doctor)
                               ? load_config_strict(&config, config_path)
                               : load_config(&config, config_path);
  if (options->check || options->doctor) {
    printf("Config: %s (%s)\n", config_path,
           result == HERDCAT_SUCCESS ? "valid" : "invalid");
    int failure = result != HERDCAT_SUCCESS;
    if (options->doctor) {
      printf("Agent integrations: herdcat setup --status\n");
      const compositor_ops_t *ops =
          compositor_detect(config.compositor_experimental != 0);
      printf("focus=%s\n", ops && ops->detect() ? ops->name : "none");
      printf("sign_theme=auto: %s\n",
             theme_tool_available() ? "busctl (XDG portal)"
                                    : "busctl missing; falls back to light");
      failure |= input_list_devices();
      failure |= wayland_list_monitors(true);
    }
    config_cleanup_full(&config);
    free(config_path);
    return failure;
  }
  return run_application(options->watch, result);
}

int main(int argc, char **argv) {
  input_privilege_init();
  if (argc > 1 && strcmp(argv[1], "--input-helper") == 0) {
    return input_helper_main(argc, argv);
  }
  input_privilege_drop();
  if (argc > 1 && !strcmp(argv[1], "--transcript-prompt"))
    return agent_prompt_main(argc, argv);
  if (argc > 1 && strcmp(argv[1], "setup") == 0)
    return run_setup(argv);
  herdcat_error_init(0);
  cli_options_t options = {0};
  int exit_code = parse_arguments(argc, argv, &options);
  if (exit_code >= 0 || dispatch_client(&options, &exit_code))
    return exit_code;
  return run_configured_application(&options);
}

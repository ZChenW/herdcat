#define _POSIX_C_SOURCE 200809L
#include "config/config.h"

#include "config/sign_options.h"
#include "config_internal.h"
#include "core/herdcat.h"
#include "utils/error.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// =============================================================================
// CONFIGURATION CONSTANTS AND VALIDATION RANGES
// =============================================================================

// =============================================================================
// CONFIGURATION VALIDATION MODULE
// =============================================================================

bool strict_parse;
bool validation_failed;
static config_diagnostic_callback_t diagnostic_callback;
static void *diagnostic_data;
char diagnostic_path[PATH_MAX];
int diagnostic_line;
void diagnostic(const char *key, const char *message) {
  config_diagnostic_t item = {.file = diagnostic_path,
                              .line = diagnostic_line,
                              .key = key,
                              .severity = (int)strict_parse ? CONFIG_ERROR
                                                            : CONFIG_WARNING,
                              .message = message};
  if (diagnostic_callback) {
    diagnostic_callback(&item, diagnostic_data);
  }
  herdcat_log_warning("%s:%d: %s: key '%s': %s", item.file, item.line,
                      (int)strict_parse ? "error" : "warning", key ? key : "",
                      message);
}

// =============================================================================
// DEVICE MANAGEMENT MODULE
// =============================================================================

herdcat_error_t config_expand_array(char ***array_ptr, int *count,
                                    const char *str) {
  char **new_array =
      (char **)realloc((void *)*array_ptr, (*count + 1) * sizeof(char *));
  if (!new_array) {
    return HERDCAT_ERROR_MEMORY;
  }
  *array_ptr = new_array;

  size_t len = strlen(str);
  (*array_ptr)[*count] = malloc(len + 1);
  if (!(*array_ptr)[*count]) {
    return HERDCAT_ERROR_MEMORY;
  }

  memcpy((*array_ptr)[*count], str, len + 1);  // includes null terminator
  (*count)++;

  return HERDCAT_SUCCESS;
}

herdcat_error_t config_add_keyboard_device(config_t *config,
                                           const char *device_path) {
  herdcat_error_t err = config_expand_array(
      &config->keyboard_devices, &config->num_keyboard_devices, device_path);
  if (err != HERDCAT_SUCCESS) {
    herdcat_log_error("Failed to add keyboard device: %s",
                      herdcat_error_string(err));
    return err;
  }

  return HERDCAT_SUCCESS;
}

void config_free_string_array(char ***array_ptr, int *count) {
  if (*array_ptr) {
    for (int i = 0; i < *count; i++) {
      free((*array_ptr)[i]);
    }
    free((void *)*array_ptr);
    *array_ptr = NULL;
    *count = 0;
  }
}

static void config_cleanup_devices(config_t *config) {
  if (!config) {
    return;
  }

  config_free_string_array(&config->keyboard_devices,
                           &config->num_keyboard_devices);
  config_free_string_array(&config->keyboard_names, &config->num_names);
}

// =============================================================================
// CONFIGURATION PARSING MODULE
// =============================================================================

herdcat_error_t config_parse_monitor_setting(config_t *config,
                                             const char *monitor,
                                             const char *key,
                                             const char *value) {
  const char *allowed[] = {"cat_draggable",
                           "cat_height",
                           "overlay_height",
                           "overlay_opacity",
                           "cat_x_offset",
                           "cat_y_offset",
                           "layer",
                           "overlay_position",
                           "cat_align",
                           "mirror_x",
                           "mirror_y",
                           "enable_antialiasing",
                           "disable_fullscreen_hide"};
  bool appearance = false;
  for (size_t k = 0; k < sizeof(allowed) / sizeof(allowed[0]); k++) {
    appearance |= strcmp(key, allowed[k]) == 0;
  }
  config_t probe = *config;
  herdcat_error_t parse_result =
      (int)appearance ? config_parse_key_value(&probe, key, value)
                      : HERDCAT_ERROR_INVALID_PARAM;
  if (parse_result == HERDCAT_SUCCESS) {
    bool previous = validation_failed;
    validation_failed = false;
    herdcat_error_t valid = config_validate(&probe);
    bool invalid = validation_failed;
    validation_failed |= previous;
    if (valid != HERDCAT_SUCCESS || (strict_parse && invalid)) {
      parse_result = HERDCAT_ERROR_CONFIG;
    }
  }
  if (parse_result == HERDCAT_SUCCESS) {
    monitor_override_t entry = {
        .monitor = strdup(monitor), .key = strdup(key), .value = strdup(value)};
    monitor_override_t *entries = NULL;
    if (entry.monitor && entry.key && entry.value) {
      entries = realloc(config->overrides,
                        (config->num_overrides + 1) * sizeof(*entries));
    }
    if (!entries) {
      free(entry.monitor);
      free(entry.key);
      free(entry.value);
      parse_result = HERDCAT_ERROR_MEMORY;
    } else {
      config->overrides = entries;
      config->overrides[config->num_overrides++] = entry;
    }
  }
  return parse_result;
}

const char *config_startup_path(const char *config_file_path,
                                char resolved[PATH_MAX]) {
  const char *file_path = config_file_path ? config_file_path : "herdcat.conf";

  // If no explicit path, try XDG paths
  bool using_resolved = false;

  if (!config_file_path) {
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (xdg_config && xdg_config[0] != '\0') {
      snprintf(resolved, PATH_MAX, "%s/herdcat/herdcat.conf", xdg_config);
      if (access(resolved, R_OK) == 0) {
        file_path = resolved;
        using_resolved = true;
      }
    }

    if (!using_resolved && home && home[0] != '\0') {
      snprintf(resolved, PATH_MAX, "%s/.config/herdcat/herdcat.conf", home);
      if (access(resolved, R_OK) == 0) {
        file_path = resolved;
      }
    }
  }

  return file_path;
}

// =============================================================================
// DEFAULT CONFIGURATION MODULE
// =============================================================================

static void config_set_defaults(config_t *config) {
  *config = (config_t){
      .screen_width =
          DEFAULT_SCREEN_WIDTH, // Will be updated by Wayland detection
      .output_name = NULL, // Will default to automatic one if kept null
      .output_names = NULL,
      .num_output_names = 0,
      .asset_paths = {"assets/new/bongo-both-up.svg",
                      "assets/new/bongo-left-down.svg", "assets/new/bongo-right-down.svg",
                      "assets/new/bongo-both-down.svg", "assets/new/bongo-sleeping.svg",
                      "assets/new/bongo-agent-working.svg", "assets/new/bongo-agent-waiting.svg",
                      "assets/new/bongo-agent-done.svg"},
      .keyboard_devices = NULL,
      .num_keyboard_devices = 0,
      .hotplug_scan_interval = 30,
      .keyboard_names = NULL,
      .num_names = 0,
      .cat_x_offset = 100,
      .cat_y_offset = 10,
      .cat_height = 40,
      .cat_draggable = 1,
      .sign_style = SIGN_STYLE_FAN,
      .sign_max = 5,
      .sign_font_size = 13,
      .sign_idle = SIGN_IDLE_HOVER,
      .sign_animations = SIGN_ANIM_FULL,
      .sign_language = SIGN_LANGUAGE_AUTO,
      .sign_done = SIGN_DONE_STICKY,
      .sign_typing_desk = 1,
      .overlay_height = 50,
      .idle_frame = 0,
      .agent_done_timeout = DEFAULT_AGENT_DONE_TIMEOUT,
      .agent_interrupt_detect = 1,
      .agent_stale_timeout = DEFAULT_AGENT_STALE_TIMEOUT,
      .keypress_duration = 100,
      .test_animation_duration = 200,
      .test_animation_interval = 0,
      .fps = 60,
      .overlay_opacity = 150,
      .mirror_x = 0,
      .mirror_y = 0,
      .enable_antialiasing = 1,
      .enable_hand_mapping = 1, // Enabled by default
      .enable_debug = 0,
      .layer = LAYER_TOP, // Default to TOP for broader compatibility
      .overlay_position = POSITION_TOP,
      .cat_align = ALIGN_CENTER,
      .enable_scheduled_sleep = 0,
      .sleep_begin = (config_time_t){0, 0},
      .sleep_end = (config_time_t){0, 0},
      .idle_sleep_timeout_sec = 0,
      .disable_fullscreen_hide = 0,
  };
}

static void config_finalize(config_t *config) {
  // Initialize error system with debug setting
  herdcat_error_init(config->enable_debug);
}

static void config_log_summary(const config_t *config) {
  herdcat_log_debug("Configuration loaded successfully");
  herdcat_log_debug("  Screen: %dx%d", config->screen_width,
                    config->overlay_height);
  herdcat_log_debug("  Cat: %dx%d at offset (%d,%d)", config->cat_height,
                    (config->cat_height * CAT_IMAGE_WIDTH) / CAT_IMAGE_HEIGHT,
                    config->cat_x_offset, config->cat_y_offset);
  herdcat_log_debug("  FPS: %d, Opacity: %d", config->fps,
                    config->overlay_opacity);
  herdcat_log_debug("  Mirror: X=%d, Y=%d", config->mirror_x, config->mirror_y);
  herdcat_log_debug("  Anti-aliasing: %s",
                    config->enable_antialiasing ? "enabled" : "disabled");
  herdcat_log_debug("  Position: %s", config->overlay_position == POSITION_TOP
                                          ? "top"
                                          : "bottom");
  herdcat_log_debug("  Layer: %s",
                    config->layer == LAYER_TOP ? "top" : "overlay");
  herdcat_log_debug("  Monitors: %d configured", config->num_output_names);
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

herdcat_error_t load_config(config_t *config, const char *config_file_path) {
  HERDCAT_CHECK_NULL(config, HERDCAT_ERROR_INVALID_PARAM);

  validation_failed = false;
  // Initialize with defaults
  config_set_defaults(config);

  // Parse config file and override defaults
  herdcat_error_t result = config_parse_file(config, config_file_path);
  if (result != HERDCAT_SUCCESS) {
    herdcat_log_error("Failed to parse configuration file: %s",
                      herdcat_error_string(result));
    return result;
  }

  // Validate and sanitize configuration
  result = config_validate(config);
  if (result != HERDCAT_SUCCESS) {
    herdcat_log_error("Configuration validation failed: %s",
                      herdcat_error_string(result));
    return result;
  }

  if (strict_parse && validation_failed) {
    return HERDCAT_ERROR_CONFIG;
  }

  // Finalize configuration
  config_finalize(config);

  // Log configuration summary
  config_log_summary(config);

  return HERDCAT_SUCCESS;
}

void config_cleanup(void) {
  // No global config state to clean up.
}

void config_cleanup_full(config_t *config) {
  if (!config) {
    return;
  }

  for (size_t i = 0; i < config->num_overrides; i++) {
    free(config->overrides[i].monitor);
    free(config->overrides[i].key);
    free(config->overrides[i].value);
  }
  free(config->overrides);
  config->overrides = NULL;
  config->num_overrides = 0;
  config_cleanup_devices(config);

  if (config->output_name) {
    free(config->output_name);
    config->output_name = NULL;
  }

  config_free_string_array(&config->output_names, &config->num_output_names);
}

int get_screen_width(void) {
  // This function is now only used for initial config loading
  // The actual screen width detection happens in wayland_init
  return DEFAULT_SCREEN_WIDTH;
}

char *config_resolve_path(const char *explicit_path) {
  if (explicit_path) {
    return strdup(explicit_path);
  }

  char path[PATH_MAX];

  // 1. $XDG_CONFIG_HOME/herdcat/herdcat.conf
  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  if (xdg_config && xdg_config[0] != '\0') {
    snprintf(path, sizeof(path), "%s/herdcat/herdcat.conf", xdg_config);
    if (access(path, R_OK) == 0) {
      return strdup(path);
    }
  }

  // 2. ~/.config/herdcat/herdcat.conf
  const char *home = getenv("HOME");
  if (home && home[0] != '\0') {
    snprintf(path, sizeof(path), "%s/.config/herdcat/herdcat.conf", home);
    if (access(path, R_OK) == 0) {
      return strdup(path);
    }
  }

  // 3. ./herdcat.conf (CWD)
  if (access("herdcat.conf", R_OK) == 0) {
    return strdup("herdcat.conf");
  }

  // No config found — will use defaults
  return NULL;
}

herdcat_error_t load_config_strict(config_t *config, const char *path) {
  strict_parse = true;
  herdcat_error_t result = load_config(config, path);
  strict_parse = false;
  return result;
}

void config_for_monitor(const config_t *global, const char *name,
                        config_t *effective) {
  *effective = *global;
  for (size_t i = 0; i < global->num_overrides; i++) {
    const monitor_override_t *entry = &global->overrides[i];
    if (name && strcmp(entry->monitor, name) == 0) {
      herdcat_error_t result =
          config_parse_key_value(effective, entry->key, entry->value);
      (void)result;
    }
  }
  herdcat_error_t result = config_validate(effective);
  (void)result;
}

herdcat_error_t load_config_report(config_t *config, const char *path,
                                   bool strict,
                                   config_diagnostic_callback_t callback,
                                   void *data) {
  diagnostic_callback = callback;
  diagnostic_data = data;
  herdcat_error_t result = (int)strict ? load_config_strict(config, path)
                                       : load_config(config, path);
  diagnostic_callback = NULL;
  diagnostic_data = NULL;
  return result;
}

bool config_sign_english(const config_t *config) {
  if (config->sign_language != SIGN_LANGUAGE_AUTO)
    return config->sign_language == SIGN_LANGUAGE_EN;
  const char *locale = getenv("LC_MESSAGES");
  if (!locale || !*locale)
    locale = getenv("LANG");
  return !(locale && !strncmp(locale, "zh", 2) &&
           (locale[2] == '_' || locale[2] == '.' || locale[2] == '-' ||
            locale[2] == '@' || locale[2] == '\0'));
}

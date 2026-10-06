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

static char *config_trim_whitespace(char *text) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }

  if (*text == '\0') {
    return text;
  }

  char *end = text + strlen(text) - 1;
  while (end > text && (*end == ' ' || *end == '\t')) {
    *end = '\0';
    end--;
  }

  return text;
}

static bool config_parse_int(const char *str, int *out) {
  if (!str || str[0] == '\0') {
    return false;
  }
  errno = 0;
  char *endptr;
  long val = strtol(str, &endptr, 10);
  if (errno != 0 || endptr == str) {
    return false;
  }
  while (*endptr == ' ' || *endptr == '\t') {
    endptr++;
  }
  if (*endptr != '\0') {
    return false;
  }
  if (val < INT_MIN || val > INT_MAX) {
    return false;
  }
  *out = (int)val;
  return true;
}

static herdcat_error_t
config_parse_integer_key(config_t *config, const char *key, const char *value) {
  // Identify which field this key maps to (NULL = not an integer key)
  int *target = NULL;
  if (strcmp(key, "sign_max") == 0) {
    target = &config->sign_max;
  } else if (strcmp(key, "sign_font_size") == 0) {
    target = &config->sign_font_size;
  } else if (strcmp(key, "sign_typing_desk") == 0) {
    target = &config->sign_typing_desk;
  } else if (strcmp(key, "cat_x_offset") == 0) {
    target = &config->cat_x_offset;
  } else if (strcmp(key, "cat_y_offset") == 0) {
    target = &config->cat_y_offset;
  } else if (strcmp(key, "cat_draggable") == 0) {
    target = &config->cat_draggable;
  } else if (strcmp(key, "cat_height") == 0) {
    target = &config->cat_height;
  } else if (strcmp(key, "overlay_height") == 0) {
    target = &config->overlay_height;
  } else if (strcmp(key, "idle_frame") == 0) {
    target = &config->idle_frame;
  } else if (strcmp(key, "agent_interrupt_detect") == 0) {
    target = &config->agent_interrupt_detect;
  } else if (strcmp(key, "agent_stale_timeout") == 0) {
    target = &config->agent_stale_timeout;
  } else if (strcmp(key, "agent_done_timeout") == 0) {
    target = &config->agent_done_timeout;
  } else if (strcmp(key, "keypress_duration") == 0) {
    target = &config->keypress_duration;
  } else if (strcmp(key, "test_animation_duration") == 0) {
    target = &config->test_animation_duration;
  } else if (strcmp(key, "test_animation_interval") == 0) {
    target = &config->test_animation_interval;
  } else if (strcmp(key, "fps") == 0) {
    target = &config->fps;
  } else if (strcmp(key, "overlay_opacity") == 0) {
    target = &config->overlay_opacity;
  } else if (strcmp(key, "mirror_x") == 0) {
    target = &config->mirror_x;
  } else if (strcmp(key, "mirror_y") == 0) {
    target = &config->mirror_y;
  } else if (strcmp(key, "enable_antialiasing") == 0) {
    target = &config->enable_antialiasing;
  } else if (strcmp(key, "enable_hand_mapping") == 0) {
    target = &config->enable_hand_mapping;
  } else if (strcmp(key, "enable_debug") == 0) {
    target = &config->enable_debug;
  } else if (strcmp(key, "enable_scheduled_sleep") == 0) {
    target = &config->enable_scheduled_sleep;
  } else if (strcmp(key, "idle_sleep_timeout") == 0) {
    target = &config->idle_sleep_timeout_sec;
  } else if (strcmp(key, "hotplug_scan_interval") == 0) {
    target = &config->hotplug_scan_interval;
  } else if (strcmp(key, "disable_fullscreen_hide") == 0) {
    target = &config->disable_fullscreen_hide;
  }

  if (!target) {
    return HERDCAT_ERROR_INVALID_PARAM;  // Not an integer key
  }

  int int_value;
  if (!config_parse_int(value, &int_value)) {
    herdcat_log_warning("Invalid integer value '%s' for key '%s'", value, key);
    return HERDCAT_ERROR_INVALID_PARAM;
  }

  bool boolean_key =
      (target == &config->agent_interrupt_detect ||
       target == &config->sign_typing_desk ||
       target == &config->cat_draggable || target == &config->mirror_x ||
       target == &config->mirror_y || target == &config->enable_antialiasing ||
       target == &config->enable_hand_mapping ||
       target == &config->enable_debug ||
       target == &config->enable_scheduled_sleep ||
       target == &config->disable_fullscreen_hide) != 0;
  if (boolean_key && int_value != 0 && int_value != 1) {
    herdcat_log_warning("Invalid boolean value '%s' for key '%s'", value, key);
    return HERDCAT_ERROR_INVALID_PARAM;
  }

  struct {
    const char *key;
    int minimum, maximum;
  } ranges[] = {
      {"sign_max",                1,  5             },
      {"sign_font_size",          10, 20            },
      {"cat_height",              10, 200           },
      {"overlay_height",          20, 300           },
      {"fps",                     1,  120           },
      {"overlay_opacity",         0,  255           },
      {"idle_frame",              0,  NUM_FRAMES - 1},
      {"keypress_duration",       10, 5000          },
      {"test_animation_duration", 10, 5000          },
      {"test_animation_interval", 0,  3600          },
      {"hotplug_scan_interval",   0,  3600          },
      {"idle_sleep_timeout",      0,  3600          }
  };
  for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
    if (strcmp(key, ranges[i].key) == 0 &&
        (int_value < ranges[i].minimum || int_value > ranges[i].maximum)) {
      diagnostic(key, "value outside allowed range");
      if (strict_parse) {
        return HERDCAT_ERROR_CONFIG;
      }
    }
  }
  *target = int_value;
  return HERDCAT_SUCCESS;
}

static herdcat_error_t config_parse_enum_key(config_t *config, const char *key,
                                             const char *value) {
  const struct {
    const char *key, *value;
    int number;
  } signs[] = {
      {"sign_style",      "fan",     SIGN_STYLE_FAN    },
      {"sign_style",      "post",    SIGN_STYLE_POST   },
      {"sign_style",      "off",     SIGN_STYLE_OFF    },
      {"sign_idle",       "hover",   SIGN_IDLE_HOVER   },
      {"sign_idle",       "always",  SIGN_IDLE_ALWAYS  },
      {"sign_idle",       "never",   SIGN_IDLE_NEVER   },
      {"sign_animations", "full",    SIGN_ANIM_FULL    },
      {"sign_animations", "reduced", SIGN_ANIM_REDUCED },
      {"sign_animations", "off",     SIGN_ANIM_OFF     },
      {"sign_language",   "auto",    SIGN_LANGUAGE_AUTO},
      {"sign_language",   "en",      SIGN_LANGUAGE_EN  },
      {"sign_language",   "zh",      SIGN_LANGUAGE_ZH  },
      {"sign_done",       "sticky",  SIGN_DONE_STICKY  },
      {"sign_done",       "timeout", SIGN_DONE_TIMEOUT },
  };
  for (size_t i = 0; i < sizeof(signs) / sizeof(signs[0]); i++) {
    if (strcmp(key, signs[i].key) || strcmp(value, signs[i].value))
      continue;
    if (!strcmp(key, "sign_style"))
      config->sign_style = (sign_style_t)signs[i].number;
    else if (!strcmp(key, "sign_idle"))
      config->sign_idle = (sign_idle_t)signs[i].number;
    else if (!strcmp(key, "sign_animations"))
      config->sign_animations = (sign_animations_t)signs[i].number;
    else if (!strcmp(key, "sign_language"))
      config->sign_language = (sign_language_t)signs[i].number;
    else
      config->sign_done = (sign_done_t)signs[i].number;
    return HERDCAT_SUCCESS;
  }
  if (strcmp(key, "layer") == 0) {
    if (strcmp(value, "background") == 0) {
      config->layer = LAYER_BACKGROUND;
    } else if (strcmp(value, "bottom") == 0) {
      config->layer = LAYER_BOTTOM;
    } else if (strcmp(value, "top") == 0) {
      config->layer = LAYER_TOP;
    } else if (strcmp(value, "overlay") == 0) {
      config->layer = LAYER_OVERLAY;
    } else {
      if (strict_parse) {
        return HERDCAT_ERROR_CONFIG;
      }
      herdcat_log_warning("Invalid layer '%s', using 'top'", value);
      config->layer = LAYER_TOP;
    }
  } else if (strcmp(key, "overlay_position") == 0) {
    if (strcmp(value, "top") == 0) {
      config->overlay_position = POSITION_TOP;
    } else if (strcmp(value, "bottom") == 0) {
      config->overlay_position = POSITION_BOTTOM;
    } else {
      if (strict_parse) {
        return HERDCAT_ERROR_CONFIG;
      }
      herdcat_log_warning("Invalid overlay_position '%s', using 'top'", value);
      config->overlay_position = POSITION_TOP;
    }
  } else if (strcmp(key, "cat_align") == 0) {
    if (strcmp(value, "left") == 0) {
      config->cat_align = ALIGN_LEFT;
    } else if (strcmp(value, "center") == 0) {
      config->cat_align = ALIGN_CENTER;
    } else if (strcmp(value, "right") == 0) {
      config->cat_align = ALIGN_RIGHT;
    } else {
      if (strict_parse) {
        return HERDCAT_ERROR_CONFIG;
      }
      herdcat_log_warning("Invalid cat_align '%s', using 'center'", value);
      config->cat_align = ALIGN_CENTER;
    }
  } else {
    return HERDCAT_ERROR_INVALID_PARAM;  // Unknown key
  }

  return HERDCAT_SUCCESS;
}

static herdcat_error_t config_parse_time_key(config_t *config, const char *key,
                                             const char *value) {
  // Only try to parse time for time-related keys
  if (strcmp(key, "sleep_begin") != 0 && strcmp(key, "sleep_end") != 0) {
    return HERDCAT_ERROR_INVALID_PARAM;  // Not a time key
  }

  if (strlen(value) != 5 || (value[0] < '0' || value[0] > '9') ||
      (value[1] < '0' || value[1] > '9') || value[2] != ':' ||
      (value[3] < '0' || value[3] > '9') ||
      (value[4] < '0' || value[4] > '9')) {
    herdcat_log_warning("Invalid time format '%s', expected HH:MM", value);
    return HERDCAT_ERROR_INVALID_PARAM;
  }
  int hour = ((value[0] - '0') * 10) + (value[1] - '0');
  int min = ((value[3] - '0') * 10) + (value[4] - '0');

  if (hour < 0 || hour > 23 || min < 0 || min > 59) {
    herdcat_log_warning(
        "Invalid time values '%s', hour must be 0-23, minute must be 0-59",
        value);
    return HERDCAT_ERROR_INVALID_PARAM;
  }

  if (strcmp(key, "sleep_begin") == 0) {
    config->sleep_begin.hour = hour;
    config->sleep_begin.min = min;
  } else if (strcmp(key, "sleep_end") == 0) {
    config->sleep_end.hour = hour;
    config->sleep_end.min = min;
  }

  return HERDCAT_SUCCESS;
}

static herdcat_error_t config_parse_monitor_list(config_t *config,
                                                 const char *value) {
  config_free_string_array(&config->output_names, &config->num_output_names);
  free(config->output_name);
  config->output_name = NULL;

  char *monitor_list = strdup(value);
  if (!monitor_list) {
    return HERDCAT_ERROR_MEMORY;
  }

  char *saveptr = NULL;
  char *token = strtok_r(monitor_list, ",", &saveptr);
  while (token) {
    char *monitor_name = config_trim_whitespace(token);
    if (monitor_name[0] != '\0') {
      herdcat_error_t err = config_expand_array(
          &config->output_names, &config->num_output_names, monitor_name);
      if (err != HERDCAT_SUCCESS) {
        free(monitor_list);
        return err;
      }
    }

    token = strtok_r(NULL, ",", &saveptr);
  }

  free(monitor_list);

  if (config->num_output_names > 0) {
    config->output_name = strdup(config->output_names[0]);
    if (!config->output_name) {
      return HERDCAT_ERROR_MEMORY;
    }
  } else {
    herdcat_log_warning(
        "monitor is empty, falling back to automatic output selection");
  }

  return HERDCAT_SUCCESS;
}

static herdcat_error_t
config_parse_string_key(config_t *config, const char *key, const char *value) {
  if (strcmp(key, "sign_font") == 0) {
    if (strlen(value) >= sizeof(config->sign_font))
      return HERDCAT_ERROR_INVALID_PARAM;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
      if (*p < 32 || *p == 127)
        return HERDCAT_ERROR_INVALID_PARAM;
    snprintf(config->sign_font, sizeof(config->sign_font), "%s", value);
    return HERDCAT_SUCCESS;
  } else if (strcmp(key, "monitor") == 0) {
    return config_parse_monitor_list(config, value);
  } else if (strcmp(key, "keyboard_name") == 0) {
    return config_expand_array(&config->keyboard_names, &config->num_names,
                               value);
  } else {
    return HERDCAT_ERROR_INVALID_PARAM;  // Unknown key
  }
}

herdcat_error_t config_parse_key_value(config_t *config, const char *key,
                                       const char *value) {
  // Try integer keys first
  herdcat_error_t integer_result = config_parse_integer_key(config, key, value);
  if (integer_result != HERDCAT_ERROR_INVALID_PARAM) {
    return integer_result;
  }

  // Try enum keys
  herdcat_error_t enum_result = config_parse_enum_key(config, key, value);
  if (enum_result != HERDCAT_ERROR_INVALID_PARAM) {
    return enum_result;
  }

  // Try time keys
  if (config_parse_time_key(config, key, value) == HERDCAT_SUCCESS) {
    return HERDCAT_SUCCESS;
  }

  // Try string keys
  herdcat_error_t string_result = config_parse_string_key(config, key, value);
  if (string_result != HERDCAT_ERROR_INVALID_PARAM) {
    return string_result;
  }

  // Handle device keys
  if (strcmp(key, "keyboard_device") == 0 ||
      strcmp(key, "keyboard_devices") == 0) {
    // Validate path starts with /dev/input/ and has no traversal
    if (strncmp(value, "/dev/input/", 11) != 0) {
      herdcat_log_warning(
          "keyboard_device path must start with /dev/input/: %s", value);
      return HERDCAT_ERROR_INVALID_PARAM;
    }
    if (strstr(value, "..") != NULL) {
      herdcat_log_warning("Path traversal detected in device path: %s", value);
      return HERDCAT_ERROR_INVALID_PARAM;
    }
    return config_add_keyboard_device(config, value);
  }

  // Unknown key
  return HERDCAT_ERROR_INVALID_PARAM;
}

static bool config_is_comment_or_empty(const char *line) {
  const unsigned char *p = (const unsigned char *)line;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  return (*p == '#' || *p == ';' || *p == '\0') != 0;
}

static bool config_parse_line(char *line, char **out_key, char **out_value) {
  char *equals = strchr(line, '=');
  if (!equals) {
    return false;
  }

  *equals = '\0';
  *out_key = config_trim_whitespace(line);
  *out_value = config_trim_whitespace(equals + 1);

  // Support inline comments: key=value # comment
  if ((*out_value)[0] == '#') {
    (*out_value)[0] = '\0';
  } else {
    char *space_comment = strstr(*out_value, " #");
    char *tab_comment = strstr(*out_value, "\t#");
    char *comment_start = NULL;

    if (space_comment && tab_comment) {
      comment_start =
          (space_comment < tab_comment) ? space_comment : tab_comment;
    } else if (space_comment) {
      comment_start = space_comment;
    } else if (tab_comment) {
      comment_start = tab_comment;
    }

    if (comment_start) {
      comment_start[1] = '\0';
      *out_value = config_trim_whitespace(*out_value);
    }
  }

  return (*out_key)[0] != '\0';
}

static bool config_parse_section(char *trimmed, char *section,
                                 size_t section_size) {
  size_t n = strlen(trimmed);
  if (n > 2 && trimmed[n - 1] == ']') {
    trimmed[n - 1] = '\0';
    if (strcmp(trimmed + 1, "global") == 0) {
      section[0] = '\0';
      return true;
    }
    if (strncmp(trimmed + 1, "monitor:", 8) == 0 && strlen(trimmed + 9) > 0 &&
        strlen(trimmed + 9) < section_size) {
      snprintf(section, section_size, "%s", trimmed + 9);
      return true;
    }
  }
  return false;
}

herdcat_error_t config_parse_file(config_t *config,
                                  const char *config_file_path) {
  HERDCAT_CHECK_NULL(config, HERDCAT_ERROR_INVALID_PARAM);

  char resolved[PATH_MAX];
  const char *file_path = config_startup_path(config_file_path, resolved);

  snprintf(diagnostic_path, sizeof(diagnostic_path), "%s", file_path);
  diagnostic_line = 0;
  FILE *file = fopen(file_path, "r");
  if (!file) {
    if (strict_parse) {
      diagnostic(NULL, "cannot open configuration");
      return HERDCAT_ERROR_FILE_IO;
    }
    herdcat_log_info("Config file '%s' not found, using defaults", file_path);
    return HERDCAT_SUCCESS;
  }

  char line[512];
  int line_number = 0;
  char section[128] = "";
  herdcat_error_t result = HERDCAT_SUCCESS;

  while (fgets(line, sizeof(line), file)) {
    line_number++;
    diagnostic_line = line_number;

    // Reject truncated lines rather than interpreting fragments as settings.
    size_t len = strlen(line);
    if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
      int next;
      while ((next = fgetc(file)) != '\n' && next != EOF) {}
      diagnostic(NULL, "line too long");
      if (strict_parse) {
        result = HERDCAT_ERROR_CONFIG;
      }
      if (next == EOF) {
        break;
      }
      continue;
    }
    // Remove trailing newline
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }

    // Skip comments and empty lines
    if (config_is_comment_or_empty(line)) {
      continue;
    }

    char *trimmed = config_trim_whitespace(line);
    if (trimmed[0] == '[') {
      if (config_parse_section(trimmed, section, sizeof(section))) {
        continue;
      }
      diagnostic(NULL, "invalid section");
      if (strict_parse) {
        result = HERDCAT_ERROR_CONFIG;
      }
      continue;
    }
    // Parse key=value pairs
    char *key = NULL;
    char *value = NULL;
    if (config_parse_line(line, &key, &value)) {
      herdcat_error_t parse_result;
      if (section[0]) {
        parse_result =
            config_parse_monitor_setting(config, section, key, value);
      } else {
        parse_result = config_parse_key_value(config, key, value);
      }
      if (parse_result == HERDCAT_ERROR_INVALID_PARAM) {
        diagnostic(key, "unknown key or invalid value");
        if (strict_parse) {
          result = HERDCAT_ERROR_CONFIG;
        }
      } else if (parse_result != HERDCAT_SUCCESS) {
        diagnostic(key, parse_result == HERDCAT_ERROR_MEMORY
                            ? "allocation failed"
                            : "invalid value");
        result = parse_result;
        break;
      }
    } else if (strlen(line) > 0) {
      diagnostic(NULL, "malformed configuration line");
      if (strict_parse) {
        result = HERDCAT_ERROR_CONFIG;
      }
    }
  }

  if (ferror(file)) {
    result = HERDCAT_ERROR_FILE_IO;
  }
  fclose(file);

  if (result == HERDCAT_SUCCESS) {
    herdcat_log_info("Loaded configuration from %s", file_path);
  }

  return result;
}

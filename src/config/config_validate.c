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

static void config_clamp_int(int *value, int min, int max, const char *name) {
  if (*value < min || *value > max) {
    validation_failed = true;
    herdcat_log_warning("%s %d out of range [%d-%d], clamping", name, *value,
                        min, max);
    *value = (*value < min) ? min : max;
  }
}

static void config_validate_dimensions(config_t *config) {
  config_clamp_int(&config->cat_height, MIN_CAT_HEIGHT, MAX_CAT_HEIGHT,
                   "cat_height");
  config_clamp_int(&config->overlay_height, MIN_OVERLAY_HEIGHT,
                   MAX_OVERLAY_HEIGHT, "overlay_height");
}

static void config_validate_timing(config_t *config) {
  if (config->agent_done_timeout < 0 ||
      config->agent_done_timeout > MAX_INTERVAL) {
    validation_failed = true;
    herdcat_log_warning(
        "agent_done_timeout %d out of range [0-%d], resetting to %d",
        config->agent_done_timeout, MAX_INTERVAL, DEFAULT_AGENT_DONE_TIMEOUT);
    config->agent_done_timeout = DEFAULT_AGENT_DONE_TIMEOUT;
  }
  if (config->agent_stale_timeout < 0 ||
      config->agent_stale_timeout > MAX_AGENT_STALE_TIMEOUT) {
    validation_failed = true;
    herdcat_log_warning(
        "agent_stale_timeout %d out of range [0-%d], resetting to %d",
        config->agent_stale_timeout, MAX_AGENT_STALE_TIMEOUT,
        DEFAULT_AGENT_STALE_TIMEOUT);
    config->agent_stale_timeout = DEFAULT_AGENT_STALE_TIMEOUT;
  }
  config_clamp_int(&config->fps, MIN_FPS, MAX_FPS, "fps");
  config_clamp_int(&config->keypress_duration, MIN_DURATION, MAX_DURATION,
                   "keypress_duration");
  config_clamp_int(&config->test_animation_duration, MIN_DURATION, MAX_DURATION,
                   "test_animation_duration");

  // Validate interval (0 is allowed to disable)
  if (config->test_animation_interval < 0 ||
      config->test_animation_interval > MAX_INTERVAL) {
    validation_failed = true;
    herdcat_log_warning(
        "test_animation_interval %d out of range [0-%d], clamping",
        config->test_animation_interval, MAX_INTERVAL);
    config->test_animation_interval =
        (config->test_animation_interval < 0) ? 0 : MAX_INTERVAL;
  }

  if (config->hotplug_scan_interval < 0 ||
      config->hotplug_scan_interval > MAX_INTERVAL) {
    validation_failed = true;
    herdcat_log_warning(
        "hotplug_scan_interval %d out of range [0-%d], clamping",
        config->hotplug_scan_interval, MAX_INTERVAL);
    config->hotplug_scan_interval =
        (config->hotplug_scan_interval < 0) ? 0 : MAX_INTERVAL;
  }

  if (config->idle_sleep_timeout_sec < 0 ||
      config->idle_sleep_timeout_sec > MAX_INTERVAL) {
    validation_failed = true;
    herdcat_log_warning("idle_sleep_timeout %d out of range [0-%d], clamping",
                        config->idle_sleep_timeout_sec, MAX_INTERVAL);
    config->idle_sleep_timeout_sec =
        config->idle_sleep_timeout_sec < 0 ? 0 : MAX_INTERVAL;
  }
}

static void config_validate_appearance(config_t *config) {
  config_clamp_int(&config->sign_desk_offset, -6, 24, "sign_desk_offset");
  config_clamp_int(&config->sign_title_length, 0, 64, "sign_title_length");
  config_clamp_int(&config->sign_max, 1, 5, "sign_max");
  config_clamp_int(&config->sign_font_size, 10, 20, "sign_font_size");
  // Validate opacity
  config_clamp_int(&config->overlay_opacity, 0, 255, "overlay_opacity");

  // Validate idle frame
  if (config->idle_frame < 0 || config->idle_frame > HERDCAT_FRAME_LAST_USER) {
    validation_failed = true;
    herdcat_log_warning("idle_frame %d out of range [0-%d], resetting to 0",
                        config->idle_frame, HERDCAT_FRAME_LAST_USER);
    config->idle_frame = 0;
  }
}

static void config_validate_enums(config_t *config) {
  // Validate layer
  if (config->layer < LAYER_BACKGROUND || config->layer > LAYER_OVERLAY) {
    herdcat_log_warning("Invalid layer %d, resetting to top", config->layer);
    config->layer = LAYER_TOP;
  }

  // Validate overlay_position
  if (config->overlay_position != POSITION_TOP &&
      config->overlay_position != POSITION_BOTTOM) {
    herdcat_log_warning("Invalid overlay_position %d, resetting to top",
                        config->overlay_position);
    config->overlay_position = POSITION_TOP;
  }
}

static void config_validate_positioning(config_t *config) {
  // Validate cat positioning doesn't go off-screen
  if (llabs((long long)config->cat_x_offset) > config->screen_width) {
    herdcat_log_warning(
        "cat_x_offset %d may position cat off-screen (screen width: %d)",
        config->cat_x_offset, config->screen_width);
  }
}

static void config_validate_time(config_t *config) {
  if (config->enable_scheduled_sleep) {
    const int begin_minutes =
        (config->sleep_begin.hour * 60) + config->sleep_begin.min;
    const int end_minutes =
        (config->sleep_end.hour * 60) + config->sleep_end.min;

    if (begin_minutes == end_minutes) {
      herdcat_log_warning("Sleep mode is enabled, but time is equal: "
                          "%02d:%02d, disable sleep mode",
                          config->sleep_begin.hour, config->sleep_begin.min);

      config->enable_scheduled_sleep = 0;
    }
  }
}

herdcat_error_t config_validate(config_t *config) {
  HERDCAT_CHECK_NULL(config, HERDCAT_ERROR_INVALID_PARAM);

  // Normalize boolean values
  config->enable_debug = config->enable_debug ? 1 : 0;
  config->enable_scheduled_sleep = config->enable_scheduled_sleep ? 1 : 0;

  config_validate_dimensions(config);
  config_validate_timing(config);
  config_validate_appearance(config);
  config_validate_enums(config);
  config_validate_positioning(config);

  // Normalize boolean values
  config->cat_draggable = config->cat_draggable ? 1 : 0;
  config->mirror_x = config->mirror_x ? 1 : 0;
  config->mirror_y = config->mirror_y ? 1 : 0;
  config->enable_antialiasing = config->enable_antialiasing ? 1 : 0;
  config_validate_time(config);
  return HERDCAT_SUCCESS;
}

#ifndef HERDCAT_CONFIG_INTERNAL_H
#define HERDCAT_CONFIG_INTERNAL_H

#include "config/config.h"

#include <limits.h>

#define MIN_CAT_HEIGHT              10
#define MAX_CAT_HEIGHT              200
#define MIN_OVERLAY_HEIGHT          20
#define MAX_OVERLAY_HEIGHT          300
#define MIN_FPS                     1
#define MAX_FPS                     120
#define MIN_DURATION                10
#define MAX_DURATION                5000
#define MAX_INTERVAL                3600
#define DEFAULT_AGENT_DONE_TIMEOUT  5
#define DEFAULT_AGENT_STALE_TIMEOUT 600
#define MAX_AGENT_STALE_TIMEOUT     86400

extern bool strict_parse;
extern bool validation_failed;
extern char diagnostic_path[PATH_MAX];
extern int diagnostic_line;

void diagnostic(const char *key, const char *message);
herdcat_error_t config_validate(config_t *config);
herdcat_error_t config_expand_array(char ***array_ptr, int *count,
                                    const char *str);
herdcat_error_t config_add_keyboard_device(config_t *config,
                                           const char *device_path);
void config_free_string_array(char ***array_ptr, int *count);
herdcat_error_t config_parse_key_value(config_t *config, const char *key,
                                       const char *value);
herdcat_error_t config_parse_monitor_setting(config_t *config,
                                             const char *monitor,
                                             const char *key,
                                             const char *value);
const char *config_startup_path(const char *config_file_path,
                                char resolved[PATH_MAX]);
herdcat_error_t config_parse_file(config_t *config,
                                  const char *config_file_path);

#endif

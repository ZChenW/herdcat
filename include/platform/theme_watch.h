#ifndef HERDCAT_THEME_WATCH_H
#define HERDCAT_THEME_WATCH_H
#include "config/sign_options.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Returns false for malformed, unrelated or out-of-range messages.
bool theme_parse(const char *text, size_t length, bool signal,
                 sign_theme_t *theme);
bool theme_tool_available(void);
void theme_watch_configure(sign_theme_t choice);
void theme_watch_ready(uint32_t token);
void theme_watch_poll(void);
int theme_watch_timeout(void);
void theme_watch_cleanup(void);
#endif

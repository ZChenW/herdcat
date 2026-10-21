#ifndef HERDCAT_PREFS_H
#define HERDCAT_PREFS_H

#include "config/sign_options.h"

#include <stddef.h>

// ${XDG_STATE_HOME:-$HOME/.local/state}/herdcat/prefs
// Each line is three tab-separated fields: key, choice, and the config
// value at the time of the choice. A choice may contain spaces. Lines
// written before that change are three space-separated fields and still
// load. A record applies only while the config value still matches its
// third field. A missing or damaged file leaves the caller's values
// unchanged. A stale record is deleted so a later edit of the config wins.

// 0 applied or nothing to apply, -1 the pointers are missing or a stale
// record could not be removed. font is the sign_font buffer.
int prefs_resolve(sign_style_t *style, sign_language_t *language, char *font,
                  size_t font_size, sign_theme_t *theme);

// Record a menu choice against the config value last passed to
// prefs_resolve. Style is fan or post. Language is en or zh.
// Theme is light or dark. Font is a
// family name, or empty for the default face. 0 on success, -1 on error.
int prefs_choose_style(sign_style_t chosen);
int prefs_choose_language(sign_language_t chosen);
int prefs_choose_theme(sign_theme_t chosen);
int prefs_choose_font(const char *family);

#endif

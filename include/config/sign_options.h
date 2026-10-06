#ifndef HERDCAT_SIGN_OPTIONS_H
#define HERDCAT_SIGN_OPTIONS_H

typedef enum {
  SIGN_STYLE_OFF,
  SIGN_STYLE_POST,
  SIGN_STYLE_FAN
} sign_style_t;
typedef enum {
  SIGN_ANIM_FULL,
  SIGN_ANIM_REDUCED,
  SIGN_ANIM_OFF
} sign_animations_t;
typedef enum {
  SIGN_IDLE_HOVER,
  SIGN_IDLE_ALWAYS,
  SIGN_IDLE_NEVER
} sign_idle_t;
typedef enum {
  SIGN_LANGUAGE_AUTO,
  SIGN_LANGUAGE_EN,
  SIGN_LANGUAGE_ZH
} sign_language_t;
typedef enum {
  SIGN_THEME_LIGHT,
  SIGN_THEME_DARK
} sign_theme_t;
typedef enum {
  SIGN_DONE_STICKY,
  SIGN_DONE_TIMEOUT
} sign_done_t;

#endif

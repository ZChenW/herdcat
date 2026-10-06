#define _GNU_SOURCE
#include "platform/prefs.h"
#include "test_helpers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/herdcat-prefs-XXXXXX";
static char *saved_home;
static char *saved_state;

static void use_root(void) {
  TEST_ASSERT(setenv("XDG_STATE_HOME", root, 1) == 0);
}
static void write_text(const char *text) {
  char path[512];
  snprintf(path, sizeof(path), "%s/herdcat", root);
  TEST_ASSERT(mkdir(path, 0700) == 0 || errno == EEXIST);
  snprintf(path, sizeof(path), "%s/herdcat/prefs", root);
  FILE *file = fopen(path, "w");
  TEST_ASSERT(file != NULL);
  TEST_ASSERT(fputs(text, file) >= 0);
  TEST_ASSERT(fclose(file) == 0);
}
static char font_buf[128];
static void resolve_ok(sign_style_t *style, sign_language_t *language) {
  sign_theme_t theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(
      prefs_resolve(style, language, font_buf, sizeof(font_buf), &theme) == 0);
}
static char *slurp(void) {
  char path[512];
  snprintf(path, sizeof(path), "%s/herdcat/prefs", root);
  FILE *file = fopen(path, "r");
  TEST_ASSERT(file != NULL);
  char *buffer = malloc(4096);
  TEST_ASSERT(buffer != NULL);
  size_t bytes = fread(buffer, 1, 4095, file);
  TEST_ASSERT(!ferror(file));
  fclose(file);
  buffer[bytes] = '\0';
  return buffer;
}
static void round_trip(void) {
  use_root();
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_AUTO;
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_FAN && language == SIGN_LANGUAGE_AUTO);
  TEST_ASSERT(prefs_choose_style(SIGN_STYLE_POST) == 0);
  TEST_ASSERT(prefs_choose_language(SIGN_LANGUAGE_ZH) == 0);
  TEST_ASSERT(prefs_choose_style(SIGN_STYLE_OFF) == -1);
  TEST_ASSERT(prefs_choose_language(SIGN_LANGUAGE_AUTO) == -1);
  char path[512];
  snprintf(path, sizeof(path), "%s/herdcat/prefs", root);
  struct stat st;
  TEST_ASSERT(stat(path, &st) == 0);
  TEST_ASSERT(S_ISREG(st.st_mode) && st.st_uid == getuid());
  TEST_ASSERT((st.st_mode & 0777) == 0600);
  style = SIGN_STYLE_FAN;
  language = SIGN_LANGUAGE_AUTO;
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_POST && language == SIGN_LANGUAGE_ZH);
  char *text = slurp();
  TEST_ASSERT(strstr(text, "sign_style\tpost\tfan\n"));
  TEST_ASSERT(strstr(text, "sign_language\tzh\tauto\n"));
  free(text);
}
static void config_changed(void) {
  use_root();
  sign_style_t style = SIGN_STYLE_OFF;
  sign_language_t language = SIGN_LANGUAGE_AUTO;
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_OFF && language == SIGN_LANGUAGE_ZH);
  char *text = slurp();
  TEST_ASSERT(!strstr(text, "sign_style"));
  TEST_ASSERT(strstr(text, "sign_language\tzh\tauto\n"));
  free(text);
  style = SIGN_STYLE_OFF;
  language = SIGN_LANGUAGE_EN;
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_OFF && language == SIGN_LANGUAGE_EN);
  text = slurp();
  TEST_ASSERT(!strstr(text, "sign_language"));
  free(text);
}
static void corrupt_ignored(void) {
  use_root();
  write_text("sign_style post fan\nbogus\n");
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_AUTO;
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_FAN && language == SIGN_LANGUAGE_AUTO);
  char *text = slurp();
  TEST_ASSERT(!strcmp(text, "sign_style post fan\nbogus\n"));
  free(text);
  TEST_ASSERT(prefs_choose_style(SIGN_STYLE_POST) == -1);
  TEST_ASSERT(prefs_choose_font("Noto Sans") == -1);
  text = slurp();
  TEST_ASSERT(!strcmp(text, "sign_style post fan\nbogus\n"));
  free(text);
}
static void old_format(void) {
  use_root();
  const char *old = "sign_style post fan\nsign_language en auto\n";
  write_text(old);
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_AUTO;
  snprintf(font_buf, sizeof(font_buf), "Noto Sans");
  resolve_ok(&style, &language);
  TEST_ASSERT(style == SIGN_STYLE_POST && language == SIGN_LANGUAGE_EN);
  TEST_ASSERT(!strcmp(font_buf, "Noto Sans"));
  char *text = slurp();
  TEST_ASSERT(!strcmp(text, old));
  free(text);
}
static void font_spaces(void) {
  use_root();
  char path[512];
  snprintf(path, sizeof(path), "%s/herdcat/prefs", root);
  unlink(path);
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_EN;
  font_buf[0] = '\0';
  resolve_ok(&style, &language);
  TEST_ASSERT(prefs_choose_font("Noto Sans CJK SC") == 0);
  TEST_ASSERT(prefs_choose_font("bad\tfont") == -1);
  char big[129];
  memset(big, 'A', 128);
  big[128] = '\0';
  TEST_ASSERT(prefs_choose_font(big) == -1);
  font_buf[0] = '\0';
  resolve_ok(&style, &language);
  TEST_ASSERT(!strcmp(font_buf, "Noto Sans CJK SC"));
  char *text = slurp();
  TEST_ASSERT(strstr(text, "sign_font\tNoto Sans CJK SC\t\n"));
  free(text);
  snprintf(font_buf, sizeof(font_buf), "Other Face");
  resolve_ok(&style, &language);
  TEST_ASSERT(!strcmp(font_buf, "Other Face"));
  text = slurp();
  TEST_ASSERT(!strstr(text, "sign_font"));
  free(text);
  snprintf(font_buf, sizeof(font_buf), "Source Han Sans");
  resolve_ok(&style, &language);
  TEST_ASSERT(prefs_choose_font(NULL) == 0);
  snprintf(font_buf, sizeof(font_buf), "Source Han Sans");
  resolve_ok(&style, &language);
  TEST_ASSERT(font_buf[0] == '\0');
  text = slurp();
  TEST_ASSERT(strstr(text, "sign_font\t\tSource Han Sans\n"));
  free(text);
}
static void theme_round_trip(void) {
  use_root();
  write_text("sign_style\tpost\tfan\nsign_font\tNoto Sans\t\n");
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_AUTO;
  sign_theme_t theme = SIGN_THEME_LIGHT;
  font_buf[0] = '\0';
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_LIGHT);
  TEST_ASSERT(prefs_choose_theme(SIGN_THEME_DARK) == 0);
  TEST_ASSERT(prefs_choose_theme((sign_theme_t)3) == -1);
  char *text = slurp();
  TEST_ASSERT(strstr(text, "sign_theme\tdark\tlight\n"));
  TEST_ASSERT(strstr(text, "sign_style\tpost\tfan\n"));
  TEST_ASSERT(strstr(text, "sign_font\tNoto Sans\t\n"));
  free(text);
  style = SIGN_STYLE_FAN;
  font_buf[0] = '\0';
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_DARK);
  TEST_ASSERT(prefs_choose_theme(SIGN_THEME_LIGHT) == 0);
  theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_LIGHT);
  // An explicit config edit invalidates the stored override permanently.
  theme = SIGN_THEME_DARK;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_DARK);
  text = slurp();
  TEST_ASSERT(!strstr(text, "sign_theme"));
  free(text);
  theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(prefs_choose_theme(SIGN_THEME_AUTO) == 0);
  theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_AUTO);
  write_text("sign_theme dark light\n");
  theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_DARK);
  write_text("sign_theme\tbogus\tlight\n");
  theme = SIGN_THEME_LIGHT;
  TEST_ASSERT(prefs_resolve(&style, &language, font_buf, sizeof(font_buf),
                            &theme) == 0);
  TEST_ASSERT(theme == SIGN_THEME_LIGHT);
  TEST_ASSERT(prefs_choose_theme(SIGN_THEME_DARK) == -1);
  write_text("");
}
static void home_fallback(void) {
  TEST_ASSERT(unsetenv("XDG_STATE_HOME") == 0);
  TEST_ASSERT(setenv("HOME", root, 1) == 0);
  sign_style_t style = SIGN_STYLE_FAN;
  sign_language_t language = SIGN_LANGUAGE_EN;
  resolve_ok(&style, &language);
  TEST_ASSERT(prefs_choose_style(SIGN_STYLE_FAN) == 0);
  char path[512];
  snprintf(path, sizeof(path), "%s/.local/state/herdcat/prefs", root);
  FILE *file = fopen(path, "r");
  TEST_ASSERT(file != NULL);
  char line[64];
  TEST_ASSERT(fgets(line, sizeof(line), file) != NULL);
  fclose(file);
  TEST_ASSERT(!strcmp(line, "sign_style\tfan\tfan\n"));
}
int main(void) {
  saved_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
  saved_state =
      getenv("XDG_STATE_HOME") ? strdup(getenv("XDG_STATE_HOME")) : NULL;
  TEST_ASSERT(mkdtemp(root) != NULL);
  round_trip();
  config_changed();
  corrupt_ignored();
  old_format();
  font_spaces();
  theme_round_trip();
  home_fallback();
  if (saved_home)
    setenv("HOME", saved_home, 1);
  if (saved_state)
    setenv("XDG_STATE_HOME", saved_state, 1);
  else
    unsetenv("XDG_STATE_HOME");
  free(saved_home);
  free(saved_state);
  return 0;
}

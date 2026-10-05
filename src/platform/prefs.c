#define _GNU_SOURCE
#include "platform/prefs.h"

#include "utils/error.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PREFS_FILE_MAX 4096

typedef struct {
  bool style_set, language_set, font_set;
  sign_style_t style, style_config;
  sign_language_t language, language_config;
  char font[128], font_config[128];
} prefs_t;

static bool have_config;
static sign_style_t seen_style;
static sign_language_t seen_language;
static char seen_font[128];

static int state_dir(bool create) {
  const char *base = getenv("XDG_STATE_HOME");
  char path[PATH_MAX];
  int length;
  if (base && base[0] == '/') {
    length = snprintf(path, sizeof(path), "%s/bongocat", base);
  } else {
    base = getenv("HOME");
    if (!base || base[0] != '/') {
      errno = EINVAL;
      return -1;
    }
    length = snprintf(path, sizeof(path), "%s/.local/state/bongocat", base);
  }
  if (length < 0 || (size_t)length >= sizeof(path)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  char *save = NULL;
  for (char *part = strtok_r(path, "/", &save); dir >= 0 && part;
       part = strtok_r(NULL, "/", &save)) {
    int next =
        openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create) {
      if (mkdirat(dir, part, 0700) == 0 || errno == EEXIST) {
        next =
            openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      }
    }
    int saved = errno;
    close(dir);
    dir = next;
    errno = saved;
  }
  if (dir >= 0) {
    struct stat st;
    if (fstat(dir, &st) || st.st_uid != getuid() ||
        (create && fchmod(dir, 0700))) {
      close(dir);
      errno = EACCES;
      return -1;
    }
  }
  return dir;
}
static bool parse_style(const char *text, sign_style_t *value, bool chosen) {
  if (!strcmp(text, "fan"))
    *value = SIGN_STYLE_FAN;
  else if (!strcmp(text, "post"))
    *value = SIGN_STYLE_POST;
  else if (!chosen && !strcmp(text, "off"))
    *value = SIGN_STYLE_OFF;
  else
    return false;
  return true;
}
static bool parse_language(const char *text, sign_language_t *value,
                           bool chosen) {
  if (!strcmp(text, "en"))
    *value = SIGN_LANGUAGE_EN;
  else if (!strcmp(text, "zh"))
    *value = SIGN_LANGUAGE_ZH;
  else if (!chosen && !strcmp(text, "auto"))
    *value = SIGN_LANGUAGE_AUTO;
  else
    return false;
  return true;
}
static const char *style_text(sign_style_t value) {
  if (value == SIGN_STYLE_FAN)
    return "fan";
  if (value == SIGN_STYLE_POST)
    return "post";
  if (value == SIGN_STYLE_OFF)
    return "off";
  return NULL;
}
static const char *language_text(sign_language_t value) {
  if (value == SIGN_LANGUAGE_EN)
    return "en";
  if (value == SIGN_LANGUAGE_ZH)
    return "zh";
  if (value == SIGN_LANGUAGE_AUTO)
    return "auto";
  return NULL;
}
static bool font_text(const char *text) {
  if (!text || strlen(text) >= 128)
    return false;
  for (const char *p = text; *p; p++)
    if ((unsigned char)*p < 0x20 || *p == 0x7f)
      return false;
  return true;
}
static bool split_line(char *line, char **key, char **choice, char **config) {
  char *tab = strchr(line, '\t');
  if (tab) {
    *tab = '\0';
    *key = line;
    *choice = tab + 1;
    tab = strchr(*choice, '\t');
    if (!tab || !**key)
      return false;
    *tab = '\0';
    *config = tab + 1;
    if (strchr(*config, '\t'))
      return false;
    size_t n = strlen(*config);
    if (n && (*config)[n - 1] == '\r')
      (*config)[n - 1] = '\0';
    return true;
  }
  char *save = NULL;
  *key = strtok_r(line, " \t\r", &save);
  *choice = strtok_r(NULL, " \t\r", &save);
  *config = strtok_r(NULL, " \t\r", &save);
  char *extra = strtok_r(NULL, " \t\r", &save);
  return *key && *choice && *config && !extra;
}
// 0 parsed, 1 absent, -1 damaged or unreadable.
static int read_prefs(int dir, prefs_t *prefs) {
  memset(prefs, 0, sizeof(*prefs));
  int fd = openat(dir, "prefs", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return errno == ENOENT ? 1 : -1;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
      st.st_size > PREFS_FILE_MAX) {
    close(fd);
    return -1;
  }
  char contents[PREFS_FILE_MAX + 1];
  ssize_t bytes = read(fd, contents, sizeof(contents));
  int saved = errno;
  close(fd);
  if (bytes < 0) {
    errno = saved;
    return -1;
  }
  if ((size_t)bytes == sizeof(contents) ||
      memchr(contents, '\0', (size_t)bytes))
    return -1;
  contents[bytes] = '\0';
  for (char *line = contents, *next; line; line = next) {
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    if (!*line)
      continue;
    char *key = NULL, *choice = NULL, *config = NULL;
    prefs_t next_prefs = *prefs;
    bool ok = split_line(line, &key, &choice, &config);
    if (ok && !strcmp(key, "sign_style")) {
      ok = parse_style(choice, &next_prefs.style, true) &&
           parse_style(config, &next_prefs.style_config, false);
      next_prefs.style_set = ok;
    } else if (ok && !strcmp(key, "sign_language")) {
      ok = parse_language(choice, &next_prefs.language, true) &&
           parse_language(config, &next_prefs.language_config, false);
      next_prefs.language_set = ok;
    } else if (ok && !strcmp(key, "sign_font")) {
      ok = font_text(choice) && font_text(config);
      if (ok) {
        snprintf(next_prefs.font, sizeof(next_prefs.font), "%s", choice);
        snprintf(next_prefs.font_config, sizeof(next_prefs.font_config), "%s",
                 config);
      }
      next_prefs.font_set = ok;
    } else {
      ok = false;
    }
    if (!ok)
      return -1;
    *prefs = next_prefs;
  }
  return 0;
}
static int write_prefs(int dir, const prefs_t *prefs) {
  static unsigned sequence;
  char temp[64];
  int fd = -1;
  for (int i = 0; i < 16; i++) {
    snprintf(temp, sizeof(temp), ".prefs.%ld.%u", (long)getpid(), sequence++);
    fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                0600);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
    return -1;
  int result = fchmod(fd, 0600);
  if (!result && prefs->style_set &&
      dprintf(fd, "sign_style\t%s\t%s\n", style_text(prefs->style),
              style_text(prefs->style_config)) < 0)
    result = -1;
  if (!result && prefs->language_set &&
      dprintf(fd, "sign_language\t%s\t%s\n", language_text(prefs->language),
              language_text(prefs->language_config)) < 0)
    result = -1;
  if (!result && prefs->font_set &&
      dprintf(fd, "sign_font\t%s\t%s\n", prefs->font, prefs->font_config) < 0)
    result = -1;
  if (!result)
    result = fsync(fd);
  if (close(fd))
    result = -1;
  if (!result)
    result = renameat(dir, temp, dir, "prefs");
  if (result)
    unlinkat(dir, temp, 0);
  return result;
}
int prefs_resolve(sign_style_t *style, sign_language_t *language, char *font,
                  size_t font_size) {
  if (!style || !language || !font || font_size < 2 ||
      strlen(font) >= font_size || strlen(font) >= sizeof(seen_font))
    return -1;
  seen_style = *style;
  seen_language = *language;
  snprintf(seen_font, sizeof(seen_font), "%s", font);
  have_config = true;
  int dir = state_dir(false);
  if (dir < 0)
    return errno == ENOENT ? 0 : -1;
  prefs_t prefs;
  int loaded = read_prefs(dir, &prefs);
  if (loaded) {
    close(dir);
    return 0;
  }
  bool stale = false;
  if (prefs.style_set && prefs.style_config == *style)
    *style = prefs.style;
  else if (prefs.style_set) {
    prefs.style_set = false;
    stale = true;
  }
  if (prefs.language_set && prefs.language_config == *language)
    *language = prefs.language;
  else if (prefs.language_set) {
    prefs.language_set = false;
    stale = true;
  }
  if (prefs.font_set && !strcmp(prefs.font_config, font))
    snprintf(font, font_size, "%s", prefs.font);
  else if (prefs.font_set) {
    prefs.font_set = false;
    stale = true;
  }
  int wrote = 0;
  if (stale)
    wrote = write_prefs(dir, &prefs);
  close(dir);
  return wrote;
}
static int choose(bool style, int value) {
  if (!have_config)
    return -1;
  if (style) {
    if (value != SIGN_STYLE_FAN && value != SIGN_STYLE_POST)
      return -1;
  } else if (value != SIGN_LANGUAGE_EN && value != SIGN_LANGUAGE_ZH) {
    return -1;
  }
  int dir = state_dir(true);
  if (dir < 0)
    return -1;
  prefs_t prefs;
  int loaded = read_prefs(dir, &prefs);
  if (loaded < 0) {
    close(dir);
    return -1;
  }
  if (loaded > 0)
    memset(&prefs, 0, sizeof(prefs));
  if (style) {
    prefs.style_set = true;
    prefs.style = (sign_style_t)value;
    prefs.style_config = seen_style;
  } else {
    prefs.language_set = true;
    prefs.language = (sign_language_t)value;
    prefs.language_config = seen_language;
  }
  int wrote = write_prefs(dir, &prefs);
  close(dir);
  return wrote;
}
int prefs_choose_style(sign_style_t chosen) {
  return choose(true, (int)chosen);
}
int prefs_choose_language(sign_language_t chosen) {
  return choose(false, (int)chosen);
}
int prefs_choose_font(const char *family) {
  if (!have_config)
    return -1;
  if (!family)
    family = "";
  if (!font_text(family))
    return -1;
  int dir = state_dir(true);
  if (dir < 0)
    return -1;
  prefs_t prefs;
  int loaded = read_prefs(dir, &prefs);
  if (loaded < 0) {
    close(dir);
    return -1;
  }
  if (loaded > 0)
    memset(&prefs, 0, sizeof(prefs));
  prefs.font_set = true;
  snprintf(prefs.font, sizeof(prefs.font), "%s", family);
  snprintf(prefs.font_config, sizeof(prefs.font_config), "%s", seen_font);
  int wrote = write_prefs(dir, &prefs);
  close(dir);
  return wrote;
}

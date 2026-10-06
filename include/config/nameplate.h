#ifndef HERDCAT_NAMEPLATE_H
#define HERDCAT_NAMEPLATE_H
#include "config/sign_options.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NAMEPLATE_RUN_MAX  80
#define NAMEPLATE_TEXT_MAX 4096

typedef struct {
  uint16_t start, length;
  uint8_t line, segment;
  bool bold, state, gap;
} nameplate_run_t;
typedef struct {
  char text[NAMEPLATE_TEXT_MAX];
  nameplate_run_t runs[NAMEPLATE_RUN_MAX];
  int count, lines;
  bool legacy;
} nameplate_t;
typedef struct {
  const char *name, *project, *title, *agent, *state;
} nameplate_fields_t;
// Returns a fixed diagnostic, never the template or an expanded title.
const char *nameplate_validate(const char *source, size_t *position);
const char *nameplate_builtin(sign_name_extra_t extra, bool title_main);
void nameplate_expand(const char *source, const nameplate_fields_t *fields,
                      nameplate_t *out);
#endif

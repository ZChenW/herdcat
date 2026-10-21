#ifndef HERDCAT_SIGN_PALETTE_H
#define HERDCAT_SIGN_PALETTE_H

#include "config/sign_options.h"
#include "core/agent_state.h"

#include <stdint.h>

// Opaque ARGB colours shared by signs, the switch card and the font panel.
typedef struct {
  uint32_t ink, paper;
  uint32_t fills[AGENT_STATE_COUNT], icons[AGENT_STATE_COUNT];
  uint32_t secondary, hover, count, plate, meta;
} sign_palette_t;

const sign_palette_t *sign_palette(sign_theme_t theme);

sign_theme_t sign_theme_effective(sign_theme_t choice);
void sign_theme_system(sign_theme_t theme);
#endif

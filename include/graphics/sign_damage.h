#ifndef HERDCAT_SIGN_DAMAGE_H
#define HERDCAT_SIGN_DAMAGE_H

#include "graphics/pixel_rect.h"
#include "graphics/signs.h"

// Conservative logical-pixel damage between two sign frames. Settled frames
// repaint only changed shapes and text; transitions and changed element
// counts keep the full sign bounds. The caller separately accounts for cat
// changes.
pixel_rect_t sign_damage(const sign_frame_t *before, const sign_frame_t *after);

#endif

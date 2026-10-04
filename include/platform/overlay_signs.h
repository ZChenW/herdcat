#ifndef BONGOCAT_OVERLAY_SIGNS_H
#define BONGOCAT_OVERLAY_SIGNS_H

#include "config/config.h"
#include "graphics/signs.h"
#include "platform/focus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// Cat, up to five boards, and the open hover pad.
#define OVERLAY_SIGNS_REGION_LIMIT 7

typedef struct {
  int x, y, w, h;
} overlay_signs_rect_t;

// timeout_ms is -1 when signs need no poll wake. frame asks for one
// wl_surface.frame; a full-rate transition must not also poll at 0.
// A positive model deadline becomes timeout_ms. The 150 ms close may
// be reported together with frame.
typedef struct {
  int timeout_ms;
  bool frame;
  bool redraw;
  bool damage_full;
  int damage_x, damage_y, damage_w, damage_h;
} overlay_signs_step_t;

int overlay_signs_height(const config_t *config);
int overlay_signs_cat_y(const config_t *config, int surface_height);
int64_t overlay_signs_now(void);

overlay_signs_step_t overlay_signs_step(size_t index, const config_t *config,
                                        int cat_x, int cat_w, int surface_h,
                                        bool invisible, int64_t now_ms);
overlay_signs_step_t overlay_signs_last(size_t index);
const sign_frame_t *overlay_signs_frame(size_t index);

int overlay_signs_regions(size_t index, const config_t *config, int cat_x,
                          int cat_w, int surface_h, overlay_signs_rect_t *out,
                          int capacity);

// Records the pointer. True when it is on a board (pointer cursor).
bool overlay_signs_pointer(size_t index, double x, double y);
void overlay_signs_leave(void);
// True when the press landed on a board.
bool overlay_signs_press(size_t index);
bool overlay_signs_release(bool dragged, size_t *index, pid_t *pid,
                           uint64_t *key);

// A key arrived. Ignored unless the focus stream has a focused session.
void overlay_signs_note_key(void);
// Show or refresh the desk for this session. Key 0 is ignored.
void overlay_signs_type_at(uint64_t key, int64_t now_ms);
// Working again puts that session's sign back up.
void overlay_signs_note_working(uint64_t key);
// Leave the desk when this is no longer the focused session. 0 clears it.
void overlay_signs_sync_focus(uint64_t key);
void overlay_signs_fail(size_t index, uint64_t key, int64_t now_ms);
void overlay_signs_arm_focus(size_t index, uint64_t key);
void overlay_signs_note_focus(focus_result_t result, int64_t now_ms);
void overlay_signs_cleanup(void);
// Called once when a cat's signs open. NULL does nothing.
void overlay_signs_on_expand(void (*fn)(void));

#endif

#ifndef HERDCAT_COMPOSITOR_INTERNAL_H
#define HERDCAT_COMPOSITOR_INTERNAL_H
#include "platform/compositor.h"
void focus_watch_apply(const focus_watch_event_t *event,
                       const focus_window_t *parsed);
void focus_watch_lost(void);
// Shared bounded transport; called only by selected Sway/Hyprland ops.
int compositor_stream_connect(bool sway);
void compositor_stream_events(void);
void compositor_stream_ready(uint32_t token);
int compositor_stream_timeout(void);
bool compositor_stream_available(void);
void compositor_stream_cleanup(void);
#endif

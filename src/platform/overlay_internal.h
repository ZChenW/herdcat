#ifndef HERDCAT_PLATFORM_OVERLAY_INTERNAL_H
#define HERDCAT_PLATFORM_OVERLAY_INTERNAL_H

#include "platform/outputs.h"
#include "platform/shm_buffer.h"
#include "platform/wayland.h"

typedef struct {
  uint32_t output_id;
  struct wl_output *output;
  char name[128];
  config_t config;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer;
  struct wp_viewport *viewport;
  struct wp_fractional_scale_v1 *fractional;
  shm_buffer_t *buffers[2];
  void *animation;
  uint32_t scale;
  int width, height, physical_width, physical_height;
  int cat_x, margin_y, output_height;
  bool has_position, accepts_pointer;
  bool configured, redraw, resize, closed, damage_all;
  pixel_rect_t pending_damage;
} overlay_t;

extern overlay_t overlays[MAX_OUTPUTS];
extern overlay_t *active;
extern overlay_t *pointer_overlay;
extern bool hidden;
extern struct wl_seat *seat;
extern struct wp_cursor_shape_manager_v1 *cursor_manager;
extern struct wp_cursor_shape_device_v1 *cursor_device;
extern uint32_t seat_id, cursor_manager_id;
extern struct wl_callback *sign_frames[MAX_OUTPUTS];
extern const struct wl_seat_listener SEAT_LISTENER;

int cat_width(const overlay_t *overlay);
void clamp_position(overlay_t *overlay);
bool overlay_hidden(const overlay_t *overlay);
void set_margin(overlay_t *overlay);
void cursor_shape(uint32_t shape);
void finish_drag(void);
void setup_cursor(void);
void release_seat(void);

#endif

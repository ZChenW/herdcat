#include "config/sign_options.h"
#include "core/agent_adapters.h"
#include "graphics/signs.h"
#include "signs_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void snap(sign_scalar_t *scalar, double value) {
  scalar->from = scalar->target = value;
  scalar->duration = 0;
}

static uint32_t mix_rgb(uint32_t from, uint32_t to, double amount) {
  amount = clamp_unit(amount);
  uint32_t mixed = 0xff000000U;
  for (int shift = 0; shift <= 16; shift += 8) {
    double start = (double)((from >> shift) & 255);
    double end = (double)((to >> shift) & 255);
    mixed |= (uint32_t)lround(start + (end - start) * amount) << shift;
  }
  return mixed;
}

static void capsule(sign_frame_t *frame, double x1, double y1, double x2,
                    double y2, double width, uint32_t color) {
  double dx = x2 - x1, dy = y2 - y1;
  double length = hypot(dx, dy);
  if (length < 0.05 || width <= 0)
    return;
  double cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
  add_shape(frame, SIGN_RECT, cx - width / 2, cy - length / 2, width, length,
            width / 2, 0, color, 0);
  // Rotation is clockwise on screen (y grows downward), so a segment that
  // runs left as it descends needs a positive angle.
  frame->shapes[frame->shape_count - 1].rotation =
      atan2(-dx, dy) * (180.0 / 3.141592653589793);
}

static void glyph_line(sign_frame_t *frame, double ox, double oy, double scale,
                       double x1, double y1, double x2, double y2,
                       uint32_t color) {
  capsule(frame, ox + x1 * scale, oy + y1 * scale, ox + x2 * scale,
          oy + y2 * scale, 1.8 * scale, color);
}

static void glyph_rect(sign_frame_t *frame, double ox, double oy, double scale,
                       double x, double y, double w, double h, double radius,
                       double rotation, uint32_t color) {
  double grow = 0.9 * scale;
  add_shape(frame, SIGN_RECT, ox + x * scale - grow, oy + y * scale - grow,
            w * scale + grow * 2, h * scale + grow * 2, radius * scale + grow,
            0, color, 0);
  if (fabs(rotation) >= 0.05)
    frame->shapes[frame->shape_count - 1].rotation = rotation;
}

static void glyph_fan(sign_frame_t *frame, double ox, double oy, double scale,
                      uint32_t color) {
  glyph_line(frame, ox, oy, scale, 13, 17, 6.5, 8, color);
  glyph_line(frame, ox, oy, scale, 13, 17, 13, 7, color);
  glyph_line(frame, ox, oy, scale, 13, 17, 19.5, 8, color);
  glyph_rect(frame, ox, oy, scale, 1.5, 3, 7, 5.5, 1.6, -28, color);
  glyph_rect(frame, ox, oy, scale, 9.5, 1, 7, 5.5, 1.6, 0, color);
  glyph_rect(frame, ox, oy, scale, 17.5, 3, 7, 5.5, 1.6, 28, color);
}

static void glyph_post(sign_frame_t *frame, double ox, double oy, double scale,
                       uint32_t color) {
  glyph_line(frame, ox, oy, scale, 13, 17, 13, 2, color);
  glyph_rect(frame, ox, oy, scale, 14.5, 2.5, 9, 5, 1.6, 0, color);
  glyph_rect(frame, ox, oy, scale, 2.5, 9.5, 9, 5, 1.6, 0, color);
}

static void place_card(sign_shape_t *shape, double ox, double oy, double appear,
                       double cat_scale) {
  double fitted = 0.88 + 0.12 * appear;
  double drop = (1 - appear) * 12 * cat_scale;
  shape->x = ox + (shape->x - ox) * fitted;
  shape->y = oy + (shape->y - oy) * fitted + drop;
  shape->w *= fitted;
  shape->h *= fitted;
  shape->radius *= fitted;
  shape->stroke *= fitted;
  shape->above = true;
}

static sign_rect_t place_rect(double x, double y, double w, double h, double ox,
                              double oy, double appear, double cat_scale) {
  double fitted = 0.88 + 0.12 * appear;
  double drop = (1 - appear) * 12 * cat_scale;
  return cover(ox + (x - ox) * fitted, oy + (y - oy) * fitted + drop,
               w * fitted, h * fitted);
}

static const char *font_label(const sign_input_t *in) {
  if (in->menu_font[0])
    return in->menu_font;
  return in->menu_english ? "Default" : "默认";
}

static void note_font(signs_t *model, const sign_input_t *in,
                      const char *label) {
  if (!strcmp(model->menu.font_label, label))
    return;
  snprintf(model->menu.font_label, sizeof(model->menu.font_label), "%s", label);
  if (!in->menu_font_dir) {
    snap(&model->menu.font_in, 1);
    return;
  }
  model->menu.font_dir = in->menu_font_dir > 0 ? 1 : -1;
  model->menu.font_in.from = 0;
  model->menu.font_in.target = 0;
  model->menu.font_in.duration = 0;
}

void emit_menu(signs_t *model, const sign_input_t *in, sign_frame_t *frame,
               double scale) {
  if (in->menu_tap == 1 || in->menu_tap == 2)
    frame->menu_paw = in->menu_tap;
  bool was_closed = model->menu.open.target == 0 &&
                    sample(&model->menu.open, in->now_ms) == 0;
  if (was_closed) {
    snap(&model->menu.style, in->menu_post ? 1 : 0);
    snap(&model->menu.language, in->menu_english ? 1 : 0);
    snap(&model->menu.style_color, in->menu_post ? 1 : 0);
    snap(&model->menu.language_color, in->menu_english ? 1 : 0);
    snap(&model->menu.font_in, 1);
    snap(&model->menu.arrow, 1);
    snprintf(model->menu.font_label, sizeof(model->menu.font_label), "%s",
             font_label(in));
  }
  if (!in->menu && in->desk_snap) {
    snap(&model->menu.open, 0);
    snap(&model->menu.fade, 0);
    snap(&model->menu.holder, 0);
    snap(&model->menu.holder_fade, 0);
  }
  double appear =
      aim(&model->menu.open, in->menu ? 1 : 0, 260, &BEZIER_POP, in, frame);
  double fade = clamp_unit(
      aim(&model->menu.fade, in->menu ? 1 : 0, 140, &BEZIER_EASE, in, frame));
  double holder =
      aim(&model->menu.holder, in->menu ? 1 : 0, 260, &BEZIER_MOVE, in, frame);
  double holder_fade =
      clamp_unit(aim(&model->menu.holder_fade, in->menu ? 1 : 0, 140,
                     &BEZIER_EASE, in, frame));
  double style_pos = aim(&model->menu.style, in->menu_post ? 1 : 0, 280,
                         &BEZIER_MOVE, in, frame);
  double lang_pos = aim(&model->menu.language, in->menu_english ? 1 : 0, 280,
                        &BEZIER_MOVE, in, frame);
  double style_color = aim(&model->menu.style_color, in->menu_post ? 1 : 0, 180,
                           &BEZIER_EASE, in, frame);
  double lang_color = aim(&model->menu.language_color, in->menu_english ? 1 : 0,
                          180, &BEZIER_EASE, in, frame);
  double holder_h = 54 * scale * holder;
  if (holder_h > 0.4 && holder_fade > 0.01) {
    add_shape(frame, SIGN_RECT, in->cat_x + 97 * scale, in->cat_y - 6 * scale,
              5 * scale, holder_h, 2.5 * scale, 1.5 * scale,
              with_alpha(PAPER, holder_fade), with_alpha(INK, holder_fade));
  }
  if (appear <= 0.001 && fade <= 0.01)
    return;
  double card_x = in->cat_x + 22 * scale;
  double card_y = in->cat_y - 136 * scale;
  double card_w = 154 * scale;
  double card_h = 130 * scale;
  double origin_x = card_x + card_w / 2;
  double origin_y = card_y + card_h;
  int from = frame->shape_count;
  add_shape(frame, SIGN_RECT, card_x, card_y, card_w, card_h, 14 * scale,
            2 * scale, with_alpha(PAPER, fade), with_alpha(INK, fade));
  double track_x = card_x + 12 * scale;
  double track_w = 130 * scale;
  double track_h = 30 * scale;
  double rows[2] = {card_y + 12 * scale, card_y + 50 * scale};
  double font_y = card_y + 88 * scale;
  double positions[2] = {style_pos, lang_pos};
  double colors[2] = {style_color, lang_color};
  for (int row = 0; row < 2; row++) {
    add_shape(frame, SIGN_RECT, track_x, rows[row], track_w, track_h,
              11 * scale, 2 * scale, with_alpha(PAPER, fade),
              with_alpha(INK, fade));
    double thumb_x = track_x + (4 + positions[row] * 61) * scale;
    double thumb_y = rows[row] + 4 * scale;
    add_shape(frame, SIGN_RECT, thumb_x, thumb_y, 61 * scale, 22 * scale,
              7 * scale, 0, with_alpha(INK, fade), 0);
  }
  double pad_x = track_x + 2 * scale;
  double pad_w = 126 * scale;
  double pad_h = 26 * scale;
  double cell = pad_w / 2;
  double icon_w = 26 * scale;
  double icon_h = 18 * scale;
  for (int row = 0; row < 2; row++) {
    double icon_y = rows[row] + 2 * scale + (pad_h - icon_h) / 2;
    uint32_t left = with_alpha(mix_rgb(PAPER, INK, colors[row]), fade);
    uint32_t right = with_alpha(mix_rgb(INK, PAPER, colors[row]), fade);
    double left_x = pad_x + (cell - icon_w) / 2;
    double right_x = pad_x + cell + (cell - icon_w) / 2;
    if (row == 0) {
      glyph_fan(frame, left_x, icon_y, scale, left);
      glyph_post(frame, right_x, icon_y, scale, right);
    }
  }
  add_shape(frame, SIGN_RECT, track_x, font_y, track_w, track_h, 11 * scale,
            2 * scale, with_alpha(PAPER, fade), with_alpha(INK, fade));
  note_font(model, in, font_label(in));
  double enter =
      clamp_unit(aim(&model->menu.font_in, 1, 200, &BEZIER_SLIDE, in, frame));
  double arrow_scale = 1;
  if (in->menu_arrow) {
    model->menu.arrow_id = in->menu_arrow;
    snap(&model->menu.arrow, 0.8);
    arrow_scale = 0.8;
  } else if (model->menu.arrow.target == 0) {
    snap(&model->menu.arrow, 1);
  } else {
    arrow_scale = aim(&model->menu.arrow, 1, 180, &BEZIER_POP, in, frame);
  }
  uint32_t ink = with_alpha(INK, fade);
  double side = 26 * scale;
  for (int end = 0; end < 2; end++) {
    bool pressed = model->menu.arrow_id == end + 1;
    double shrink = pressed ? arrow_scale : 1;
    double cx = track_x + (end ? track_w - side / 2 : side / 2);
    double cy = font_y + track_h / 2;
    double s = scale * shrink;
    double tip = end ? 2.5 : -2.5;
    double wing = end ? -2 : 2;
    capsule(frame, cx + wing * s, cy - 5 * s, cx + tip * s, cy, 2 * s, ink);
    capsule(frame, cx + tip * s, cy, cx + wing * s, cy + 5 * s, 2 * s, ink);
  }
  if (in->menu_font_hot)
    add_shape(frame, SIGN_RECT, track_x + side, font_y + 2 * scale,
              track_w - side * 2, track_h - 4 * scale, 7 * scale, 0,
              with_alpha(0xffe3e8f0U, fade), 0);
  for (int i = from; i < frame->shape_count; i++) {
    place_card(&frame->shapes[i], origin_x, origin_y, appear, scale);
    include_bounds(frame, frame->shapes[i].x, frame->shapes[i].y,
                   frame->shapes[i].w, frame->shapes[i].h);
  }
  frame->menu_open = true;
  frame->menu_card = place_rect(card_x, card_y, card_w, card_h, origin_x,
                                origin_y, appear, scale);
  for (int row = 0; row < 2; row++) {
    sign_rect_t *halves = row == 0 ? frame->menu_style : frame->menu_lang;
    halves[0] = place_rect(track_x, rows[row], track_w / 2, track_h, origin_x,
                           origin_y, appear, scale);
    halves[1] = place_rect(track_x + track_w / 2, rows[row], track_w / 2,
                           track_h, origin_x, origin_y, appear, scale);
    double thumb_x = track_x + (4 + positions[row] * 61) * scale;
    sign_rect_t thumb =
        place_rect(thumb_x, rows[row] + 4 * scale, 61 * scale, 22 * scale,
                   origin_x, origin_y, appear, scale);
    if (row == 0)
      frame->menu_style_thumb = thumb;
    else
      frame->menu_lang_thumb = thumb;
  }
  frame->menu_font = place_rect(track_x, font_y, track_w, track_h, origin_x,
                                origin_y, appear, scale);
  frame->menu_font_prev = place_rect(track_x, font_y, side, track_h, origin_x,
                                     origin_y, appear, scale);
  frame->menu_font_next =
      place_rect(track_x + track_w - side, font_y, side, track_h, origin_x,
                 origin_y, appear, scale);
  if (frame->text_count + 3 > SIGN_MAX_TEXTS)
    return;
  double ratio = font_ratio(in);
  for (int half = 0; half < 2; half++) {
    sign_text_t *text = &frame->texts[frame->text_count++];
    double x = track_x + half * (track_w / 2);
    double y = rows[1];
    double fitted = 0.88 + 0.12 * appear;
    double drop = (1 - appear) * 12 * scale;
    double left = origin_x + (x - origin_x) * fitted;
    double top = origin_y + (y - origin_y) * fitted + drop;
    double width = (track_w / 2) * fitted;
    double height = track_h * fitted;
    uint32_t color = half == 0 ? mix_rgb(PAPER, INK, lang_color)
                               : mix_rgb(INK, PAPER, lang_color);
    double line_h = 13 * scale * ratio * fitted;
    double border = 2 * scale * fitted;
    double content_h = height - border * 2;
    *text = (sign_text_t){.x = left,
                          .line_top = top + border + (content_h - line_h) / 2,
                          .line_h = line_h,
                          .w = width,
                          .clip_y = top,
                          .clip_h = height,
                          .px = line_h,
                          .color = with_alpha(color, fade),
                          .above = true,
                          .center = true};
    snprintf(text->value, sizeof(text->value), "%s", half ? "EN" : "中");
  }
  sign_text_t *font = &frame->texts[frame->text_count++];
  double fitted = 0.88 + 0.12 * appear;
  double drop = (1 - appear) * 12 * scale;
  double shift = (1 - enter) * 14 * scale * (model->menu.font_dir < 0 ? -1 : 1);
  double x = track_x + side;
  double width = track_w - side * 2;
  double left = origin_x + (x - origin_x) * fitted;
  double top = origin_y + (font_y - origin_y) * fitted + drop;
  double line_h = 13 * scale * ratio * fitted;
  double border = 2 * scale * fitted;
  double height = track_h * fitted;
  double content_h = height - border * 2;
  *font = (sign_text_t){.x = left,
                        .line_top = top + border + (content_h - line_h) / 2,
                        .line_h = line_h,
                        .w = width * fitted,
                        .clip_y = top,
                        .clip_h = height,
                        .px = line_h,
                        .color = with_alpha(INK, fade * enter),
                        .above = true,
                        .center = true,
                        .slide = shift * fitted};
  snprintf(font->value, sizeof(font->value), "%s", font_label(in));
  if (in->menu_font[0])
    snprintf(font->family, sizeof(font->family), "%s", in->menu_font);
}

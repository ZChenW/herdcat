#include "graphics/signs.h"
#include "test_helpers.h"

#include <math.h>
#include <string.h>

static void near(double got, double want) {
  TEST_ASSERT(fabs(got - want) < 0.03);
}
static void check_bounds(const sign_frame_t *frame) {
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    TEST_ASSERT(shape->x >= frame->bounds_x && shape->y >= frame->bounds_y);
    TEST_ASSERT(shape->x + shape->w <= frame->bounds_x + frame->bounds_w);
    TEST_ASSERT(shape->y + shape->h <= frame->bounds_y + frame->bounds_h);
  }
}
static const sign_shape_t *board_near(const sign_frame_t *frame, double x,
                                      double y) {
  const sign_shape_t *found = NULL;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (shape->stroke <= 0)
      continue;
    if (fabs(shape->x - x) < 0.2 && fabs(shape->y - y) < 0.2)
      found = shape;
  }
  TEST_ASSERT(found);
  return found;
}
static const sign_shape_t *pole_of(const sign_frame_t *frame) {
  const sign_shape_t *found = NULL;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (shape->kind == SIGN_RECT && shape->stroke > 0 && shape->w < shape->h &&
        (!found || shape->h > found->h))
      found = shape;
  }
  TEST_ASSERT(found);
  return found;
}
static void session(agent_session_view_t *out, int index, agent_state_t state,
                    const char *agent) {
  memset(out, 0, sizeof(*out));
  out->key = (uint64_t)index + 1;
  out->order = (uint64_t)index + 1;
  out->state = state;
  out->pid = 1000 + index;
  strcpy(out->agent, agent);
  strcpy(out->name, "项目 with spaces");
}
static sign_input_t base_input(const agent_session_view_t *sessions,
                               size_t count) {
  sign_input_t in = {.sessions = sessions,
                     .count = count,
                     .style = SIGN_STYLE_POST,
                     .animations = SIGN_ANIM_OFF,
                     .now_ms = 1000,
                     .cat_x = 100,
                     .cat_y = 170,
                     .cat_height = 110};
  return in;
}
static void test_lifecycle(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t sessions[5];
  for (int i = 0; i < 5; i++)
    session(&sessions[i], i, AGENT_STATE_DONE, i % 2 ? "codex" : "claude");
  sign_input_t in = base_input(sessions, 5);
  in.animations = SIGN_ANIM_REDUCED;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 0);
  for (int64_t time = 1001; time < 1550; time += 7) {
    in.now_ms = time;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.bounds_y >= 0);
    check_bounds(&frame);
  }
  in.now_ms = 1600;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.hit_count == 5);
  TEST_ASSERT(frame.hits[0].w == 34 && frame.bounds_y >= 0);
  for (int i = 0; i < frame.hit_count; i++) {
    const sign_hit_t *hit = &frame.hits[i];
    int index = (int)hit->key - 1;
    TEST_ASSERT(hit->y == 280 - 100 - (4 - index) * 30 - 26);
    TEST_ASSERT(hit->x == (index % 2 ? 250 - 5 - 34 : 250 + 5));
  }
  check_bounds(&frame);
  in.open = true;
  in.now_ms = 1700;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.animating);
  in.now_ms = 2200;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.hit_count == 5 &&
              frame.text_count == 5);
  for (int i = 0; i < frame.hit_count; i++)
    TEST_ASSERT(frame.hits[i].w == 204);
  TEST_ASSERT(frame.bounds_y >= 0);
  check_bounds(&frame);
  in.open = false;
  in.now_ms = 2300;
  signs_frame(&model, &in, &frame);
  in.now_ms = 2400;
  signs_frame(&model, &in, &frame);
  sign_frame_t before = frame;
  in.open = true;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == before.hit_count);
  TEST_ASSERT(!memcmp(frame.hits, before.hits,
                      (size_t)frame.hit_count * sizeof(frame.hits[0])));
  in.open = false;
  sessions[0].state = AGENT_STATE_WAITING;
  in.now_ms = 3000;
  signs_frame(&model, &in, &frame);
  in.now_ms = 3600;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.text_count == 1);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "等你批准"));
  in.animations = SIGN_ANIM_FULL;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 3633);
  in.animations = SIGN_ANIM_OFF;
  sessions[0].state = AGENT_STATE_IDLE;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.hit_count == 4);
  for (int i = 0; i < 5; i++)
    sessions[i].state = AGENT_STATE_IDLE;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.hit_count && !frame.animating && frame.next_frame_ms == 0);
  in.open = true;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 5);
  in.animations = SIGN_ANIM_REDUCED;
  in.count = 0;
  in.now_ms = 4000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.animating && !frame.hit_count && frame.shape_count > 2);
  in.now_ms = 4600;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && !frame.hit_count && frame.shape_count == 2);
  in.style = SIGN_STYLE_OFF;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.shape_count && !frame.has_pad);
}
static void test_rows_and_direction(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t sessions[4];
  session(&sessions[0], 0, AGENT_STATE_IDLE, "claude");
  session(&sessions[1], 1, AGENT_STATE_WORKING, "claude");
  session(&sessions[2], 2, AGENT_STATE_IDLE, "codex");
  session(&sessions[3], 3, AGENT_STATE_DONE, "codex");
  sessions[1].order = 1;
  sessions[3].order = 3;
  sign_input_t in = base_input(sessions, 4);
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 2);
  const sign_hit_t *top = NULL, *bottom = NULL;
  for (int i = 0; i < frame.hit_count; i++) {
    if (frame.hits[i].key == 2)
      top = &frame.hits[i];
    if (frame.hits[i].key == 4)
      bottom = &frame.hits[i];
  }
  TEST_ASSERT(top && bottom);
  TEST_ASSERT(top->y == 280 - 130 - 26 && bottom->y == 280 - 100 - 26);
  TEST_ASSERT(top->x == 250 + 5 && bottom->x == 250 + 5);
  TEST_ASSERT(top->h == 26 && top->pid == 1001);
  const sign_shape_t *pole = pole_of(&frame);
  near(pole->h, 166);
  near(pole->w, 6);
  near(pole->radius, 3);
  near(pole->stroke, 2);
  near(pole->x + pole->w / 2, 250);
  TEST_ASSERT((pole->fill & 0xffffff) == 0xf8fafc);
  TEST_ASSERT((pole->outline & 0xffffff) == 0x111827);
  const sign_shape_t *claude = board_near(&frame, top->x, top->y);
  const sign_shape_t *codex = board_near(&frame, bottom->x, bottom->y);
  near(claude->radius, 8);
  near(codex->radius, 13);
  TEST_ASSERT((claude->fill & 0xffffff) == 0xd9ebff);
  TEST_ASSERT((codex->fill & 0xffffff) == 0xc7f1d6);
  TEST_ASSERT(claude->rotation == 0 && codex->rotation == 0);
}
static void test_easing_hover_and_press(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t session0;
  session(&session0, 0, AGENT_STATE_DONE, "claude");
  sign_input_t in = base_input(&session0, 1);
  signs_frame(&model, &in, &frame);
  near(pole_of(&frame)->h, 136);
  TEST_ASSERT(frame.hit_count == 1 && frame.hits[0].w == 34);
  in.animations = SIGN_ANIM_REDUCED;
  in.open = true;
  in.now_ms = 2000;
  signs_frame(&model, &in, &frame);
  in.now_ms = 2190;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hit_count == 1);
  near(board_near(&frame, frame.hits[0].x, frame.hits[0].y)->w, 202.541);
  in.now_ms = 2500;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.hits[0].w == 204);
  double rested = frame.hits[0].x;
  in.has_hover = true;
  in.hover_key = session0.key;
  in.now_ms = 2600;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)rested);
  in.now_ms = 2800;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)rested + 5);
  in.has_pressed = true;
  in.pressed_key = session0.key;
  in.has_hover = false;
  in.now_ms = 3000;
  signs_frame(&model, &in, &frame);
  const sign_shape_t *pressed = NULL;
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].stroke == 2)
      pressed = &frame.shapes[i];
  TEST_ASSERT(pressed && (pressed->fill >> 24) == 255);
  in.now_ms = 3200;
  signs_frame(&model, &in, &frame);
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].stroke == 2)
      pressed = &frame.shapes[i];
  TEST_ASSERT((pressed->fill >> 24) == (uint32_t)lround(0.85 * 255));
  agent_session_view_t pair[2];
  session(&pair[0], 0, AGENT_STATE_DONE, "claude");
  session(&pair[1], 1, AGENT_STATE_DONE, "codex");
  signs_t stacked = {0};
  sign_input_t hover = base_input(pair, 2);
  hover.has_hover = true;
  hover.hover_key = pair[1].key;
  signs_frame(&stacked, &hover, &frame);
  int back = -1, front = -1;
  for (int i = 0; i < frame.shape_count; i++) {
    if (frame.shapes[i].stroke != 2)
      continue;
    if (frame.shapes[i].x >= 250)
      back = i;
    else
      front = i;
  }
  TEST_ASSERT(back >= 0 && front > back);
}
static void test_icons_text_and_loops(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t one;
  session(&one, 0, AGENT_STATE_WORKING, "claude");
  one.state_since_ms = 0;
  sign_input_t in = base_input(&one, 1);
  in.open = true;
  in.now_ms = 150000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.text_count == 1);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "2 分钟"));
  TEST_ASSERT(!strcmp(frame.texts[0].value, "项目 with spaces"));
  near(frame.texts[0].px, 13);
  near(frame.texts[0].meta_px, 11.5);
  near(frame.texts[0].gap, 7);
  near(frame.texts[0].line_top, frame.hits[0].y + 6.5);
  near(frame.texts[0].line_h, 13);
  near(frame.texts[0].x, frame.hits[0].x + 33);
  TEST_ASSERT(!frame.texts[0].reverse && !frame.texts[0].above);
  TEST_ASSERT((frame.texts[0].meta_color & 0xffffff) == 0x4a5261);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 180000);
  in.animations = SIGN_ANIM_FULL;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.next_frame_ms == (150000 / 180 + 1) * 180);
  int dots = 0;
  for (int i = 0; i < frame.shape_count; i++) {
    const sign_shape_t *shape = &frame.shapes[i];
    if (shape->w == 4 && shape->h == 4) {
      dots++;
      TEST_ASSERT(shape->clipped && shape->radius == 2);
    }
  }
  TEST_ASSERT(dots == 3);
  in.animations = SIGN_ANIM_OFF;
  one.state = AGENT_STATE_WAITING;
  one.state_since_ms = 150000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "等你批准"));
  TEST_ASSERT((frame.texts[0].meta_color & 0xffffff) == 0x71430b);
  int bars = 0, checks = 0;
  for (int i = 0; i < frame.shape_count; i++) {
    if (frame.shapes[i].w == 3.5 && frame.shapes[i].h == 9)
      bars++;
    if (frame.shapes[i].kind == SIGN_CHECK)
      checks++;
  }
  TEST_ASSERT(bars == 1 && checks == 0);
  one.state = AGENT_STATE_DONE;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "完成"));
  const sign_shape_t *check = NULL;
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].kind == SIGN_CHECK)
      check = &frame.shapes[i];
  TEST_ASSERT(check && check->fill == 0 && check->stroke == 3);
  TEST_ASSERT((check->outline & 0xffffff) == 0x22643d);
  one.unread = true;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "完成 · 未查看"));
  const sign_shape_t *badge = NULL;
  const sign_shape_t *board = NULL;
  for (int i = 0; i < frame.shape_count; i++) {
    const sign_shape_t *shape = &frame.shapes[i];
    if (fabs(shape->w - 9) < 0.02 && fabs(shape->h - 9) < 0.02)
      badge = shape;
    if (fabs(shape->h - 26) < 0.02 && shape->w > 100)
      board = shape;
  }
  TEST_ASSERT(badge && board && badge->stroke == 2);
  TEST_ASSERT(fabs(badge->radius - 4.5) < 0.02);
  TEST_ASSERT((badge->fill & 0xffffff) == 0x22643d);
  TEST_ASSERT((badge->outline & 0xffffff) == 0x111827);
  near(badge->x, board->x + board->w - 4);
  near(badge->y, board->y - 5);
  in.style = SIGN_STYLE_FAN;
  signs_frame(&model, &in, &frame);
  badge = NULL;
  const sign_shape_t *plate = NULL;
  for (int i = 0; i < frame.shape_count; i++) {
    const sign_shape_t *shape = &frame.shapes[i];
    if (fabs(shape->w - 9) < 0.02 && fabs(shape->h - 9) < 0.02)
      badge = shape;
    if (shape->w > 20 && shape->h > 20)
      plate = shape;
  }
  TEST_ASSERT(badge && plate);
  near(badge->x, plate->x + plate->w - 4);
  near(badge->y, plate->y - 5);
  in.has_hover = true;
  in.hover_key = one.key;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(strstr(frame.texts[0].meta, "已完成 · 未查看"));
  one.unread = false;
  in.has_hover = false;
  in.style = SIGN_STYLE_POST;
  one.state = AGENT_STATE_IDLE;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!strcmp(frame.texts[0].meta, "空闲"));
  int dashes = 0;
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].w == 10 && frame.shapes[i].h == 3) {
      dashes++;
      near(frame.shapes[i].radius, 2);
    }
  TEST_ASSERT(dashes == 1);
  one.state = AGENT_STATE_WAITING;
  one.order = 2;
  signs_t left = {0};
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&left, &in, &frame);
  TEST_ASSERT(frame.texts[0].reverse);
  near(frame.texts[0].x, frame.hits[0].x + 8);
  in.animations = SIGN_ANIM_FULL;
  in.now_ms = 150000;
  signs_frame(&left, &in, &frame);
  double origin = frame.hits[0].x;
  in.now_ms = 150750;
  signs_frame(&left, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)floor(origin - 6));
  TEST_ASSERT(frame.next_frame_ms == 150750 + 33);
  in.animations = SIGN_ANIM_REDUCED;
  signs_frame(&left, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)origin);
  TEST_ASSERT(!frame.animating);
}
static void test_failure_scale_and_clearance(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t one;
  session(&one, 0, AGENT_STATE_DONE, "claude");
  sign_input_t in = base_input(&one, 1);
  in.open = true;
  signs_frame(&model, &in, &frame);
  double rested = frame.hits[0].x;
  signs_focus_failed(&model, one.key, 4000);
  in.animations = SIGN_ANIM_REDUCED;
  in.now_ms = 4000 + 29;
  signs_frame(&model, &in, &frame);
  double shake = 29.0 / 350;
  double offset = sin(shake * 6 * 3.141592653589793) * 5 * (1 - shake);
  const sign_shape_t *shaken = NULL;
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].stroke == 2)
      shaken = &frame.shapes[i];
  TEST_ASSERT(shaken);
  near(shaken->x, rested + offset);
  in.now_ms = 4400;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)rested);
  in.animations = SIGN_ANIM_OFF;
  signs_focus_failed(&model, one.key, 5000);
  in.now_ms = 5029;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.hits[0].x == (int)rested);
  signs_t scaled = {0};
  in.animations = SIGN_ANIM_OFF;
  in.cat_height = 220;
  in.cat_x = 0;
  in.cat_y = 0;
  in.now_ms = 6000;
  signs_frame(&scaled, &in, &frame);
  TEST_ASSERT(frame.hit_count == 1);
  TEST_ASSERT(frame.hits[0].x == 300 + 10 && frame.hits[0].w == 408);
  TEST_ASSERT(frame.hits[0].h == 52);
  near(frame.texts[0].gap, 14);
  near(pole_of(&frame)->h, 272);
  TEST_ASSERT(frame.has_pad);
  TEST_ASSERT(frame.pad.x == (int)floor(0 - 140));
  TEST_ASSERT(frame.pad.y == (int)floor(0 - 320));
  TEST_ASSERT(frame.pad.w == 880 && frame.pad.h == 540);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_OFF, 110) == 0);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_POST, 0) == 0);
  int clearance = sign_clearance(SIGN_STYLE_POST, 110);
  TEST_ASSERT(clearance >= 160 && clearance <= 180);
  TEST_ASSERT(clearance - 136 >= 6);
  signs_t rising = {0};
  agent_session_view_t many[5];
  for (int i = 0; i < 5; i++)
    session(&many[i], i, AGENT_STATE_DONE, "claude");
  sign_input_t grow = base_input(many, 5);
  grow.animations = SIGN_ANIM_REDUCED;
  grow.cat_y = clearance;
  grow.open = true;
  double peak = 0;
  for (int64_t time = 0; time <= 500; time += 7) {
    grow.now_ms = time;
    signs_frame(&rising, &grow, &frame);
    TEST_ASSERT(frame.bounds_y >= 0);
    check_bounds(&frame);
    peak = fmax(peak, pole_of(&frame)->h);
  }
  TEST_ASSERT(peak > 256);
  grow.now_ms = 800;
  signs_frame(&rising, &grow, &frame);
  near(pole_of(&frame)->h, 256);
  const sign_shape_t *cap = NULL;
  for (int i = 0; i < frame.shape_count; i++)
    if (frame.shapes[i].w == 10 && frame.shapes[i].h == 10)
      cap = &frame.shapes[i];
  TEST_ASSERT(cap);
  near(cap->y, grow.cat_y + grow.cat_height - 262);
  near(cap->stroke, 2);
  TEST_ASSERT((cap->fill & 0xffffff) == 0xf8fafc);
  TEST_ASSERT((cap->outline & 0xffffff) == 0x111827);
  sign_input_t pad = base_input(&one, 1);
  signs_frame(&model, &pad, &frame);
  TEST_ASSERT(frame.has_pad);
  TEST_ASSERT(frame.pad.x == 30 && frame.pad.y == 10);
  TEST_ASSERT(frame.pad.w == 440 && frame.pad.h == 270);
}
static const sign_shape_t *fan_plate(const sign_frame_t *frame,
                                     double rotation) {
  const sign_shape_t *found = NULL;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (shape->w > 20 && fabs(shape->rotation - rotation) < 0.05)
      found = shape;
  }
  TEST_ASSERT(found);
  return found;
}
static void test_fan(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t sessions[4];
  session(&sessions[0], 0, AGENT_STATE_WORKING, "claude");
  session(&sessions[1], 1, AGENT_STATE_WORKING, "codex");
  session(&sessions[2], 2, AGENT_STATE_WAITING, "claude");
  session(&sessions[3], 3, AGENT_STATE_IDLE, "claude");
  sign_input_t in = base_input(sessions, 4);
  in.style = SIGN_STYLE_FAN;
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.hit_count == 3);
  // Targets are plate-sized. A box around a whole tilted sign reaches under
  // its neighbours' plates and steals their hover.
  for (int i = 0; i < frame.hit_count; i++) {
    TEST_ASSERT(frame.hits[i].w <= 50 && frame.hits[i].h <= 50);
    for (int other = 0; other < frame.hit_count; other++) {
      double cx = frame.hits[other].x + frame.hits[other].w / 2.0;
      double cy = frame.hits[other].y + frame.hits[other].h / 2.0;
      double own_x = frame.hits[i].x + frame.hits[i].w / 2.0;
      double own_y = frame.hits[i].y + frame.hits[i].h / 2.0;
      TEST_ASSERT(other == i ||
                  (cx - own_x) * (cx - own_x) + (cy - own_y) * (cy - own_y) >
                      100);
    }
  }
  const sign_shape_t *left = fan_plate(&frame, -15);
  const sign_shape_t *middle = fan_plate(&frame, 0);
  const sign_shape_t *right = fan_plate(&frame, 15);
  near(left->w, 34);
  near(left->h, 27);
  near(left->radius, 9);
  near(left->origin_x, 208);
  near(left->origin_y, 242);
  TEST_ASSERT(left->orbit && left->stroke == 2);
  near(middle->w, 30);
  near(middle->radius, 15);
  near(right->y, 132);
  bool stick = false;
  for (int i = 0; i < frame.shape_count; i++) {
    const sign_shape_t *shape = &frame.shapes[i];
    if (fabs(shape->w - 5) < 0.03 && fabs(shape->stroke - 1.5) < 0.03 &&
        (shape->fill & 0xffffff) == 0xf8fafc)
      stick = true;
  }
  TEST_ASSERT(stick);
  TEST_ASSERT(frame.text_count == 1 && frame.texts[0].above);
  TEST_ASSERT(strstr(frame.texts[0].meta, "等你批准"));
  near(frame.texts[0].tag_scale, 1);
  for (int i = 0; i < frame.hit_count; i++)
    TEST_ASSERT(frame.hits[i].key != 4);
  in.has_hover = true;
  in.hover_key = 1;
  signs_frame(&model, &in, &frame);
  near(fan_plate(&frame, -15)->w, 34 * 1.16);
  in.count = 3;
  in.has_hover = false;
  in.open = true;
  signs_frame(&model, &in, &frame);
  fan_plate(&frame, -22);
  fan_plate(&frame, 22);
  TEST_ASSERT(frame.hit_count == 3);
  TEST_ASSERT(frame.pad.x == 56 && frame.pad.y == 86);
  TEST_ASSERT(frame.pad.w == 288 && frame.pad.h == 194);
  int clearance = sign_clearance(SIGN_STYLE_FAN, 110);
  TEST_ASSERT(clearance == 142);
  TEST_ASSERT(clearance - 136 >= 6);
  TEST_ASSERT(sign_clearance(SIGN_STYLE_FAN, 0) == 0);
  signs_t rising = {0};
  agent_session_view_t many[5];
  for (int i = 0; i < 5; i++)
    session(&many[i], i, AGENT_STATE_WAITING, "claude");
  sign_input_t grow = base_input(many, 5);
  grow.style = SIGN_STYLE_FAN;
  grow.animations = SIGN_ANIM_REDUCED;
  grow.open = true;
  grow.cat_y = clearance;
  for (int64_t time = 0; time <= 700; time += 7) {
    grow.now_ms = time;
    signs_frame(&rising, &grow, &frame);
    TEST_ASSERT(frame.bounds_y >= 0);
    check_bounds(&frame);
  }
}
static const sign_shape_t *desk_board(const sign_frame_t *frame) {
  const sign_shape_t *found = NULL;
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (fabs(shape->w - 164) < 0.05 && fabs(shape->h - 26) < 0.05)
      found = shape;
  }
  TEST_ASSERT(found);
  return found;
}
static void test_desk(void) {
  signs_t model = {0};
  sign_frame_t frame;
  agent_session_view_t pair[2];
  session(&pair[0], 0, AGENT_STATE_WORKING, "claude");
  session(&pair[1], 1, AGENT_STATE_WORKING, "codex");
  sign_input_t in = base_input(pair, 2);
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&model, &in, &frame);
  double upper = 0;
  for (int i = 0; i < frame.hit_count; i++)
    if (frame.hits[i].key == pair[0].key)
      upper = frame.hits[i].y;
  in.typing = true;
  in.typing_key = pair[1].key;
  in.desk_name[0] = 'A';
  in.desk_name[1] = '\0';
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.cat_lift == 8);
  TEST_ASSERT(frame.hit_count == 1 && frame.hits[0].key == pair[0].key);
  near(frame.hits[0].y, upper - 8);
  const sign_shape_t *board = desk_board(&frame);
  near(board->y, 170 + 68);
  near(board->radius, 8);
  TEST_ASSERT(board->stroke == 2);
  TEST_ASSERT((board->fill & 0xffffff) == 0xf8fafc);
  TEST_ASSERT(frame.text_count >= 1 && frame.texts[frame.text_count - 1].caret);
  near(frame.texts[frame.text_count - 1].px, 12);
  in.style = SIGN_STYLE_FAN;
  in.typing = false;
  signs_t fan = {0};
  signs_frame(&fan, &in, &frame);
  fan_plate(&frame, 7.5);
  in.typing = true;
  in.typing_key = pair[0].key;
  signs_frame(&fan, &in, &frame);
  fan_plate(&frame, 7.5);
  TEST_ASSERT(frame.hit_count == 1 && frame.hits[0].key == pair[1].key);
  pair[0].state = AGENT_STATE_DONE;
  in.count = 1;
  in.style = SIGN_STYLE_POST;
  in.animations = SIGN_ANIM_FULL;
  in.now_ms = 10000;
  in.typing_until = 20000;
  signs_t blink = {0};
  signs_frame(&blink, &in, &frame);
  TEST_ASSERT(frame.next_frame_ms == 0);
  in.now_ms = 12000;
  signs_frame(&blink, &in, &frame);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 12500);
  in.animations = SIGN_ANIM_REDUCED;
  signs_frame(&blink, &in, &frame);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 20000);
  in.now_ms = 20000;
  signs_frame(&blink, &in, &frame);
  TEST_ASSERT(!frame.animating);
  in.animations = SIGN_ANIM_OFF;
  signs_frame(&blink, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.cat_lift == 8);
}
static void options(void) {
  agent_session_view_t item;
  session(&item, 0, AGENT_STATE_WAITING, "claude");
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    signs_t model = {0};
    sign_frame_t frame;
    sign_input_t in = base_input(&item, 1);
    in.style = (sign_style_t)style;
    in.english = true;
    for (int size = 10; size <= 20; size++) {
      in.font_size = size;
      signs_frame(&model, &in, &frame);
      TEST_ASSERT(frame.text_count == 1);
      near(frame.texts[0].px, size);
      near(frame.texts[0].meta_px, size * 11.5 / 13);
      TEST_ASSERT(strstr(frame.texts[0].meta, "Needs approval"));
    }
    in.english = false;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(strstr(frame.texts[0].meta, "等你批准"));
    item.state = AGENT_STATE_IDLE;
    in.idle = SIGN_IDLE_ALWAYS;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.hit_count == 1);
    in.idle = SIGN_IDLE_NEVER;
    in.open = true;
    signs_frame(&model, &in, &frame);
    TEST_ASSERT(frame.hit_count == 0);
    item.state = AGENT_STATE_WAITING;
  }
}
static const sign_shape_t *shape_box(const sign_frame_t *frame, double x,
                                     double y, double w, double h) {
  for (int i = 0; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (fabs(shape->x - x) < 0.05 && fabs(shape->y - y) < 0.05 &&
        fabs(shape->w - w) < 0.05 && fabs(shape->h - h) < 0.05)
      return shape;
  }
  return NULL;
}
static const sign_text_t *text_value(const sign_frame_t *frame,
                                     const char *value) {
  for (int i = 0; i < frame->text_count; i++)
    if (!strcmp(frame->texts[i].value, value))
      return &frame->texts[i];
  return NULL;
}
static void test_menu(void) {
  signs_t model = {0};
  sign_frame_t frame;
  sign_input_t in = base_input(NULL, 0);
  in.style = SIGN_STYLE_FAN;
  in.animations = SIGN_ANIM_OFF;
  in.menu = true;
  in.now_ms = 1000;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.menu_open && !frame.animating && frame.next_frame_ms == 0);
  TEST_ASSERT(frame.menu_card.x == 122 && frame.menu_card.y == 34);
  TEST_ASSERT(frame.menu_card.w == 154 && frame.menu_card.h == 130);
  TEST_ASSERT(frame.menu_card.y + frame.menu_card.h == 170 - 6);
  const sign_shape_t *card = shape_box(&frame, 122, 34, 154, 130);
  TEST_ASSERT(card && card->above && card->radius == 14 && card->stroke == 2);
  TEST_ASSERT((card->fill & 0xffffff) == 0xf8fafc);
  TEST_ASSERT((card->outline & 0xffffff) == 0x111827);
  const sign_shape_t *holder = shape_box(&frame, 197, 164, 5, 54);
  TEST_ASSERT(holder && !holder->above && holder->stroke == 1.5 &&
              holder->radius == 2.5);
  TEST_ASSERT(frame.menu_style[0].x == 134 && frame.menu_style[0].y == 46);
  TEST_ASSERT(frame.menu_style[0].w == 65 && frame.menu_style[0].h == 30);
  TEST_ASSERT(frame.menu_style[1].x == 199 && frame.menu_style[1].w == 65);
  TEST_ASSERT(frame.menu_style_thumb.x == 138 &&
              frame.menu_style_thumb.y == 50);
  TEST_ASSERT(frame.menu_style_thumb.w == 61 && frame.menu_style_thumb.h == 22);
  const sign_shape_t *thumb = shape_box(&frame, 138, 50, 61, 22);
  TEST_ASSERT(thumb && thumb->above && (thumb->fill & 0xffffff) == 0x111827);
  TEST_ASSERT(frame.menu_lang_thumb.x == 138 && frame.menu_lang[0].y == 84);
  TEST_ASSERT(frame.menu_lang_thumb.y == 88);
  TEST_ASSERT(frame.menu_font.x == 134 && frame.menu_font.y == 122);
  TEST_ASSERT(frame.menu_font.w == 130 && frame.menu_font.h == 30);
  TEST_ASSERT(frame.menu_font_prev.x == 134 && frame.menu_font_prev.w == 26);
  TEST_ASSERT(frame.menu_font_next.x == 238 && frame.menu_font_next.w == 26);
  TEST_ASSERT(!shape_box(&frame, 160, 124, 78, 26));
  in.menu_font_hot = true;
  signs_frame(&model, &in, &frame);
  const sign_shape_t *hot = shape_box(&frame, 160, 124, 78, 26);
  TEST_ASSERT(hot && hot->stroke == 0 && (hot->fill & 0xffffff) == 0xe3e8f0);
  in.menu_font_hot = false;
  signs_frame(&model, &in, &frame);
  const sign_text_t *def = text_value(&frame, "默认");
  TEST_ASSERT(def && def->center && !def->family[0] && def->slide == 0);
  const sign_text_t *zh = text_value(&frame, "中");
  const sign_text_t *en = text_value(&frame, "EN");
  TEST_ASSERT(zh && en && zh->center && zh->above && en->center);
  TEST_ASSERT((zh->color & 0xffffff) == 0xf8fafc);
  TEST_ASSERT((en->color & 0xffffff) == 0x111827);
  TEST_ASSERT(shape_box(&frame, 0, 0, 0, 0) == NULL);
  int tilted = 0;
  for (int i = 0; i < frame.shape_count; i++)
    if (fabs(frame.shapes[i].rotation) == 28)
      tilted++;
  TEST_ASSERT(tilted == 2);
  // Arrows point outward. Rotation is clockwise on screen, so the upper
  // stroke of the left arrow leans like "/" (positive) and the right one
  // like "\" (negative). They were mirrored, pointing at the font name.
  int left_up = 0, right_up = 0;
  for (int i = 0; i < frame.shape_count; i++) {
    const sign_shape_t *stroke = &frame.shapes[i];
    double mid_x = stroke->x + stroke->w / 2, mid_y = stroke->y + stroke->h / 2;
    if (stroke->w > 3 || mid_y >= 137 || mid_y < 122 || stroke->rotation == 0)
      continue;
    if (mid_x < 160 && stroke->rotation > 0)
      left_up++;
    if (mid_x > 238 && stroke->rotation < 0)
      right_up++;
  }
  TEST_ASSERT(left_up == 1 && right_up == 1);
  in.menu_post = true;
  in.menu_english = true;
  in.menu_tap = 2;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.menu_style_thumb.x == 199 &&
              frame.menu_lang_thumb.x == 199);
  TEST_ASSERT(frame.menu_paw == 2);
  zh = text_value(&frame, "中");
  en = text_value(&frame, "EN");
  TEST_ASSERT((zh->color & 0xffffff) == 0x111827);
  TEST_ASSERT((en->color & 0xffffff) == 0xf8fafc);
  in.menu_tap = 0;
  signs_frame(&model, &in, &frame);
  TEST_ASSERT(frame.menu_paw == 0);

  signs_t moving = {0};
  in.menu_post = false;
  in.menu_english = false;
  in.animations = SIGN_ANIM_FULL;
  in.now_ms = 0;
  signs_frame(&moving, &in, &frame);
  TEST_ASSERT(frame.animating && frame.next_frame_ms == 0);
  in.now_ms = 1000;
  signs_frame(&moving, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.next_frame_ms == 0);
  TEST_ASSERT(frame.menu_open && frame.menu_style_thumb.x == 138);
  in.animations = SIGN_ANIM_REDUCED;
  in.menu_post = true;
  in.now_ms = 1000;
  signs_frame(&moving, &in, &frame);
  TEST_ASSERT(frame.animating);
  TEST_ASSERT(frame.menu_style_thumb.x == 138);
  in.now_ms = 1280;
  signs_frame(&moving, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.next_frame_ms == 0);
  TEST_ASSERT(frame.menu_style_thumb.x == 199);
  signs_t instant = {0};
  in.animations = SIGN_ANIM_OFF;
  in.now_ms = 0;
  signs_frame(&instant, &in, &frame);
  TEST_ASSERT(!frame.animating && frame.menu_style_thumb.x == 199);

  agent_session_view_t item;
  session(&item, 0, AGENT_STATE_WAITING, "claude");
  sign_input_t posted = base_input(&item, 1);
  posted.animations = SIGN_ANIM_OFF;
  posted.menu = true;
  signs_t hidden = {0};
  signs_frame(&hidden, &posted, &frame);
  TEST_ASSERT(frame.hit_count == 0 && frame.menu_open);
  posted.style = SIGN_STYLE_OFF;
  signs_frame(&hidden, &posted, &frame);
  TEST_ASSERT(!frame.menu_open && frame.shape_count == 0);

  signs_t fonts = {0};
  in = base_input(NULL, 0);
  in.style = SIGN_STYLE_FAN;
  in.animations = SIGN_ANIM_OFF;
  in.menu = true;
  in.now_ms = 1000;
  signs_frame(&fonts, &in, &frame);
  in.animations = SIGN_ANIM_FULL;
  in.menu_font_dir = 1;
  snprintf(in.menu_font, sizeof(in.menu_font), "Noto Sans");
  in.now_ms = 2000;
  signs_frame(&fonts, &in, &frame);
  const sign_text_t *named = text_value(&frame, "Noto Sans");
  TEST_ASSERT(named && !strcmp(named->family, "Noto Sans"));
  near(named->slide, 14);
  in.menu_font_dir = -1;
  snprintf(in.menu_font, sizeof(in.menu_font), "DejaVu Sans");
  in.now_ms = 3000;
  signs_frame(&fonts, &in, &frame);
  named = text_value(&frame, "DejaVu Sans");
  TEST_ASSERT(named && !strcmp(named->family, "DejaVu Sans"));
  near(named->slide, -14);
  in.now_ms = 3200;
  in.menu_font_dir = 0;
  signs_frame(&fonts, &in, &frame);
  named = text_value(&frame, "DejaVu Sans");
  TEST_ASSERT(named);
  near(named->slide, 0);
}
int main(void) {
  options();
  test_menu();
  test_lifecycle();
  test_rows_and_direction();
  test_easing_hover_and_press();
  test_icons_text_and_loops();
  test_failure_scale_and_clearance();
  test_fan();
  test_desk();
  return 0;
}

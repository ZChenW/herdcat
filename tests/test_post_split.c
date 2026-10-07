#include "graphics/post_text_layout.h"
#include "graphics/sign_draw.h"
#include "graphics/sign_palette.h"
#include "graphics/text.h"
#include "platform/overlay_geometry.h"
#include "signs_nanosvg.h"
#include "test_helpers.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void near(double a, double b) {
  TEST_ASSERT(fabs(a - b) < 1e-8);
}

static sign_input_t input(agent_session_view_t *session) {
  return (sign_input_t){.sessions = session,
                        .count = 1,
                        .style = SIGN_STYLE_POST,
                        .animations = SIGN_ANIM_OFF,
                        .idle = SIGN_IDLE_ALWAYS,
                        .name = SIGN_NAME_PROJECT,
                        .name_extra = SIGN_EXTRA_INLINE,
                        .english = true,
                        .now_ms = 100000,
                        .cat_x = 300,
                        .cat_y = 400,
                        .cat_height = 110,
                        .surface_width = 1000,
                        .surface_height = 800};
}

static sign_frame_t frame_for(const sign_input_t *in) {
  signs_t model = {0};
  sign_frame_t frame;
  signs_frame(&model, in, &frame);
  return frame;
}

static const sign_shape_t *pill(const sign_frame_t *frame) {
  for (int i = 2; i < frame->shape_count; i++) {
    const sign_shape_t *shape = &frame->shapes[i];
    if (shape->h == 26 && shape->radius == 13 && shape->stroke == 2)
      return shape;
  }
  TEST_ASSERT(false);
  return NULL;
}

static void geometry_and_shared_ink(void) {
  const char *agents[] = {"claude", "codex", "custom"};
  for (int agent = 0; agent < 3; agent++)
    for (int theme = 0; theme < 2; theme++)
      for (int state = 0; state < AGENT_STATE_COUNT; state++)
        for (int side = 1; side <= 2; side++) {
          agent_session_view_t session = {
              .key = 1,
              .order = (uint64_t)side,
              .state = (agent_state_t)state,
              .name = "repo",
              .title = "Fix the layout",
              .unread = state >= AGENT_STATE_DONE,
              .child_count = state == AGENT_STATE_IDLE ? 0 : 12};
          strcpy(session.agent, agents[agent]);
          sign_input_t in = input(&session);
          in.theme = (sign_theme_t)theme;
          const sign_palette_t *palette = sign_palette(in.theme);
          bool urgent =
              state == AGENT_STATE_WAITING || state == AGENT_STATE_ERROR;
          sign_frame_t compact = frame_for(&in);
          const sign_shape_t *face = &compact.shapes[2];
          near(face->w, agent == 1 ? 30 : 34);
          near(face->h, agent == 1 ? 30 : 27);
          near(face->radius, agent == 1 ? 15 : 9);
          TEST_ASSERT(face->kind == (agent == 2 ? SIGN_CUT : SIGN_RECT));
          TEST_ASSERT(face->fill == palette->fills[state]);
          TEST_ASSERT(face->outline == palette->ink && face->stroke == 2);
          if (!urgent) {
            TEST_ASSERT(compact.text_count == 0);
            TEST_ASSERT(compact.hits[0].w == (int)face->w);
          }
          in.open = true;
          sign_frame_t expanded = frame_for(&in);
          face = &expanded.shapes[2];
          const sign_shape_t *name = pill(&expanded);
          near(face->y + face->h / 2, name->y + name->h / 2);
          near(side == 1 ? name->x - face->x - face->w
                         : face->x - name->x - name->w,
               6);
          TEST_ASSERT(name->fill == palette->paper);
          TEST_ASSERT(name->outline == palette->ink);
          TEST_ASSERT(expanded.text_count == 1);
          const sign_text_t *text = &expanded.texts[0];
          TEST_ASSERT(text->meta_color == palette->secondary);
          TEST_ASSERT(text->secondary_color == palette->secondary);
          TEST_ASSERT(text->reverse == (side == 2));
          TEST_ASSERT(!strcmp(text->value, "repo"));
          TEST_ASSERT(!strcmp(text->extra, "Fix the layout"));
          TEST_ASSERT(strstr(text->meta, "Claude") ||
                      strstr(text->meta, "Codex") ||
                      strstr(text->meta, "Custom"));
          // The state face, unread dot and icons are identical to an unrotated
          // front fan group, after translation. No post count badge is drawn.
          in.style = SIGN_STYLE_FAN;
          in.has_hover = false;
          sign_frame_t fan = frame_for(&in);
          int shared = expanded.shape_count - 3;
          TEST_ASSERT(shared >= 2);
          for (int i = 0; i < shared; i++) {
            sign_shape_t a = expanded.shapes[2 + i];
            sign_shape_t b = fan.shapes[1 + i];
            near(a.x - face->x, b.x - fan.shapes[1].x);
            near(a.y - face->y, b.y - fan.shapes[1].y);
            near(a.w, b.w);
            near(a.h, b.h);
            near(a.radius, b.radius);
            TEST_ASSERT(a.kind == b.kind && a.fill == b.fill &&
                        a.outline == b.outline);
          }
          for (int i = 0; i < expanded.shape_count; i++)
            TEST_ASSERT(expanded.shapes[i].kind != SIGN_BADGE);
          if (session.unread) {
            const sign_shape_t *dot = &expanded.shapes[3];
            near(dot->x, face->x + face->w - 4);
            near(dot->y, face->y - 5);
            near(dot->w, 9);
          }
        }
}

static void target(const sign_frame_t *frame, double x, double y) {
  sign_hit_t hit;
  TEST_ASSERT(signs_hit(frame, x, y, &hit));
  TEST_ASSERT(hit.key == 1);
}

static void displacement_and_hits(void) {
  for (int side = 1; side <= 2; side++)
    for (int below = 0; below < 2; below++) {
      agent_session_view_t session = {.key = 1,
                                      .order = (uint64_t)side,
                                      .agent = "claude",
                                      .name = "repo",
                                      .state = AGENT_STATE_WAITING};
      sign_input_t in = input(&session);
      in.orientation = (sign_orientation_t)below;
      sign_frame_t rest = frame_for(&in);
      const sign_shape_t *name = pill(&rest), *face = &rest.shapes[2];
      double cy = face->y + face->h / 2;
      target(&rest, face->x + face->w / 2, cy);
      target(&rest, name->x + name->w / 2, cy);
      target(&rest, side == 1 ? face->x + face->w + 3 : name->x + name->w + 3,
             cy);
      signs_t model = {0};
      in.animations = SIGN_ANIM_FULL;
      // Settle all scalar transitions at the same loop boundary as rest.
      in.now_ms = 99000;
      sign_frame_t frame;
      signs_frame(&model, &in, &frame);
      in.now_ms = 100500;  // 1500ms boundary: no waiting displacement.
      signs_frame(&model, &in, &frame);
      rest = frame;
      in.now_ms += 750;
      signs_frame(&model, &in, &frame);
      double dx = side == 1 ? 6 : -6;
      for (int i = 2; i < frame.shape_count; i++) {
        near(frame.shapes[i].x - rest.shapes[i].x, dx);
        near(frame.shapes[i].y, rest.shapes[i].y);
      }
      near(frame.texts[0].x - rest.texts[0].x, dx);
      TEST_ASSERT(frame.texts[0].pixel_snap);
      target(&frame, rest.shapes[2].x + (side == 1 ? .1 : 33.9),
             rest.shapes[2].y + 13.5);
      // Hover and focus-failure shakes also translate every row element.
      session.state = AGENT_STATE_ERROR;
      in.animations = SIGN_ANIM_OFF;
      in.has_hover = false;
      signs_frame(&model, &in, &rest);
      in.has_hover = true;
      in.hover_key = 1;
      signs_frame(&model, &in, &frame);
      for (int i = 2; i < frame.shape_count; i++)
        near(frame.shapes[i].x - rest.shapes[i].x, side == 1 ? 5 : -5);
      near(frame.texts[0].x - rest.texts[0].x, side == 1 ? 5 : -5);
      in.has_hover = false;
      in.animations = SIGN_ANIM_REDUCED;
      signs_focus_failed(&model, 1, in.now_ms);
      in.now_ms += 29;
      signs_frame(&model, &in, &frame);
      // Finish the hover retreat before testing an isolated shake.
      in.now_ms += 400;
      signs_frame(&model, &in, &rest);
      signs_focus_failed(&model, 1, in.now_ms);
      in.now_ms += 29;
      signs_frame(&model, &in, &frame);
      dx = sin(29.0 / 350 * 6 * acos(-1)) * 5 * (1 - 29.0 / 350);
      if (side == 2)
        dx = -dx;
      for (int i = 2; i < frame.shape_count; i++)
        near(frame.shapes[i].x - rest.shapes[i].x, dx);
      near(frame.texts[0].x - rest.texts[0].x, dx);
      in.animations = SIGN_ANIM_OFF;
      in.has_pressed = true;
      in.pressed_key = 1;
      signs_frame(&model, &in, &frame);
      for (int i = 2; i < frame.shape_count; i++) {
        unsigned alpha = frame.shapes[i].fill >> 24;
        if (alpha)
          TEST_ASSERT(alpha == 217);
      }
      TEST_ASSERT(frame.texts[0].color >> 24 == 217);
    }
}

static void retraction(void) {
  agent_session_view_t session = {.key = 1,
                                  .order = 1,
                                  .agent = "claude",
                                  .name = "repo",
                                  .state = AGENT_STATE_DONE};
  sign_input_t in = input(&session);
  in.open = true;
  signs_t model = {0};
  sign_frame_t full, mid, end;
  signs_frame(&model, &in, &full);
  in.open = false;
  in.animations = SIGN_ANIM_REDUCED;
  signs_frame(&model, &in, &mid);
  in.now_ms += 100;
  signs_frame(&model, &in, &mid);
  TEST_ASSERT(mid.transitioning && mid.hits[0].w > 34);
  TEST_ASSERT(mid.hits[0].w < full.hits[0].w);
  TEST_ASSERT(pill(&mid)->fill >> 24 < 255);
  near(mid.shapes[2].w, 34);
  TEST_ASSERT(mid.shapes[2].fill >> 24 == 255);
  in.now_ms += 400;
  signs_frame(&model, &in, &end);
  TEST_ASSERT(!end.animating && end.hits[0].w == 34);
  TEST_ASSERT(end.text_count == 0 && end.shape_count == 4);
  for (int state = AGENT_STATE_WAITING; state < AGENT_STATE_COUNT; state++) {
    session.state = (agent_state_t)state;
    signs_frame(&model, &in, &end);
    in.now_ms += 500;
    signs_frame(&model, &in, &end);
    TEST_ASSERT((end.text_count == 1) == (state != AGENT_STATE_DONE));
  }
}

static void ten_rows_and_edges(void) {
  agent_session_view_t sessions[10] = {0};
  for (int i = 0; i < 10; i++) {
    sessions[i] =
        (agent_session_view_t){.key = (uint64_t)i + 1,
                               .order = (uint64_t)i + 1,
                               .state = (agent_state_t)(i % 5),
                               .unread = true,
                               .name = "a very long project name",
                               .title = "a supplement that must yield"};
    strcpy(sessions[i].agent, i % 3 == 0   ? "claude"
                              : i % 3 == 1 ? "codex"
                                           : "custom");
  }
  for (int height = 40; height <= 220; height += 30)
    for (int below = 0; below < 2; below++)
      for (int n = 1; n <= 10; n++) {
        int clear = sign_clearance(SIGN_STYLE_POST, height, n);
        sign_input_t in = input(sessions);
        in.count = (size_t)n;
        in.cat_height = height;
        in.cat_y = below ? 8.0 * height / 110 : clear + 6;
        in.surface_height = clear + height + 10;
        in.orientation = (sign_orientation_t)below;
        in.open = true;
        signs_t model = {0};
        in.animations = SIGN_ANIM_FULL;
        for (int t = 0; t <= 1400; t += 17) {
          in.now_ms = t;
          if (t == 680) {
            in.has_hover = true;
            in.hover_key = 3;
            signs_focus_failed(&model, 3, t);
          }
          sign_frame_t frame;
          signs_frame(&model, &in, &frame);
          TEST_ASSERT(frame.bounds_y >= 0);
          TEST_ASSERT(frame.bounds_y + frame.bounds_h <= in.surface_height);
          TEST_ASSERT(frame.hit_count == (t ? n : 0));
        }
      }
  sign_input_t in = input(sessions);
  in.count = 10;
  in.open = true;
  config_t config = {.sign_style = SIGN_STYLE_POST, .cat_height = 110};
  overlay_extent_t extent = overlay_extent(&config, 2560);
  for (int side = 0; side < 2; side++)
    for (int width = 200; width < 900; width += 31) {
      in.surface_width = width;
      in.cat_x = side ? width - 198 : 0;
      sign_frame_t frame = frame_for(&in);
      for (int i = 0; i < frame.hit_count; i++) {
        TEST_ASSERT(frame.hits[i].x >= 0);
        TEST_ASSERT(frame.hits[i].x + frame.hits[i].w <= width);
      }
      // Every state and name part stays within its narrower surface, even
      // while the old wider name target retracts at an edge.
      in.animations = SIGN_ANIM_FULL;
      signs_t model = {0};
      sign_frame_t wide;
      in.cat_x = extent.cat_x_in_surface;
      in.surface_width = extent.width;
      in.now_ms = 0;
      signs_frame(&model, &in, &wide);
      in.now_ms = 1000;
      signs_frame(&model, &in, &wide);
      in.surface_width = width;
      in.cat_x = side ? width - 198 : 0;
      for (int t = 1000; t <= 1500; t += 25) {
        in.now_ms = t;
        signs_frame(&model, &in, &frame);
        for (int i = 0; i < frame.hit_count; i++) {
          TEST_ASSERT(frame.hits[i].x >= 0);
          TEST_ASSERT(frame.hits[i].x + frame.hits[i].w <= width);
        }
        for (int i = 2; i < frame.shape_count; i++) {
          TEST_ASSERT(frame.shapes[i].x >= 0);
          TEST_ASSERT(frame.shapes[i].x + frame.shapes[i].w <= width);
        }
      }
      in.animations = SIGN_ANIM_OFF;
    }
}

static void fitting(void) {
  agent_session_view_t session = {.key = 1,
                                  .agent = "claude",
                                  .name = "repo",
                                  .title = "A long supplementary title",
                                  .state = AGENT_STATE_WAITING};
  sign_input_t in = input(&session);
  in.open = true;
  for (int side = 1; side <= 2; side++) {
    session.order = (uint64_t)side;
    sign_frame_t frame = frame_for(&in);
    const sign_text_t *text = &frame.texts[0];
    post_text_layout_t layout;
    post_text_layout(text, &layout);
    double name_w = text_measure(text->value, 13, true);
    if (side == 1)
      near(layout.name_x, text->x);
    else
      near(layout.name_x + name_w, text->x + text->w);
    sign_text_t limited = *text;
    double meta_w = text_measure(text->meta, 11.5, false);
    limited.w = name_w + meta_w + 2 * text->gap + 24;
    post_text_layout(&limited, &layout);
    TEST_ASSERT(layout.extra[0] && strstr(layout.extra, "…"));
    TEST_ASSERT(layout.extra_width <= 24);
    near(layout.name_budget, name_w + text->gap + 24);
    limited.w -= .01;
    post_text_layout(&limited, &layout);
    TEST_ASSERT(!layout.extra[0]);
    limited.w = name_w + meta_w + text->gap - 1;
    post_text_layout(&limited, &layout);
    TEST_ASSERT(!layout.extra[0]);
    near(layout.name_budget, name_w - 1);
  }
}

int main(void) {
  geometry_and_shared_ink();
  displacement_and_hits();
  retraction();
  TEST_ASSERT(text_init("sans") == 0);
  fitting();
  ten_rows_and_edges();
  sign_draw_cleanup();
  text_cleanup();
  puts(
      "Post split geometry, shared fan ink, motion, fitting and edges passed.");
  return 0;
}

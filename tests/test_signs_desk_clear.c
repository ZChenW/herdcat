#include "graphics/signs.h"
#include "test_helpers.h"

#include <math.h>
#include <string.h>

static sign_frame_t frame(signs_t *model, sign_style_t style,
                          sign_orientation_t orientation, bool typing,
                          sign_animations_t animations, int64_t now,
                          double height) {
  agent_session_view_t s = {.key = 1,
                            .pid = 42,
                            .state = AGENT_STATE_WAITING,
                            .agent = "claude",
                            .name = "repo"};
  sign_input_t in = {.sessions = &s,
                     .count = 1,
                     .style = style,
                     .orientation = orientation,
                     .animations = animations,
                     .cat_x = 260,
                     .cat_y = 8,
                     .cat_height = 110,
                     .surface_height = height,
                     .typing = typing,
                     .typing_key = 2,
                     .typing_until = 999999,
                     .now_ms = now};
  sign_frame_t result;
  signs_frame(model, &in, &result);
  return result;
}
static void near(double a, double b) {
  TEST_ASSERT(fabs(a - b) < 1e-8);
}
int main(void) {
  for (int style = SIGN_STYLE_POST; style <= SIGN_STYLE_FAN; style++) {
    signs_t model = {0}, plain = {0};
    sign_frame_t rest =
        frame(&plain, style, SIGN_BELOW, false, SIGN_ANIM_OFF, 1000, 0);
    sign_frame_t typing =
        frame(&model, style, SIGN_BELOW, true, SIGN_ANIM_OFF, 1000, 0);
    // Cat lift is unchanged. Compare against a desk-free model at the lifted
    // cat position by accounting for the 8px cat shift.
    TEST_ASSERT(rest.hit_count == 1 && typing.hit_count == 1);
    TEST_ASSERT(typing.hits[0].y == rest.hits[0].y - 8 + 22);
    if (style == SIGN_STYLE_FAN) {
      near(typing.shapes[0].h, rest.shapes[0].h + 22);
      near(typing.shapes[0].y, rest.shapes[0].y - 8);
    } else {
      near(typing.shapes[0].h, rest.shapes[0].h + 22);
    }
    plain = (signs_t){0};
    model = (signs_t){0};
    sign_frame_t above =
        frame(&plain, style, SIGN_ABOVE, true, SIGN_ANIM_OFF, 1000, 0);
    sign_frame_t bounded =
        frame(&model, style, SIGN_ABOVE, true, SIGN_ANIM_OFF, 1000, 1);
    TEST_ASSERT(memcmp(&above, &bounded, sizeof(above)) == 0);
    // The fade is at an intermediate value; the rod's extra length follows it
    // exactly and returns smoothly on dismissal.
    model = (signs_t){0};
    frame(&model, style, SIGN_BELOW, false, SIGN_ANIM_OFF, 1000, 0);
    frame(&model, style, SIGN_BELOW, true, SIGN_ANIM_REDUCED, 2000, 0);
    sign_frame_t middle =
        frame(&model, style, SIGN_BELOW, true, SIGN_ANIM_REDUCED, 2090, 0);
    TEST_ASSERT(middle.shapes[0].h > rest.shapes[0].h &&
                middle.shapes[0].h < rest.shapes[0].h + 22);
    sign_frame_t full =
        frame(&model, style, SIGN_BELOW, true, SIGN_ANIM_REDUCED, 2400, 0);
    near(full.shapes[0].h, rest.shapes[0].h + 22);
    frame(&model, style, SIGN_BELOW, false, SIGN_ANIM_REDUCED, 3000, 0);
    middle =
        frame(&model, style, SIGN_BELOW, false, SIGN_ANIM_REDUCED, 3090, 0);
    TEST_ASSERT(middle.shapes[0].h > rest.shapes[0].h &&
                middle.shapes[0].h < full.shapes[0].h);
    frame(&model, style, SIGN_BELOW, false, SIGN_ANIM_REDUCED, 3400, 0);
    // Supply only 5px of extra clearance above the lifted base frame's bounds.
    model = (signs_t){0};
    double height = rest.bounds_y + rest.bounds_h - 8 + 5;
    bounded =
        frame(&model, style, SIGN_BELOW, true, SIGN_ANIM_OFF, 4000, height);
    TEST_ASSERT(bounded.bounds_y + bounded.bounds_h <= height);
    TEST_ASSERT(bounded.shapes[0].h < full.shapes[0].h);
  }
  return 0;
}

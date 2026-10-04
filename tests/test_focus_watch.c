#include "platform/focus_watch.h"
#include "test_helpers.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void parsed(const char *line, focus_watch_event_t *event,
                   focus_window_t *windows, size_t capacity) {
  int result = focus_watch_parse(line, strlen(line), event, windows, capacity);
  TEST_ASSERT(result == 1);
}

static void test_events(void) {
  focus_watch_event_t event;
  focus_window_t windows[4];
  parsed("{\"WindowFocusChanged\":{\"id\":7}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_FOCUS && event.id == 7 &&
              !event.id_null);
  parsed("{\"WindowFocusChanged\":{\"id\":null}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_FOCUS && event.id_null);
  parsed("{\"WindowClosed\":{\"id\":7}}", &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_CLOSE && event.id == 7);
  const char *opened =
      "{\"WindowOpenedOrChanged\":{\"window\":{\"id\":7,\"title\":\"say "
      "\\\"hi\\\" "
      "{ok}\",\"app_id\":\"kitty\",\"pid\":42,\"is_focused\":true}}}";
  parsed(opened, &event, NULL, 0);
  TEST_ASSERT(event.kind == FOCUS_WATCH_UPSERT && event.id == 7 &&
              event.pid == 42);
  TEST_ASSERT(event.has_focused && event.focused == 7);
  const char *replaced =
      "{\"WindowsChanged\":{\"windows\":[{\"id\":1,\"title\":\"a {b} "
      "\\\"c\\\"\",\"pid\":9},{\"id\":2,\"pid\":null}]}}";
  parsed(replaced, &event, windows, 4);
  TEST_ASSERT(event.kind == FOCUS_WATCH_WINDOWS && event.count == 1);
  TEST_ASSERT(windows[0].id == 1 && windows[0].pid == 9);
  TEST_ASSERT(!event.has_focused);
  // A new stream starts with the window list and sends no focus event, so the
  // focused window has to be read from the list itself.
  const char *initial =
      "{\"WindowsChanged\":{\"windows\":[{\"id\":3,\"pid\":5,"
      "\"is_focused\":false},{\"id\":4,\"pid\":6,\"is_focused\":true}]}}";
  parsed(initial, &event, windows, 4);
  TEST_ASSERT(event.kind == FOCUS_WATCH_WINDOWS && event.count == 2);
  TEST_ASSERT(event.has_focused && event.focused == 4);
  const char *layout = "{\"KeyboardLayoutSwitched\":{\"idx\":0}}";
  TEST_ASSERT(focus_watch_parse(layout, strlen(layout), &event, NULL, 0) == 0);
  const char *ok = "{\"Ok\":\"Handled\"}";
  TEST_ASSERT(focus_watch_parse(ok, strlen(ok), &event, NULL, 0) == 0);
  const char *broken = "{\"WindowFocusChanged\":{\"id\":";
  TEST_ASSERT(focus_watch_parse(broken, strlen(broken), &event, NULL, 0) == -1);
  TEST_ASSERT(focus_watch_parse("{\"WindowClosed\":{\"id\":1}}", 26, NULL, NULL,
                                0) == -1);
  char *huge = malloc(65537);
  TEST_ASSERT(huge);
  TEST_ASSERT(focus_watch_parse(huge, 65537, &event, NULL, 0) == -1);
  free(huge);
  TEST_ASSERT(
      focus_watch_parse(replaced, strlen(replaced), &event, windows, 0) == -1);
}

static void test_match(void) {
  focus_window_t windows[1] = {
      {.id = 4, .pid = getpid()}
  };
  agent_session_view_t sessions[2] = {0};
  sessions[0].key = 1;
  sessions[0].pid = getpid();
  sessions[0].updated_ms = 10;
  sessions[1].key = 2;
  sessions[1].pid = getpid();
  sessions[1].updated_ms = 20;
  TEST_ASSERT(focus_watch_match(4, windows, 1, sessions, 2) == 2);
  sessions[1].updated_ms = 5;
  TEST_ASSERT(focus_watch_match(4, windows, 1, sessions, 2) == 1);
  TEST_ASSERT(focus_watch_match(9, windows, 1, sessions, 2) == 0);
  uint64_t keys[2] = {0};
  TEST_ASSERT(focus_watch_matching(4, windows, 1, sessions, 2, keys, 2) == 2);
  TEST_ASSERT(keys[0] == 1 && keys[1] == 2);
  TEST_ASSERT(focus_watch_matching(9, windows, 1, sessions, 2, keys, 2) == 0);
  TEST_ASSERT(focus_watch_focused_keys(sessions, 2, keys, 2) == 0);
}

int main(void) {
  test_events();
  test_match();
  TEST_ASSERT(!focus_watch_available());
  TEST_ASSERT(focus_watch_focused_id() == 0);
  TEST_ASSERT(focus_watch_focused_session(NULL, 0) == 0);
  return 0;
}

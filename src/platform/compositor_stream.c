#define _GNU_SOURCE
#include "compositor_internal.h"
#include "platform/agent_watch.h"
#include "platform/command_job.h"
#include "utils/json.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define STREAM_TOKEN 1U
#define QUERY_TOKEN  6U
static int fd = -1;
static bool sway, connecting, enabled, skipping, refresh, active_query;
static bool subscribed, tree_received;
static int backoff;
static int64_t deadline, retry_at;
static command_job_t query = {.fd = -1};
static char incoming[65550], outgoing[256];
static size_t used, sent, queued;
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void compositor_stream_cleanup(void) {
  agent_watch_unlisten(fd);
  agent_watch_unlisten(query.fd);
  if (fd >= 0)
    close(fd);
  fd = -1;
  job_cleanup(&query);
  enabled = connecting = skipping = refresh = active_query = false;
  used = sent = queued = 0;
  subscribed = tree_received = false;
  backoff = 0;
  focus_watch_lost();
}
static void retry(void) {
  int delay = backoff ? (backoff < 16000 ? backoff * 2 : 30000) : 1000;
  compositor_stream_cleanup();
  backoff = delay;
  enabled = true;
  retry_at = now_ms() + delay;
}
static bool watch(uint32_t events) {
  agent_watch_unlisten(fd);
  return agent_watch_listen(fd, STREAM_TOKEN, events) == 0;
}
static bool flush(void) {
  while (sent < queued) {
    ssize_t n = send(fd, outgoing + sent, queued - sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && errno == EAGAIN)
      return watch(EPOLLIN | EPOLLOUT);
    if (n <= 0)
      return false;
    sent += (size_t)n;
  }
  if (!sway || (subscribed && tree_received))
    deadline = 0;
  return watch(EPOLLIN);
}
static bool query_start(bool active) {
  const char *args[] = {"hyprctl", "-j", active ? "activewindow" : "clients",
                        NULL};
  if (job_start(&query, args) < 0)
    return false;
  active_query = active;
  if (agent_watch_listen(query.fd, QUERY_TOKEN, EPOLLIN) < 0) {
    job_cleanup(&query);
    return false;
  }
  return true;
}
static bool connected(void) {
  connecting = false;
  backoff = 0;
  if (sway) {
    queued =
        compositor_sway_message(2, "[\"window\"]", outgoing, sizeof(outgoing));
    queued += compositor_sway_message(4, "", outgoing + queued,
                                      sizeof(outgoing) - queued);
    return flush();
  }
  deadline = 0;
  return watch(EPOLLIN) && query_start(false);
}
static bool open_stream(void) {
  char path[108];
  if (sway) {
    const char *p = getenv("SWAYSOCK");
    if (!p || *p != '/' || strlen(p) >= sizeof(path))
      return false;
    memcpy(path, p, strlen(p) + 1);
  } else if (!compositor_hyprland_path(path, sizeof(path)))
    return false;
  fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return false;
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  memcpy(address.sun_path, path, strlen(path) + 1);
  int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
  connecting = result < 0 && errno == EINPROGRESS;
  deadline = now_ms() + 1000;
  if (result < 0 && !connecting)
    return false;
  return connecting ? watch(EPOLLOUT) : connected();
}
int compositor_stream_connect(bool use_sway) {
  const compositor_ops_t *ops = compositor_selected();
  if (ops != (use_sway ? &COMPOSITOR_SWAY : &COMPOSITOR_HYPRLAND))
    return 0;
  if (enabled)
    return 0;
  sway = use_sway;
  enabled = true;
  if (!open_stream())
    retry();
  return 0;
}
static bool sway_packet(uint32_t type, const char *text, size_t length) {
  focus_watch_event_t event;
  if (type == 2) {
    json_span_t root, ok;
    subscribed = json_document(text, length, &root) &&
                 json_field(root, "success", &ok) && ok.end - ok.p == 4 &&
                 !memcmp(ok.p, "true", 4);
    if (subscribed && tree_received)
      deadline = 0;
    return subscribed;
  }
  if (type == 4) {
    focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
    int count =
        compositor_sway_tree(text, length, windows, FOCUS_WATCH_WINDOW_MAX);
    if (count < 0)
      return false;
    event = (focus_watch_event_t){.kind = FOCUS_WATCH_WINDOWS, .count = count};
    focus_watch_apply(&event, windows);
    if (compositor_sway_event(text, length, &event) < 0)
      return false;
    focus_watch_apply(&event, NULL);
    tree_received = true;
    if (subscribed)
      deadline = 0;
  } else if (type == UINT32_C(0x80000003)) {
    int result = compositor_sway_event(text, length, &event);
    if (result < 0)
      return false;
    if (result > 0)
      focus_watch_apply(&event, NULL);
  }
  return true;
}
static bool consume(const char *chunk, size_t size) {
  if (!sway) {
    for (size_t i = 0; i < size; i++) {
      if (chunk[i] == '\n') {
        focus_watch_event_t event;
        int result =
            skipping ? 0 : compositor_hyprland_event(incoming, used, &event);
        if (result == 1)
          focus_watch_apply(&event, NULL);
        else if (result == 2)
          refresh = true;
        used = 0;
        skipping = false;
      } else if (!skipping) {
        if (used == 65536)
          skipping = true;
        else
          incoming[used++] = chunk[i];
      }
    }
    return true;
  }
  for (size_t i = 0; i < size; i++) {
    if (used == sizeof(incoming))
      return false;
    incoming[used++] = chunk[i];
    uint32_t length, type;
    int result = compositor_sway_header(incoming, used, &length, &type);
    if (result < 0)
      return false;
    if (result > 0 && used == 14 + (size_t)length) {
      if (!sway_packet(type, incoming + 14, length))
        return false;
      used = 0;
    }
  }
  return true;
}
static void read_stream(void) {
  char chunk[2048];
  ssize_t n = -1;
  for (int batch = 0; batch < 32; batch++) {
    n = read(fd, chunk, sizeof(chunk));
    if (n <= 0)
      break;
    if (!consume(chunk, (size_t)n)) {
      retry();
      return;
    }
  }
  if (!n || (n < 0 && errno != EAGAIN && errno != EINTR))
    retry();
}
static void query_poll(void) {
  int saved = query.fd;
  int result = job_process(&query);
  if (!result) {
    if (query.eof)
      agent_watch_unlisten(saved);
    return;
  }
  agent_watch_unlisten(saved);
  if (result < 0) {
    retry();
    return;
  }
  focus_watch_event_t event = {0};
  if (active_query) {
    json_span_t root, address;
    char text[32];
    if (!json_document(query.buffer, query.used, &root)) {
      retry();
      return;
    }
    event.kind = FOCUS_WATCH_FOCUS;
    event.id_null = !json_field(root, "address", &address) ||
                    !json_text(address, text, sizeof(text)) ||
                    !compositor_hyprland_address(text, &event.id) || !event.id;
    focus_watch_apply(&event, NULL);
  } else {
    focus_window_t windows[FOCUS_WATCH_WINDOW_MAX];
    int count = compositor_hyprland_windows(query.buffer, query.used, windows,
                                            FOCUS_WATCH_WINDOW_MAX);
    if (count < 0) {
      retry();
      return;
    }
    event.kind = FOCUS_WATCH_WINDOWS;
    event.count = count;
    focus_watch_apply(&event, windows);
    if (!query_start(true))
      retry();
  }
}
void compositor_stream_events(void) {
  if (!enabled)
    return;
  if (fd < 0) {
    if (now_ms() >= retry_at && !open_stream())
      retry();
    return;
  }
  if (deadline && now_ms() >= deadline) {
    retry();
    return;
  }
  if (query.pid)
    query_poll();
  if (!sway && refresh && !query.pid && fd >= 0) {
    refresh = false;
    if (!query_start(false))
      retry();
  }
}
void compositor_stream_ready(uint32_t token) {
  if (!enabled || fd < 0)
    return;
  if (token == QUERY_TOKEN) {
    if (query.pid)
      query_poll();
    return;
  }
  if (token != STREAM_TOKEN)
    return;
  if (connecting) {
    int error;
    socklen_t length = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error ||
        !connected()) {
      retry();
      return;
    }
  }
  if (sent < queued && !flush()) {
    retry();
    return;
  }
  read_stream();
}
int compositor_stream_timeout(void) {
  if (!enabled)
    return -1;
  int64_t until = fd < 0 ? retry_at : deadline;
  if (query.pid) {
    int64_t job_until = query.eof ? now_ms() + 10 : query.deadline;
    if (!until || job_until < until)
      until = job_until;
  }
  if (!until)
    return refresh ? 0 : -1;
  int64_t left = until - now_ms();
  return left < 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}
bool compositor_stream_available(void) {
  return enabled && fd >= 0 && !connecting &&
         (!sway || (subscribed && tree_received));
}

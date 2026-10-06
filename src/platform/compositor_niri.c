#define _GNU_SOURCE
#include "compositor_internal.h"
#include "platform/agent_watch.h"
#include "platform/compositor.h"
#include "platform/focus_watch.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#define STREAM_TOKEN 1U
#define STREAM_LINE  65536
#define WINDOW_MAX   FOCUS_WATCH_WINDOW_MAX
static int stream_fd = -1;
static bool connecting, disabled;
static int backoff_ms;
static int64_t retry_at;
static char pending[STREAM_LINE];
static size_t pending_used;
static bool skipping;
static void niri_ready(uint32_t token);
static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void schedule_retry(void) {
  if (stream_fd >= 0) {
    agent_watch_unlisten(stream_fd);
    close(stream_fd);
    stream_fd = -1;
  }
  connecting = false;
  focus_watch_lost();
  pending_used = 0;
  skipping = false;
  if (backoff_ms < 1000) {
    backoff_ms = 1000;
  } else if (backoff_ms < 30000) {
    backoff_ms *= 2;
  }
  retry_at = now_ms() + backoff_ms;
}
static void consume_lines(void) {
  char chunk[2048];
  ssize_t count;
  while ((count = read(stream_fd, chunk, sizeof(chunk))) > 0) {
    for (ssize_t i = 0; i < count; i++) {
      char c = chunk[i];
      if (c != '\n') {
        if (pending_used + 1 >= STREAM_LINE) {
          skipping = true;
        } else if (!skipping) {
          pending[pending_used++] = c;
        }
        continue;
      }
      if (!skipping && pending_used) {
        focus_watch_event_t event;
        focus_window_t parsed[WINDOW_MAX];
        int parsed_count = focus_watch_parse(pending, pending_used, &event,
                                             parsed, WINDOW_MAX);
        if (parsed_count > 0) {
          focus_watch_apply(&event, parsed);
        }
      }
      pending_used = 0;
      skipping = false;
    }
  }
  if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
    schedule_retry();
  }
}
static int send_request(int fd) {
  const char request[] = "\"EventStream\"\n";
  size_t sent = 0;
  while (sent < sizeof(request) - 1) {
    ssize_t n =
        send(fd, request + sent, sizeof(request) - 1 - sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && errno == EAGAIN) {
      return 0;
    }
    if (n <= 0) {
      return -1;
    }
    sent += (size_t)n;
  }
  backoff_ms = 1000;
  return 0;
}
static void niri_ready(uint32_t token) {
  if (token != STREAM_TOKEN || stream_fd < 0) {
    return;
  }
  if (connecting) {
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(stream_fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 ||
        error) {
      schedule_retry();
      return;
    }
    connecting = false;
    agent_watch_unlisten(stream_fd);
    if (send_request(stream_fd) < 0 ||
        agent_watch_listen(stream_fd, STREAM_TOKEN, EPOLLIN) < 0) {
      schedule_retry();
      return;
    }
  }
  consume_lines();
}
static int open_stream(void) {
  const char *path = getenv("NIRI_SOCKET");
  if (!path || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
    return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    return -1;
  }
  struct sockaddr_un address = {.sun_family = AF_UNIX};
  memcpy(address.sun_path, path, strlen(path) + 1);
  int result = connect(fd, (struct sockaddr *)&address, sizeof(address));
  connecting = result < 0 && errno == EINPROGRESS;
  if (result < 0 && !connecting) {
    close(fd);
    return -1;
  }
  uint32_t events = connecting ? EPOLLOUT : EPOLLIN;
  if (!connecting && send_request(fd) < 0) {
    close(fd);
    return -1;
  }
  if (agent_watch_listen(fd, STREAM_TOKEN, events) < 0) {
    close(fd);
    return -1;
  }
  stream_fd = fd;
  return 0;
}

static int niri_connect(void) {
  if (agent_watch_fd() < 0 || !focus_available()) {
    disabled = true;
    return 0;
  }
  if (open_stream() < 0) {
    schedule_retry();
  }
  return 0;
}
static void niri_cleanup(void) {
  if (stream_fd >= 0) {
    agent_watch_unlisten(stream_fd);
    close(stream_fd);
    stream_fd = -1;
  }
  connecting = false;
  disabled = false;
  focus_watch_lost();
  pending_used = 0;
  skipping = false;
}
static void niri_events(void) {
  if (disabled || stream_fd >= 0 || now_ms() < retry_at) {
    return;
  }
  if (open_stream() < 0) {
    schedule_retry();
  }
}
static int niri_timeout(void) {
  if (disabled || stream_fd >= 0) {
    return -1;
  }
  int64_t remaining = retry_at - now_ms();
  if (remaining < 1) {
    return 1;
  }
  return remaining > INT_MAX ? INT_MAX : (int)remaining;
}
static bool niri_available(void) {
  return !disabled && stream_fd >= 0 && !connecting;
}

static bool niri_detect(void) {
  const char *socket = getenv("NIRI_SOCKET");
  struct stat st;
  return socket && *socket && stat(socket, &st) == 0 && S_ISSOCK(st.st_mode);
}
static void niri_windows(const char **args) {
  const char *a[] = {"niri", "msg", "-j", "windows", NULL};
  memcpy(args, a, sizeof(a));
}
static bool niri_focus(uint64_t id, const char **args, char *text,
                       size_t size) {
  int n = snprintf(text, size, "%" PRIu64, id);
  if (n < 0 || (size_t)n >= size)
    return false;
  const char *a[] = {"niri", "msg", "action", "focus-window",
                     "--id", text,  NULL};
  memcpy(args, a, sizeof(a));
  return true;
}
const compositor_ops_t COMPOSITOR_NIRI = {.name = "niri",
                                          .detect = niri_detect,
                                          .connect = niri_connect,
                                          .events = niri_events,
                                          .ready = niri_ready,
                                          .timeout = niri_timeout,
                                          .available = niri_available,
                                          .cleanup = niri_cleanup,
                                          .windows = niri_windows,
                                          .parse_windows = focus_parse_windows,
                                          .focus_window = niri_focus};

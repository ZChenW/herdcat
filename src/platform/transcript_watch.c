#define _GNU_SOURCE
#include "platform/transcript_watch.h"

#include "core/agent_adapters.h"
#include "core/agent_quiet.h"
#include "core/agent_sessions.h"
#include "core/agent_state.h"
#include "core/agent_title.h"
#include "core/agent_transcript.h"
#include "platform/agent_watch.h"
#include "platform/command_job.h"
#include "utils/json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>  // IWYU pragma: keep
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TRANSCRIPT_TOKEN 0x7472616eU
#define BACKLOG_TOKEN    0x74726162U
#define READ_BUDGET      (256L * 1024L)

typedef struct {
  uint64_t key, order;
  char agent[9], path[AGENT_TRANSCRIPT_PATH_MAX + 1];
  char session_id[AGENT_SESSION_ID_MAX + 1];
  int fd, wd;
  int64_t submitted_ms;
  off_t offset;
  dev_t device;
  ino_t inode;
  size_t used;
  bool active, pending, skipping, failed;
  char line[AGENT_TRANSCRIPT_LINE_MAX + 1];
} transcript_t;
static transcript_t slots[AGENT_SESSIONS_MAX];
static int notify_fd = -1, backlog_fd = -1;
static bool enabled;
static unsigned cursor;
#define PROMPT_TOKEN 0x70726f6dU
static command_job_t prompt_job = {.fd = -1};
static uint64_t prompt_key, prompt_order;

void transcript_prompt_poll(void) {
  if (prompt_job.pid > 0) {
    int fd = prompt_job.fd;
    int result = job_process(&prompt_job);
    if (!result)
      return;
    agent_watch_unlisten(fd);
    if (result > 0 && prompt_job.used <= AGENT_TITLE_MAX &&
        !memchr(prompt_job.buffer, 0, prompt_job.used))
      agent_sessions_recovered_prompt(prompt_key, prompt_order,
                                      prompt_job.buffer);
    memset(prompt_job.buffer, 0, sizeof(prompt_job.buffer));
  }
  agent_session_record_t row;
  if (!agent_sessions_next_prompt(&row))
    return;
  const char *argv[] = {"/proc/self/exe", "--transcript-prompt", row.agent,
                        row.transcript, NULL};
  if (job_start(&prompt_job, argv) < 0)
    return;
  prompt_key = row.key;
  prompt_order = row.order;
  if (agent_watch_listen(prompt_job.fd, PROMPT_TOKEN, EPOLLIN | EPOLLHUP) < 0)
    job_cleanup(&prompt_job);
}
int transcript_prompt_timeout(int64_t now) {
  if (prompt_job.pid <= 0)
    return -1;
  return prompt_job.deadline <= now ? 0 : (int)(prompt_job.deadline - now);
}

static void disarm(transcript_t *slot) {
  if (!slot->active)
    return;
  slot->active = slot->pending = false;
  bool shared = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    shared |= slots[i].active && slots[i].wd == slot->wd;
  if (!shared && notify_fd >= 0)
    inotify_rm_watch(notify_fd, slot->wd);
  close(slot->fd);
  slot->fd = slot->wd = -1;
  slot->used = 0;
}
static void close_notifiers(void) {
  if (notify_fd >= 0) {
    agent_watch_unlisten(notify_fd);
    close(notify_fd);
    notify_fd = -1;
  }
  if (backlog_fd >= 0) {
    agent_watch_unlisten(backlog_fd);
    close(backlog_fd);
    backlog_fd = -1;
  }
}
static bool open_notifiers(void) {
  if (notify_fd >= 0)
    return true;
  notify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  backlog_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (notify_fd < 0 || backlog_fd < 0 ||
      agent_watch_listen(notify_fd, TRANSCRIPT_TOKEN, EPOLLIN) < 0 ||
      agent_watch_listen(backlog_fd, BACKLOG_TOKEN, EPOLLIN) < 0) {
    close_notifiers();
    return false;
  }
  return true;
}
static void arm(transcript_t *slot) {
  if (slot->active || slot->failed || !slot->path[0])
    return;
  slot->failed = true;
  int fd = transcript_watch_open(slot->path);
  if (fd < 0)
    return;
  if (!open_notifiers()) {
    close(fd);
    return;
  }
  char proc[64];
  snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
  int wd = inotify_add_watch(
      notify_fd, proc, IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
  off_t offset = lseek(fd, 0, SEEK_END);
  struct stat st;
  if (wd < 0 || offset < 0 || fstat(fd, &st) < 0) {
    if (wd >= 0)
      inotify_rm_watch(notify_fd, wd);
    close(fd);
    return;
  }
  char last = '\n';
  if (offset > 0 && pread(fd, &last, 1, offset - 1) != 1)
    last = 'x';
  slot->offset = offset;
  slot->device = st.st_dev;
  slot->inode = st.st_ino;
  slot->skipping = last != '\n';
  slot->used = 0;
  slot->fd = fd;
  slot->wd = wd;
  slot->active = true;
  slot->failed = false;
}
int transcript_watch_count(void) {
  int count = 0;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    count += slots[i].active;
  return count;
}
void transcript_watch_sync(bool on, int64_t now_ms) {
  (void)now_ms;
  enabled = on;
  transcript_prompt_poll();
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
    transcript_t *slot = &slots[i];
    if (!slot->key)
      continue;
    const agent_session_view_t *view = NULL;
    for (int j = 0; j < count; j++)
      if (views[j].key == slot->key && views[j].order == slot->order)
        view = &views[j];
    if (!view) {
      disarm(slot);
      memset(slot, 0, sizeof(*slot));
    } else if (!on || (view->state != AGENT_STATE_WORKING &&
                       view->state != AGENT_STATE_WAITING &&
                       !agent_quiet_stopped(view->key, view->order,
                                            view->updated_ms))) {
      disarm(slot);
      slot->failed = false;
    } else
      arm(slot);
  }
  if (!transcript_watch_count())
    close_notifiers();
}
void transcript_watch_path(uint64_t key, const char *path, int64_t now_ms) {
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  const agent_session_view_t *view = NULL;
  for (int i = 0; i < count; i++)
    if (views[i].key == key)
      view = &views[i];
  if (!key || !view || !path || strlen(path) > AGENT_TRANSCRIPT_PATH_MAX)
    return;
  const agent_adapter_t *adapter = agent_adapter_find(view->agent);
  if (strcmp(adapter->name, view->agent))
    return;
  bool transcript = adapter->interrupt_source == AGENT_SIGNAL_TRANSCRIPT ||
                    adapter->error_source == AGENT_SIGNAL_TRANSCRIPT;
  if (!transcript && strcmp(view->agent, "pi") && strcmp(view->agent, "grok"))
    return;
  size_t length = strlen(path);
  if (!strcmp(view->agent, "agy")) {
    if (length >= 6 && !strcmp(path + length - 6, ".jsonl")) {
      agent_sessions_set_transcript(key, path);
      return;  // Recovery uses JSONL; the independent log watch stays intact.
    }
    if (length < 4 || strcmp(path + length - 4, ".log") ||
        !agent_session_id_valid(view->session_id))
      return;
  } else if (length >= 4 && !strcmp(path + length - 4, ".log"))
    return;
  if (strcmp(view->agent, "agy")) {
    agent_sessions_set_transcript(key, path);
    agent_sessions_refresh_title(key);
  }
  if (!transcript)
    return;
  transcript_t *slot = NULL;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    if (slots[i].key == key)
      slot = &slots[i];
  if (!slot)
    for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
      if (!slots[i].key) {
        slot = &slots[i];
        break;
      }
  if (!slot)
    return;
  // Every path handoff marks a new submission and resets the read baseline.
  disarm(slot);
  memset(slot, 0, sizeof(*slot));
  slot->key = key;
  slot->order = view->order;
  slot->submitted_ms = now_ms;
  memcpy(slot->agent, view->agent, sizeof(slot->agent));
  memcpy(slot->session_id, view->session_id, sizeof(slot->session_id));
  memcpy(slot->path, path, strlen(path) + 1);
  transcript_watch_sync(enabled, now_ms);
}
static void agy_confirmation(transcript_t *slot, int64_t now) {
  agy_confirmation_t event = agent_transcript_agy_confirmation(
      slot->session_id, slot->line, slot->used);
  if (event == AGY_CONFIRMATION_NONE)
    return;
  agent_session_view_t views[AGENT_SESSIONS_MAX];
  int count = agent_sessions_snapshot(views, AGENT_SESSIONS_MAX);
  const agent_session_view_t *current = NULL;
  unsigned working = 0;
  for (int i = 0; i < count; i++) {
    const agent_session_view_t *view = &views[i];
    if (view->key == slot->key && view->order == slot->order)
      current = view;
    if (view->state != AGENT_STATE_WORKING)
      continue;
    for (int j = 0; j < AGENT_SESSIONS_MAX; j++) {
      const transcript_t *other = &slots[j];
      if (other->key == view->key && other->order == view->order &&
          other->device == slot->device && other->inode == slot->inode) {
        working++;
        break;
      }
    }
  }
  if (!current)
    return;
  if (event == AGY_CONFIRMATION_WAITING && working == 1 &&
      current->state == AGENT_STATE_WORKING)
    agent_sessions_waiting(slot->key, now);
  else if (event == AGY_CONFIRMATION_ANSWERED &&
           (current->state == AGENT_STATE_WAITING ||
            current->state == AGENT_STATE_WORKING))
    agent_sessions_working(slot->key, now);
  else if (event == AGY_CONFIRMATION_CANCELLED)
    agent_sessions_interrupt(slot->key, now);
}
static bool feed(transcript_t *slot, const char *data, size_t length,
                 int64_t now) {
  for (size_t i = 0; i < length; i++) {
    if (data[i] == '\n') {
      if (!slot->skipping && !strcmp(slot->agent, "agy"))
        agy_confirmation(slot, now);
      bool hit =
          !slot->skipping &&
          (strcmp(slot->agent, "claude") || now - slot->submitted_ms >= 1000) &&
          (!strcmp(slot->agent, "agy")
               ? agent_transcript_agy_cancelled(slot->session_id, slot->line,
                                                slot->used)
               : agent_transcript_interrupted(slot->agent, slot->line,
                                              slot->used));
      if (!slot->skipping && !strcmp(slot->agent, "claude")) {
        char title[AGENT_TITLE_MAX + 1];
        if (agent_title_line(slot->agent, NULL, slot->line, slot->used, title))
          agent_sessions_set_title(slot->key, title);
        // A new user/assistant record is activity, not a completion signal.
        // Retain event-only recovery after quiet guesses; no idle timer.
        json_span_t doc, type, message, role;
        if (!agent_transcript_interrupted(slot->agent, slot->line,
                                          slot->used) &&
            json_document(slot->line, slot->used, &doc) &&
            json_field(doc, "type", &type) &&
            (json_equal(type, "user") || json_equal(type, "assistant")) &&
            json_field(doc, "message", &message) &&
            json_field(message, "role", &role) &&
            ((json_equal(type, "user") && json_equal(role, "user")) ||
             (json_equal(type, "assistant") && json_equal(role, "assistant"))))
          agent_quiet_record(slot->key, now);
      }
      size_t used = slot->used;
      slot->used = 0;
      slot->skipping = false;
      if (hit) {
        if (agent_transcript_failed(slot->agent, slot->line, used))
          agent_sessions_fail(slot->key, now);
        else
          agent_sessions_interrupt(slot->key, now);
        return true;
      }
    } else if (!slot->skipping) {
      if (slot->used < AGENT_TRANSCRIPT_LINE_MAX)
        slot->line[slot->used++] = data[i];
      else {
        slot->used = 0;
        slot->skipping = true;
      }
    }
  }
  return false;
}
void transcript_watch_ready(uint32_t token, int64_t now_ms) {
  if (token == PROMPT_TOKEN) {
    transcript_prompt_poll();
    return;
  }
  if ((token != TRANSCRIPT_TOKEN && token != BACKLOG_TOKEN) || notify_fd < 0)
    return;
  if (token == BACKLOG_TOKEN) {
    uint64_t value;
    ssize_t ignored = read(backlog_fd, &value, sizeof(value));
    (void)ignored;
  } else {
    // Bounded drain: unread inotify events remain readable in epoll.
    union {
      struct inotify_event align;
      char bytes[8192];
    } events;
    ssize_t n = read(notify_fd, events.bytes, sizeof(events.bytes));
    for (size_t offset = 0;
         n > 0 && offset + sizeof(struct inotify_event) <= (size_t)n;) {
      const struct inotify_event *event = (const void *)(events.bytes + offset);
      for (int i = 0; i < AGENT_SESSIONS_MAX; i++) {
        transcript_t *slot = &slots[i];
        if (!slot->active ||
            (event->wd != slot->wd && !(event->mask & IN_Q_OVERFLOW)))
          continue;
        if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
          disarm(slot);
          slot->failed = true;
        } else
          slot->pending = true;
      }
      offset += sizeof(*event) + event->len;
    }
  }
  size_t budget = READ_BUDGET;
  for (int visited = 0; visited < AGENT_SESSIONS_MAX && budget; visited++) {
    transcript_t *slot = &slots[cursor++ % AGENT_SESSIONS_MAX];
    if (!slot->active || !slot->pending)
      continue;
    struct stat st, named;
    if (fstat(slot->fd, &st) < 0 || st.st_size < slot->offset ||
        lstat(slot->path, &named) < 0 || st.st_dev != named.st_dev ||
        st.st_ino != named.st_ino) {
      disarm(
          slot);  // Truncation/rotation: fail closed until next path handoff.
      slot->failed = true;
      continue;
    }
    while (budget && slot->active && slot->pending) {
      char data[8192];
      size_t want = budget < sizeof(data) ? budget : sizeof(data);
      ssize_t n = pread(slot->fd, data, want, slot->offset);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        slot->pending = false;
        if (n < 0) {
          disarm(slot);
          slot->failed = true;
        }
        break;
      }
      budget -= (size_t)n;
      slot->offset += n;
      if (feed(slot, data, (size_t)n, now_ms))
        disarm(slot);
    }
  }
  bool pending = false;
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    pending |= slots[i].active && slots[i].pending;
  if (pending && backlog_fd >= 0) {
    uint64_t one = 1;
    ssize_t ignored = write(backlog_fd, &one, sizeof(one));
    (void)ignored;
  }
  if (!transcript_watch_count())
    close_notifiers();
}
void transcript_watch_cleanup(void) {
  if (prompt_job.fd >= 0)
    agent_watch_unlisten(prompt_job.fd);
  job_cleanup(&prompt_job);
  memset(prompt_job.buffer, 0, sizeof(prompt_job.buffer));
  for (int i = 0; i < AGENT_SESSIONS_MAX; i++)
    disarm(&slots[i]);
  memset(slots, 0, sizeof(slots));
  close_notifiers();
  enabled = false;
  cursor = 0;
}

int transcript_watch_command(const char *request, int64_t now_ms) {
  char key[17];
  int end = 0;
  if (strlen(request) > AGENT_TRANSCRIPT_PATH_MAX + 22 ||
      sscanf(request, "path %16[0-9a-fA-F]%n", key, &end) != 1 || end != 21 ||
      request[21] != ' ' || request[22] != '/' || !strtoull(key, NULL, 16))
    return 1;
  agent_sessions_adopt(strtoull(key, NULL, 16));
  transcript_watch_path(strtoull(key, NULL, 16), request + 22, now_ms);
  return 0;
}

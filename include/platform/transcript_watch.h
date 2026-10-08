#ifndef TRANSCRIPT_WATCH_H
#define TRANSCRIPT_WATCH_H

#include <stdbool.h>
#include <stdint.h>

// Owns one inotify plus a backlog eventfd, both on agent_watch's epoll.
void transcript_prompt_poll(void);
int transcript_prompt_timeout(int64_t now_ms);
void transcript_watch_sync(bool on, int64_t now_ms);
// Invalid paths and unknown/unsupported sessions are silently ignored.
void transcript_watch_path(uint64_t key, const char *path, int64_t now_ms);
void transcript_watch_ready(uint32_t token, int64_t now_ms);
void transcript_watch_cleanup(void);
int transcript_watch_command(const char *request, int64_t now_ms);
// Validate and open a home-confined regular same-UID file, without symlinks.
int transcript_watch_open(const char *path);
int transcript_watch_count(void);

#endif

#ifndef HERDCAT_CONTROL_H
#define HERDCAT_CONTROL_H
#include <stddef.h>
// A reply is one packet, "<status> <detail>". The handler gets
// CONTROL_DETAIL_MAX bytes, enough for the whole session table; the status
// prefix needs at most 12 more.
#define CONTROL_DETAIL_MAX 16384
#define CONTROL_REPLY_MAX  (CONTROL_DETAIL_MAX + 16)
int instance_lock(void);
void instance_unlock(void);
int control_start(void);
void control_cleanup(void);
int control_request(const char *request);
void control_process(int (*handler)(const char *, char *, size_t));
int control_timeout(void);
int control_fds(int *fds, size_t capacity);
#endif

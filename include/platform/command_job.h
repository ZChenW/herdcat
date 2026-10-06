#ifndef HERDCAT_COMMAND_JOB_H
#define HERDCAT_COMMAND_JOB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef struct {
  pid_t pid;
  int fd;
  int64_t deadline;
  size_t used;
  bool eof, failed, exited;
  int status;
  char buffer[65536];
} command_job_t;
extern const char *command_socket;
int job_start(command_job_t *job, const char *const argv[]);
int job_process(command_job_t *job);
void job_cleanup(command_job_t *job);
#endif

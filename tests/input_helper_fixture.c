#define _GNU_SOURCE
#include "platform/input.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
  input_privilege_init();
  if (argc > 1 && !strcmp(argv[1], "--input-helper")) {
    return input_helper_main(argc, argv);
  }
  input_privilege_drop();
  if (argc == 2 && !strcmp(argv[1], "--list-devices")) {
    return input_list_devices();
  }
  int interval = argc == 2 && !strcmp(argv[1], "--once") ? 0 : 1;
  if (input_start_monitoring(NULL, 0, NULL, 0, interval, 0) !=
      HERDCAT_SUCCESS) {
    return 1;
  }
  printf("input-helper=%s helper-pid=%ld\n%s\n", input_mode_name(),
         (long)input_get_child_pid(), input_mode_hint());
  fflush(stdout);
  struct pollfd fds[2] = {
      {.fd = input_get_wake_fd(), .events = POLLIN},
      {.fd = STDIN_FILENO,        .events = POLLIN}
  };
  while (poll(fds, 2, 5000) > 0) {
    if (fds[0].revents) {
      input_process_events();
      unsigned paws = pending_paws ? atomic_exchange(pending_paws, 0) : 0;
      printf("devices=%u denied=%u paws=%u\n", input_device_count(),
             input_denied_count(), paws);
      fflush(stdout);
    }
    if (fds[1].revents) {
      break;
    }
  }
  input_cleanup();
  return 0;
}

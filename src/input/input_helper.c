#define _GNU_SOURCE
#include "platform/input_protocol.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/audit.h>
#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// No configuration, logging or renderer code belongs in this executable.
typedef struct {
  int output;
  int interval;
  int filtered;
  int count;
  char **paths;
} input_options_t;

typedef struct {
  int fd;
  dev_t identity;
  char path[INPUT_PATH_SIZE];
} input_device_t;

static gid_t real_group;
static gid_t saved_group;

static bool input_number(const char *text, int maximum, int *value) {
  if (!text || !*text) {
    return false;
  }
  unsigned parsed = 0;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] < '0' || text[i] > '9' ||
        (unsigned)(text[i] - '0') > (unsigned)maximum ||
        parsed > ((unsigned)maximum - (unsigned)(text[i] - '0')) / 10) {
      return false;
    }
    parsed = (parsed * 10) + (unsigned)(text[i] - '0');
  }
  *value = (int)parsed;
  return true;
}

static bool input_output(int fd) {
  struct stat metadata;
  int flags = fcntl(fd, F_GETFL);
  if (fd < 3 || flags < 0 || ((unsigned)flags & O_ACCMODE) == O_RDONLY ||
      fstat(fd, &metadata) != 0) {
    return false;
  }
  if (S_ISSOCK(metadata.st_mode)) {
    struct ucred peer;
    int type = 0;
    int domain = 0;
    socklen_t size = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) != 0 ||
        type != SOCK_SEQPACKET ||
        getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &size) != 0 ||
        domain != AF_UNIX) {
      return false;
    }
    size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 ||
        peer.uid != getuid() || peer.pid != getppid() || peer.pid <= 1) {
      return false;
    }
  } else if (!S_ISFIFO(metadata.st_mode) ||
             ((unsigned)flags & O_ACCMODE) != O_WRONLY) {
    // eventfd cannot carry the existing 24-byte packet; reject it too.
    return false;
  }
  return fcntl(fd, F_SETFL, (int)((unsigned)flags | O_NONBLOCK)) == 0;
}

static bool input_arguments(int argc, char **argv, input_options_t *options) {
  if (argc < 5 || !input_number(argv[1], INT_MAX, &options->output) ||
      !input_number(argv[2], 3600, &options->interval) ||
      !input_number(argv[3], 1, &options->filtered) ||
      !input_number(argv[4], INPUT_MAX_DEVICES, &options->count) ||
      argc != 5 + options->count ||
      (!options->filtered && options->count != 0)) {
    return false;
  }
  options->paths = argv + 5;
  for (int i = 0; i < options->count; i++) {
    if (!input_event_path(options->paths[i])) {
      return false;
    }
  }
  return input_output(options->output);
}

static bool input_group(bool raise, bool permanent) {
  gid_t effective = raise ? saved_group : real_group;
  gid_t saved = permanent ? real_group : saved_group;
  gid_t real;
  gid_t current;
  gid_t retained;
  if (setresgid(real_group, effective, saved) != 0 ||
      getresgid(&real, &current, &retained) != 0 || real != real_group ||
      current != effective || retained != saved) {
    return false;
  }
  // Changing effective gid can reset dumpability; reset it every time.
  return prctl(PR_SET_DUMPABLE, 0) == 0;
}

static bool input_metadata(const struct stat *metadata) {
  return S_ISCHR(metadata->st_mode) && major(metadata->st_rdev) == 13;
}

static int input_open(const char *path) {
  if (!input_event_path(path) || !input_group(true, false)) {
    _exit(1);
  }
  // Anchor the directory too: no symlink components in privileged opens.
  int directory = open("/dev/input", O_RDONLY | O_DIRECTORY | O_NOFOLLOW |
                                         O_CLOEXEC | O_NONBLOCK);
  int fd = directory < 0
               ? -1
               : openat(directory, path + strlen("/dev/input/"),
                        O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  int error = errno;
  if (directory >= 0) {
    close(directory);
  }
  if (!input_group(false, false)) {
    _exit(1);
  }
  struct stat metadata;
  if (fd >= 0 && (fstat(fd, &metadata) != 0 || !input_metadata(&metadata))) {
    close(fd);
    fd = -1;
    error = EINVAL;
  }
  errno = error;
  return fd;
}

static bool input_keyboard(int fd) {
  unsigned long bits[(KEY_MAX + (8 * sizeof(unsigned long))) /
                     (8 * sizeof(unsigned long))] = {0};
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
    return false;
  }
  static const unsigned keys[] = {KEY_A, KEY_Z, KEY_ENTER, KEY_SPACE};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (!(bits[keys[i] / (8 * sizeof(unsigned long))] &
          (1UL << (keys[i] % (8 * sizeof(unsigned long)))))) {
      return false;
    }
  }
  return true;
}

static unsigned input_attach(input_device_t *devices, const char *path,
                             bool filtered) {
  int slot = -1;
  for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
    if (devices[i].fd >= 0 && strcmp(devices[i].path, path) == 0) {
      return 0;
    }
    if (slot < 0 && devices[i].fd < 0) {
      slot = i;
    }
  }
  if (slot < 0) {
    return 0;
  }
  int fd = input_open(path);
  if (fd < 0) {
    return errno == EACCES || errno == EPERM;
  }
  struct stat metadata;
  if (fstat(fd, &metadata) != 0 || (!filtered && !input_keyboard(fd))) {
    close(fd);
    return 0;
  }
  for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
    if (devices[i].fd >= 0 && devices[i].identity == metadata.st_rdev) {
      close(fd);
      return 0;
    }
  }
  devices[slot].fd = fd;
  devices[slot].identity = metadata.st_rdev;
  memcpy(devices[slot].path, path, strlen(path) + 1);
  return 0;
}

static unsigned input_scan(input_device_t *devices,
                           const input_options_t *options) {
  unsigned denied = 0;
  if (options->filtered) {
    for (int i = 0; i < options->count; i++) {
      denied += input_attach(devices, options->paths[i], true);
    }
    return denied;
  }
  int fd = open("/dev/input",
                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  DIR *directory = fd < 0 ? NULL : fdopendir(fd);
  if (!directory) {
    if (fd >= 0) {
      close(fd);
    }
    return errno == EACCES || errno == EPERM;
  }
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    char path[INPUT_PATH_SIZE];
    int length = snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
    if (length > 0 && length < (int)sizeof(path) && input_event_path(path)) {
      denied += input_attach(devices, path, false);
    }
  }
  closedir(directory);
  return denied;
}

static bool input_emit(int output, unsigned paws, unsigned count,
                       unsigned denied) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return false;
  }
  input_message_t message = {
      .paws = paws,
      .devices = count,
      .denied = denied,
      .monotonic_ns = ((int64_t)now.tv_sec * 1000000000) + now.tv_nsec,
  };
  ssize_t result = write(output, &message, sizeof(message));
  return (bool)(result == sizeof(message) ||
                (result < 0 && (errno == EAGAIN || errno == EINTR)));
}

static int input_capture(const input_options_t *options) {
  input_device_t devices[INPUT_MAX_DEVICES];
  for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
    devices[i] = (input_device_t){.fd = -1};
  }
  time_t next_scan = 0;
  bool scanned = false;
  unsigned denied = 0;
  while (true) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      return 1;
    }
    bool scan =
        (bool)(!scanned || (options->interval != 0 && now.tv_sec >= next_scan));
    if (scan) {
      denied = input_scan(devices, options);
      scanned = true;
      next_scan = now.tv_sec + options->interval;
      if (options->interval == 0 && !input_group(false, true)) {
        return 1;
      }
    }
    struct pollfd fds[INPUT_MAX_DEVICES + 1] = {
        {.fd = options->output, .events = 0},
    };
    unsigned count = 0;
    for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
      fds[i + 1] = (struct pollfd){.fd = devices[i].fd, .events = POLLIN};
      count += devices[i].fd >= 0;
    }
    // A status heartbeat detects pipe loss even when no device is active.
    // It also lets the unprivileged parent refresh name/alias selectors.
    if (scan && !input_emit(options->output, 0, count, denied)) {
      return 0;
    }
    if (poll(fds, INPUT_MAX_DEVICES + 1, 1000) < 0 && errno != EINTR) {
      return 1;
    }
    if (((unsigned)fds[0].revents & (unsigned)(POLLHUP | POLLERR | POLLNVAL)) !=
        0) {
      return 0;
    }
    unsigned paws = 0;
    for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
      if (!fds[i + 1].revents) {
        continue;
      }
      struct input_event events[64];
      ssize_t bytes = read(devices[i].fd, events, sizeof(events));
      if (((unsigned)fds[i + 1].revents &
           (unsigned)(POLLHUP | POLLERR | POLLNVAL)) ||
          bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EINTR)) {
        close(devices[i].fd);
        devices[i].fd = -1;
        if (!input_emit(options->output, 0, count - 1, denied)) {
          return 0;
        }
        count--;
        continue;
      }
      if (bytes < 0) {
        continue;
      }
      if ((size_t)bytes % sizeof(events[0]) != 0) {
        return 1;
      }
      for (size_t j = 0; j < (size_t)bytes / sizeof(events[0]); j++) {
        if (events[j].type == EV_KEY && events[j].value == 1) {
          paws |= paw_for_keycode(events[j].code);
        }
      }
    }
    if (paws && !input_emit(options->output, paws, count, denied)) {
      return 0;
    }
  }
}

// libc's directory allocator needs brk/mmap/munmap, and hotplug needs
// getdents64 and verified gid transitions in addition to evdev syscalls.
// Unknown architectures fail closed until their ABI has been audited.
static bool input_seccomp(int output) {
#ifdef __x86_64__
#  define INPUT_AUDIT_ARCH AUDIT_ARCH_X86_64
#elifdef __aarch64__
#  define INPUT_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
  (void)output;
  return false;
#endif
#ifdef INPUT_AUDIT_ARCH
#  define INPUT_ALLOW(nr)                          \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, nr, 0, 1), \
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
#  define INPUT_ARG(index)             \
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, \
             offsetof(struct seccomp_data, args[index]))
  const struct sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, INPUT_AUDIT_ARCH, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      // write can only send the paw packet to the validated inherited fd.
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0, 4),
      INPUT_ARG(0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (unsigned)output, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_openat, 0, 5),
      INPUT_ARG(2),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 2, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_DIRECTORY, 1,
               0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      // Only keyboard capability queries; no grab or state-changing ioctl.
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_ioctl, 0, 4),
      INPUT_ARG(1),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
               EVIOCGBIT(EV_KEY, ((KEY_MAX + (8 * sizeof(unsigned long))) /
                                  (8 * sizeof(unsigned long))) *
                                     sizeof(unsigned long)),
               0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_prctl, 0, 6),
      INPUT_ARG(0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PR_SET_DUMPABLE, 0, 3),
      INPUT_ARG(1),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fcntl, 0, 4),
      INPUT_ARG(1),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, F_GETFL, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_mmap, 0, 4),
      INPUT_ARG(2),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, PROT_EXEC, 1, 0),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      INPUT_ALLOW(SYS_read),
      INPUT_ALLOW(SYS_close),
#  ifdef SYS_poll
      INPUT_ALLOW(SYS_poll),
#  endif
      INPUT_ALLOW(SYS_ppoll),
#  ifdef SYS_fstat
      INPUT_ALLOW(SYS_fstat),
#  endif
      INPUT_ALLOW(SYS_newfstatat),
      INPUT_ALLOW(SYS_getdents64),
      INPUT_ALLOW(SYS_clock_gettime),
      INPUT_ALLOW(SYS_setresgid),
      INPUT_ALLOW(SYS_getresgid),
      INPUT_ALLOW(SYS_brk),
      INPUT_ALLOW(SYS_munmap),
      INPUT_ALLOW(SYS_rt_sigreturn),
      INPUT_ALLOW(SYS_exit),
      INPUT_ALLOW(SYS_exit_group),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
  };
  const struct sock_fprog program = {
      .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
      .filter = (struct sock_filter *)filter,
  };
  return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
#  undef INPUT_ALLOW
#  undef INPUT_ARG
#endif
}

int main(int argc, char **argv) {
  if (clearenv() != 0) {
    return 1;
  }
  if (prctl(PR_SET_DUMPABLE, 0) != 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    return 1;
  }
  if (geteuid() != getuid()) {
    return 1;  // A setuid installation is never part of this contract.
  }
  real_group = getgid();
  saved_group = getegid();
  if (!input_group(false, false)) {
    return 1;
  }
  input_options_t options = {0};
  if (!input_arguments(argc, argv, &options)) {
    return 1;
  }
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    return 1;
  }
  // Discard inherited descriptors, including all standard streams.
  for (int fd = 0; fd < 3; fd++) {
    close(fd);
  }
  if ((options.output > 3 && close_range(3, (unsigned)options.output - 1, 0)) ||
      close_range((unsigned)options.output + 1, UINT_MAX, 0)) {
    return 1;
  }
  if (!input_seccomp(options.output)) {
    return 1;
  }
  return input_capture(&options);
}

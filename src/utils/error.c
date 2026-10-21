#define _POSIX_C_SOURCE 200809L
#include "utils/error.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

// Clang's analyzer does not yet model __builtin_c23_va_start. The legacy
// GCC/Clang builtin has the same contract for our named variadic functions.
#ifdef __GNUC__
#  define HERDCAT_VA_START(args, last) __builtin_va_start(args, last)
#else
#  define HERDCAT_VA_START(args, last) va_start(args, last)
#endif

static atomic_int debug_enabled = 1;

void herdcat_error_init(int enable_debug) {
  atomic_store(&debug_enabled, enable_debug);
}

static void log_timestamp(FILE *stream) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  time_t sec = ts.tv_sec;
  struct tm tm_info = {0};
  char time_str[32];
  localtime_r(&sec, &tm_info);
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);
  fprintf(stream, "[%s.%03ld] ", time_str, ts.tv_nsec / 1000000L);
}

void herdcat_log_error(const char *format, ...) {
  va_list args;
  char message[1024];
  HERDCAT_VA_START(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log_timestamp(stderr);
  fprintf(stderr, "ERROR: %s\n", message);
  fflush(stderr);
}

void herdcat_log_warning(const char *format, ...) {
  va_list args;
  char message[1024];
  HERDCAT_VA_START(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log_timestamp(stderr);
  fprintf(stderr, "WARNING: %s\n", message);
  fflush(stderr);
}

void herdcat_log_info(const char *format, ...) {
  va_list args;
  char message[1024];
  HERDCAT_VA_START(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log_timestamp(stdout);
  fprintf(stdout, "INFO: %s\n", message);
  fflush(stdout);
}

void herdcat_log_debug(const char *format, ...) {
  if (!atomic_load(&debug_enabled)) {
    return;
  }

  va_list args;
  char message[1024];
  HERDCAT_VA_START(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  log_timestamp(stdout);
  fprintf(stdout, "DEBUG: %s\n", message);
  fflush(stdout);
}

const char *herdcat_error_string(herdcat_error_t error) {
  switch (error) {
  case HERDCAT_SUCCESS:
    return "Success";
  case HERDCAT_ERROR_MEMORY:
    return "Memory allocation error";
  case HERDCAT_ERROR_FILE_IO:
    return "File I/O error";
  case HERDCAT_ERROR_WAYLAND:
    return "Wayland error";
  case HERDCAT_ERROR_CONFIG:
    return "Configuration error";
  case HERDCAT_ERROR_INPUT:
    return "Input error";
  case HERDCAT_ERROR_ANIMATION:
    return "Animation error";
  case HERDCAT_ERROR_THREAD:
    return "Thread error";
  case HERDCAT_ERROR_INVALID_PARAM:
    return "Invalid parameter";
  default:
    return "Unknown error";
  }
}

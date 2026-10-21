#ifndef ERROR_H
#define ERROR_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// C23 COMPATIBILITY MACROS
// =============================================================================

// Nodiscard for functions that return values that must be used
#if __STDC_VERSION__ >= 202311L
#  define HERDCAT_NODISCARD [[nodiscard]]
#else
#  define HERDCAT_NODISCARD __attribute__((warn_unused_result))
#endif

// Null pointer (C23 nullptr or fallback)
#if __STDC_VERSION__ >= 202311L
#  define HERDCAT_NULLPTR nullptr
#else
#  define HERDCAT_NULLPTR NULL
#endif

// Unreachable code hint for optimizer
#if __STDC_VERSION__ >= 202311L
#  define HERDCAT_UNREACHABLE() unreachable()
#else
#  define HERDCAT_UNREACHABLE() __builtin_unreachable()
#endif

// =============================================================================
// ERROR CODES
// =============================================================================

typedef enum {
  HERDCAT_SUCCESS = 0,
  HERDCAT_ERROR_MEMORY,
  HERDCAT_ERROR_FILE_IO,
  HERDCAT_ERROR_WAYLAND,
  HERDCAT_ERROR_CONFIG,
  HERDCAT_ERROR_INPUT,
  HERDCAT_ERROR_ANIMATION,
  HERDCAT_ERROR_THREAD,
  HERDCAT_ERROR_INVALID_PARAM
} herdcat_error_t;

// =============================================================================
// GUARD CLAUSE MACROS
// =============================================================================

// Guard clause for null pointer - returns early with error
#define HERDCAT_CHECK_NULL(ptr, error_code)                          \
  do {                                                               \
    if ((ptr) == HERDCAT_NULLPTR) {                                  \
      herdcat_log_error("NULL pointer: %s at %s:%d", #ptr, __FILE__, \
                        __LINE__);                                   \
      return (error_code);                                           \
    }                                                                \
  } while (0)

// Guard clause for error conditions - returns early with error
#define HERDCAT_CHECK_ERROR(condition, error_code, message)          \
  do {                                                               \
    if (condition) {                                                 \
      herdcat_log_error("%s at %s:%d", message, __FILE__, __LINE__); \
      return (error_code);                                           \
    }                                                                \
  } while (0)

// Guard clause for boolean conditions - returns early with value
#define HERDCAT_GUARD(condition, return_value) \
  do {                                         \
    if (condition) {                           \
      return (return_value);                   \
    }                                          \
  } while (0)

// =============================================================================
// LOGGING FUNCTIONS
// =============================================================================

void herdcat_log_error(const char *format, ...);
void herdcat_log_warning(const char *format, ...);
void herdcat_log_info(const char *format, ...);
void herdcat_log_debug(const char *format, ...);

// =============================================================================
// ERROR HANDLING
// =============================================================================

void herdcat_error_init(int enable_debug);
HERDCAT_NODISCARD const char *herdcat_error_string(herdcat_error_t error);

#endif  // ERROR_H
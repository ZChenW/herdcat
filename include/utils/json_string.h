#ifndef HERDCAT_JSON_STRING_H
#define HERDCAT_JSON_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Decode a validated JSON string slice, retaining complete UTF-8 scalars.
// Oversized titles are bounded in memory and never written to a log.
static inline void json_string_copy(const char *raw, size_t length, char *out,
                                    size_t capacity) {
  size_t used = 0;
  for (size_t i = 0; i < length && capacity; i++) {
    unsigned char c = (unsigned char)raw[i];
    char bytes[4];
    size_t n = 1;
    bytes[0] = (char)c;
    if (c == '\\' && ++i < length) {
      c = (unsigned char)raw[i];
      if (c == 'u' && i + 4 < length) {
        uint32_t cp = 0;
        for (int k = 0; k < 4; k++) {
          char h = raw[++i];
          cp = cp * 16 + (uint32_t)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
        }
        if (cp >= 0xd800 && cp <= 0xdbff && i + 6 < length &&
            raw[i + 1] == '\\' && raw[i + 2] == 'u') {
          uint32_t low = 0;
          i += 2;
          for (int k = 0; k < 4; k++) {
            char h = raw[++i];
            low =
                low * 16 + (uint32_t)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
          }
          cp = low >= 0xdc00 && low <= 0xdfff
                   ? 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00
                   : 0xfffd;
        }
        if (!cp || (cp >= 0xd800 && cp <= 0xdfff))
          cp = 0xfffd;
        if (cp < 0x80) {
          bytes[0] = (char)cp;
        } else if (cp < 0x800) {
          bytes[0] = (char)(0xc0 | (cp >> 6));
          bytes[1] = (char)(0x80 | (cp & 63));
          n = 2;
        } else if (cp < 0x10000) {
          bytes[0] = (char)(0xe0 | (cp >> 12));
          bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
          bytes[2] = (char)(0x80 | (cp & 63));
          n = 3;
        } else {
          bytes[0] = (char)(0xf0 | (cp >> 18));
          bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
          bytes[2] = (char)(0x80 | ((cp >> 6) & 63));
          bytes[3] = (char)(0x80 | (cp & 63));
          n = 4;
        }
      } else {
        const char escapes[] = "bfnrt";
        const char *esc = strchr(escapes, c);
        bytes[0] = esc ? "\b\f\n\r\t"[esc - escapes] : (char)c;
      }
    } else if (c >= 0xc2) {
      n = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
      if (i + n > length)
        break;
      memcpy(bytes, raw + i, n);
      i += n - 1;
    }
    if (used + n >= capacity)
      break;
    memcpy(out + used, bytes, n);
    used += n;
  }
  if (capacity)
    out[used] = 0;
}
#endif

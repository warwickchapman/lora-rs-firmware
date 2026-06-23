#pragma once

#include <cstddef>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

inline size_t lrsStrlcpy(char *dst, const char *src, size_t size) {
  if (src == nullptr) src = "";
  const size_t srcLen = strlen(src);
  if (size != 0) {
    const size_t copyLen = (srcLen >= size) ? size - 1 : srcLen;
    memcpy(dst, src, copyLen);
    dst[copyLen] = '\0';
  }
  return srcLen;
}

template <size_t N>
struct FixedSettingString {
  char value[N]{};

  FixedSettingString() = default;
  FixedSettingString(const char *s) { set(s); }
#if defined(ARDUINO)
  FixedSettingString(const String &s) { set(s.c_str()); }
#endif

  FixedSettingString &operator=(const char *s) {
    set(s);
    return *this;
  }
#if defined(ARDUINO)
  FixedSettingString &operator=(const String &s) {
    set(s.c_str());
    return *this;
  }
#endif
  FixedSettingString &operator=(const FixedSettingString &other) {
    if (this != &other) set(other.value);
    return *this;
  }

  const char *c_str() const { return value; }
  size_t length() const { return strlen(value); }
  bool isEmpty() const { return value[0] == '\0'; }
  operator const char *() const { return value; }

  bool equals(const char *s) const { return *this == s; }
#if defined(ARDUINO)
  bool equals(const String &s) const { return s.equals(value); }
#endif
  bool operator==(const char *s) const {
    return strcmp(value, s ? s : "") == 0;
  }
  bool operator!=(const char *s) const { return !(*this == s); }
#if defined(ARDUINO)
  bool operator==(const String &s) const { return s.equals(value); }
  bool operator!=(const String &s) const { return !s.equals(value); }
#endif
  template <size_t M>
  bool operator==(const FixedSettingString<M> &other) const {
    return strcmp(value, other.value) == 0;
  }
  template <size_t M>
  bool operator!=(const FixedSettingString<M> &other) const {
    return strcmp(value, other.value) != 0;
  }

  void trim() {
    char *start = value;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n')) {
      --end;
    }
    const size_t len = static_cast<size_t>(end - start);
    if (start != value) memmove(value, start, len);
    value[len] = '\0';
  }

  void toLowerCase() {
    for (size_t i = 0; value[i] != '\0'; ++i) {
      if (value[i] >= 'A' && value[i] <= 'Z') {
        value[i] = static_cast<char>(value[i] - 'A' + 'a');
      }
    }
  }

 private:
  void set(const char *s) { lrsStrlcpy(value, s, N); }
};

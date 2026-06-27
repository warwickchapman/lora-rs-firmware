#pragma once
#include <string>

class JsonDocument {
public:
  template<typename T>
  T as() const { return T(); }
  template<typename T>
  bool is() const { return false; }
};

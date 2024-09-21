#pragma once
#include <string>
#include <chrono>

namespace brocseg {
namespace prof {
class watch {
public:
  watch() {
    beg_ = std::chrono::steady_clock::now();
  }
  std::string report(const std::string& name) {
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<float> elapsed_seconds{end - beg_};
    return name + " took " + std::to_string(elapsed_seconds.count()) + "s";
  }

private:
  std::chrono::time_point<std::chrono::steady_clock> beg_;
};

} // namespace prof
} // namespace brocseg

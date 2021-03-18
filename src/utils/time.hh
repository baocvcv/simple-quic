#ifndef _THQUIC_UTILS_TIME_H_
#define _THQUIC_UTILS_TIME_H_

#include <chrono>

namespace thquic {
namespace utils {
using clock = std::chrono::steady_clock;
using timepoint = clock::time_point;
using namespace std::chrono_literals;
}  // namespace utils
}  // namespace thquic

#endif

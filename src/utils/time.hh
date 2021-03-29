#ifndef _THQUIC_UTILS_TIME_H_
#define _THQUIC_UTILS_TIME_H_

#include <chrono>

namespace thquic::utils {
using clock = std::chrono::steady_clock;
using timepoint = clock::time_point;
using duration = clock::duration;
using namespace std::chrono_literals;
}  // namespace thquic::utils

#endif

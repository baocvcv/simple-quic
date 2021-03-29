#ifndef _THQUIC_UTILS_LOG_H_
#define _THQUIC_UTILS_LOG_H_

#include <arpa/inet.h>

#include "spdlog/spdlog.h"
#include "utils/time.hh"

namespace thquic::utils {
namespace logger = spdlog;

int initLogger();

std::string formatNetworkAddress(const struct sockaddr_in& addr);

std::string formatTimepoint(timepoint time);

std::string formatTimeDuration(duration d);
}  // namespace thquic::utils

#endif
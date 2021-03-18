#ifndef _THQUIC_CONFIG_H_
#define _THQUIC_CONFIG_H_

#include <memory>

namespace thquic::config {

constexpr size_t LOCAL_CONNECTION_ID_LENGTH = 8;
constexpr size_t CONNECTION_ID_MAX_SIZE = 20;
constexpr uint32_t QUIC_VERSION = 0x00000001;
constexpr size_t UDP_MAX_BODY = 1472;

}  // namespace thquic::config

#endif
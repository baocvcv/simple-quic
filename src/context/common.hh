#ifndef _THQUIC_CONTEXT_COMMON_H_
#define _THQUIC_CONTEXT_COMMON_H_

#include <memory>

namespace thquic::context {
enum class PacketContext { INITIAL, HANDSHAKE, APPLICATION };

enum class PeerType { CLIENT, SERVER };

enum class StreamType {
    CLIENT_BI = 0,
    SERVER_BI = 1,
    CLIENT_UNI = 2,
    SERVER_UNI = 3
};

constexpr size_t STREAM_TYPE_NUM = 4;

enum TransportErrorCodes {
    NO_ERROR = 0x0,
    INTERNAL_ERROR = 0x1,
    CONNECTION_REFUSED = 0x2,
    FLOW_CONTROL_ERROR = 0x3,
    STREAM_LIMIT_ERROR = 0x4,
    STREAM_STATE_ERROR = 0x5,
};

}  // namespace thquic::context
#endif
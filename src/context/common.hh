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

}  // namespace thquic::context
#endif
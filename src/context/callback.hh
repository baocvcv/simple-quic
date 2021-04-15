#ifndef _THQUIC_CONTEXT_CALLBACK_H_
#define _THQUIC_CONTEXT_CALLBACK_H_

#include <functional>
#include <memory>

namespace thquic::context {
/*
 * Invoke when client or server completes handshake and the connection is ready.
 * @param connectionSequence a 64 bit descriptor of the established connection
 * @return errorCode
 * */
using ConnectionReadyCallbackType = std::function<int(uint64_t)>;

/**
 * Invoke when a connection is closed by the peer
 * @param descriptor of the connection closed by the peer
 * @return error code
 * */
using ConnectionCloseCallbackType =
std::function<int(uint64_t, std::string, uint64_t)>;

/*
 * Invoke when receives data in a new stream from the remote peer
 * @param descriptor of the connection the new stream belongs to
 * @param streamID of the created stream
 * @return errorCode
 * */
using StreamReadyCallbackType = std::function<int(uint64_t, uint64_t)>;

/*
 * Invoke when receives data from the peer
 * @param connection descriptor
 * @param stream ID in which data arrives
 * @param receiving data buffer
 * @param length of the receiving data buffer
 * @param whether the stream ends
 * @return errorCode
 * */
using StreamDataReadyCallbackType = std::function<int(
    uint64_t, uint64_t, std::unique_ptr<uint8_t[]>, size_t, bool)>;


using SentPktACKedCallbackType = std::function<int(
    uint64_t)>;

}  // namespace thquic::context



#endif  // THQUIC_CALLBACK_HH
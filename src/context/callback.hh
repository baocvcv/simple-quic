#ifndef _THQUIC_CONTEXT_CALLBACK_H_
#define _THQUIC_CONTEXT_CALLBACK_H_

#include <functional>
#include <memory>

namespace thquic::context {

using ConnectionReadyCallbackType = std::function<int(uint64_t)>;
using ConnectionCloseCallbackType =
std::function<int(uint64_t, std::string, uint64_t)>;

using StreamReadyCallbackType = std::function<int(uint64_t, uint64_t)>;
using StreamDataReadyCallbackType = std::function<int(
    uint64_t, uint64_t, std::unique_ptr<uint8_t[]>, size_t, bool)>;

}  // namespace thquic::context

#endif  // THQUIC_CALLBACK_HH
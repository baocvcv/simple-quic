#ifndef _THQUIC_UTILS_VARIABLE_CODEC_H_
#define _THQUIC_UTILS_VARIABLE_CODEC_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

#include "utils/bytestream.hh"

namespace thquic::utils {

int encodeUInt(ByteStream& stream, uint64_t value, uint8_t intSize);
uint64_t decodeUint(ByteStream& stream, uint8_t intSize);
size_t encodeVarIntLen(uint64_t value);

// QUIC RFC Sec. 16 Variable-Length Integer Encoding
int encodeVarInt(ByteStream& stream, uint64_t value);
uint64_t decodeVarInt(ByteStream& stream);

int encodeBuffer(ByteStream& stream, const std::unique_ptr<uint8_t[]> buf,
                 size_t len);
template <std::size_t SIZE>
void encodeBuffer(ByteStream& stream, std::array<int, SIZE>& buf);

std::unique_ptr<uint8_t[]> decodeBuffer(ByteStream& stream, size_t len);

}  // namespace thquic::utils
#endif

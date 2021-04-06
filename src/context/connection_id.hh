#ifndef _THQUIC_CONTEXT_CONNECTION_ID_H_
#define _THQUIC_CONTEXT_CONNECTION_ID_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <set>

#include "config.hh"
#include "payload/common.hh"
#include "utils/bytestream.hh"
#include "utils/log.hh"
#include "utils/random.hh"
#include "utils/time.hh"
#include "utils/variable_codec.hh"

namespace thquic {

constexpr uint32_t MAX_REGENERATION = 16;
using IDType = std::array<uint8_t, config::CONNECTION_ID_MAX_SIZE>;

class ConnectionID;
class ConnectionIDGenerator {
   public:
    static ConnectionIDGenerator& Get();

    ConnectionID Generate();

    void AddUsedConnectionID(ConnectionID uid); // { this->usedID.insert(uid); }

   private:
    explicit ConnectionIDGenerator(size_t localConnectionIDLen);
    std::set<ConnectionID> usedID;
    size_t connectionIDLen;
};

class ConnectionID {
   public:
    ConnectionID();

    ConnectionID(IDType id, size_t len);

    explicit ConnectionID(utils::ByteStream& stream);

    ConnectionID(utils::ByteStream& stream, uint8_t connectionIDLen);

    bool operator==(const ConnectionID& other) const;
    ;

    bool operator<(const ConnectionID& other) const;

    int Encode(utils::ByteStream& stream, bool withIDLen = false);

    uint8_t EncodeLen(bool withIDLen = false) const;

    const IDType& ID() const;

    uint8_t IDLen() const;

    bool Valid() const;

    std::string ToString() const;

   private:
    IDType id;
    uint8_t valid : 1;
    uint8_t len : 7;
};

class LocalConnectionContext {
   public:
    LocalConnectionContext(uint64_t sequence, utils::timepoint current);
    ;

    bool Valid();
    const ConnectionID& ID() const;

   private:
    ConnectionID localID;
    uint64_t sequence;
    utils::timepoint createTime;
};

class RemoteConnectionContext {
   public:
    RemoteConnectionContext() = default;
    RemoteConnectionContext(const ConnectionID& remoteID, uint64_t sequence);
    bool Valid();
    const ConnectionID& ID() const;

   private:
    ConnectionID remoteID;
    uint64_t sequence{};
};

};  // namespace thquic

namespace std {
template <>
struct hash<thquic::ConnectionID> {
    size_t operator()(thquic::ConnectionID const& s) const noexcept {
        constexpr size_t S = sizeof(size_t);
        size_t h = 0;
        for (size_t i = 0; i < s.IDLen(); i++) {
            h ^= (s.ID()[i] << (8 * (i % S)));
        }
        return h;
    };
};
}  // namespace std

#endif
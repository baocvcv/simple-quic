#ifndef _THQUIC_PAYLOAD_PACKET_H_
#define _THQUIC_PAYLOAD_PACKET_H_

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <numeric>
#include <utility>

#include "config.hh"
#include "context/connection_id.hh"
#include "payload/frame.hh"
#include "utils/bytestream.hh"
#include "utils/variable_codec.hh"

namespace thquic::payload {

enum class PacketType {
    INITIAL,    // sec 17.2.2
    ZERO_RTT,   // sec 17.2.3
    HANDSHAKE,  // sec 17.2.4
    RETRY,      // sec 17.2.5
    ONE_RTT     // sec 17.3
};

class ShortHeader;
class Initial;
class ZeroRTT;
class Handshake;
class Retry;

class Header : public Serialization {
   public:
    static std::shared_ptr<Header> Parse(utils::ByteStream& stream) {
        auto buf = stream.Fetch(1).first;
        switch (*buf & 0x80) {
            case 0x80:
                switch (*buf & 0x30) {
                    case 0x00:
                        return std::static_pointer_cast<Header>(
                            std::make_shared<Initial>(stream));
                    case 0x10:
                        return std::static_pointer_cast<Header>(
                            std::make_shared<ZeroRTT>(stream));
                    case 0x20:
                        return std::static_pointer_cast<Header>(
                            std::make_shared<Handshake>(stream));
                    case 0x30:
                        return std::static_pointer_cast<Header>(
                            std::make_shared<Retry>(stream));
                    default:
                        throw std::runtime_error("should not reach here");
                }
            case 0x00:
                return std::static_pointer_cast<Header>(
                    std::make_shared<ShortHeader>(
                        stream, config::LOCAL_CONNECTION_ID_LENGTH));
            default:
                throw std::runtime_error("should not reach here");
        }
    }

    virtual PacketType Type() const = 0;
    virtual uint64_t GetPktNum() const = 0;
    virtual const ConnectionID& GetDstID() const = 0;
    virtual bool IsFrameAllowed(FrameType type) const = 0;
};

class Payload : public Serialization {
   public:
    Payload() = default;
    Payload(utils::ByteStream& stream, size_t len) {
        size_t pos = stream.Pos();
        while (stream.Pos() - pos < len) {
            this->frames.emplace_back(Frame::Parse(stream));
        }
    }

    explicit Payload(utils::ByteStream& stream) {
        while (!stream.Empty()) {
            this->frames.emplace_back(Frame::Parse(stream));
        }
    }

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        for (const auto& frame : frames) {
            frame->Encode(stream);
        }
        return 0;
    }

    size_t EncodeLen() const override {
        size_t len = 0;
        for (const auto& frame : frames) {
            len += frame->EncodeLen();
        }
        return len;
    }

    const std::list<std::shared_ptr<payload::Frame>>& GetFrames() const {
        return this->frames;
    }

    int AttachFrame(std::shared_ptr<payload::Frame> frame) {
        frames.emplace_back(frame);
        return 0;
    }

    bool IsACKEliciting() const {
        return std::find_if(
                   frames.cbegin(), frames.cend(),
                   [](const std::shared_ptr<payload::Frame>& f) {
                       return (f->Type() != payload::FrameType::ACK) &&
                              (f->Type() !=
                               payload::FrameType::CONNECTION_CLOSE) &&
                              (f->Type() != payload::FrameType::PADDING);
                   }) != frames.cend();
    }

    utils::IntervalSet CollectACKRange() const {
        utils::IntervalSet set;
        for (const auto& frame : frames) {
            if (frame->Type() == FrameType::ACK) {
                set.AddIntervalSet(
                    std::dynamic_pointer_cast<payload::ACKFrame>(frame)
                        ->GetACKRanges());
            }
        }

        return set;
    }

   protected:
    std::list<std::shared_ptr<payload::Frame>> frames;
};

class ShortHeader : public Header {
   public:
    ShortHeader(uint8_t pktNumLen, const ConnectionID& dstConnID,
                uint32_t pktNum)
        : headerForm(0),
          fixedBit(1),
          reservedBits(0),
          pktNumLen(pktNumLen - 1),
          dstConnID(dstConnID),
          pktNum(pktNum) {
        this->spinBit = 0;   // TODO: Implement this
        this->keyPhase = 0;  // TODO: Implement this
    }

    ShortHeader(utils::ByteStream& stream, uint8_t connectionIDLen) {
        auto buf = stream.Consume(1).first;
        this->headerForm = ((*buf & 0x80) >> 7);
        assert(this->headerForm == 0);
        this->fixedBit = ((*buf & 0x40) >> 6);
        assert(this->fixedBit == 1);
        this->spinBit = 0;
        this->reservedBits = ((*buf & 0x18) >> 3);
        assert(this->reservedBits == 0);
        this->keyPhase = 0;
        this->pktNumLen = (*buf & 0x03);

        this->dstConnID = ConnectionID(stream, connectionIDLen);
        this->pktNum = utils::decodeUint(stream, pktNumLen + 1);
    }

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        auto buf = stream.Consume(1).first;
        buf[0] = (this->headerForm << 7) | (this->fixedBit << 6) |
                 (this->spinBit << 5) | (this->reservedBits << 4) |
                 (this->keyPhase << 2) | (this->pktNumLen);
        this->dstConnID.Encode(stream, false);
        utils::encodeUInt(stream, this->pktNum, this->pktNumLen + 1);
        return 0;
    }

    size_t EncodeLen() const override {
        return 1 + this->pktNumLen + 1 + this->dstConnID.EncodeLen(false);
    }

    PacketType Type() const override { return PacketType::ONE_RTT; }

    const ConnectionID& GetDstID() const override { return this->dstConnID; }

    virtual uint64_t GetPktNum() const override { return this->pktNum; }

    bool IsFrameAllowed([[maybe_unused]] FrameType type) const override {
        return true;
    }

   protected:
    uint8_t headerForm : 1;
    uint8_t fixedBit : 1;
    uint8_t spinBit : 1;
    uint8_t reservedBits : 2;
    uint8_t keyPhase : 1;
    uint8_t pktNumLen : 2;
    ConnectionID dstConnID;
    uint32_t pktNum;
};

class LongHeader : public Header {
   public:
    LongHeader(uint32_t version, uint8_t pktNumLen,
               const ConnectionID& srcConnID, const ConnectionID& dstConnID,
               PacketType type)
        : version(version), dstConnID(dstConnID), srcConnID(srcConnID) {
        assert(type != PacketType::RETRY);

        this->headerForm = 1;
        this->fixedBit = 1;
        this->unusedLower = 0;
        this->pktNumLen = pktNumLen - 1;
        switch (type) {
            case PacketType::INITIAL:
                this->packetType = 0;
                break;
            case PacketType::ZERO_RTT:
                this->packetType = 1;
                break;
            case PacketType::HANDSHAKE:
                this->packetType = 2;
                break;
            default:
                throw std::invalid_argument("packet type mismatch");
        }
    }

    explicit LongHeader(utils::ByteStream& stream) {
        auto buf = stream.Consume(1).first;
        this->headerForm = ((*buf & 0x80) >> 7);
        assert(this->headerForm == 1);
        this->fixedBit = ((*buf & 0x40) >> 6);
        assert(this->fixedBit == 1);
        this->packetType = (*buf & 0x30) >> 4;
        this->unusedLower = (*buf & 0x0C) >> 2;
        if (this->packetType == 0x3) {
            this->unusedUpper = (*buf & 0x03);
        } else {
            this->pktNumLen = (*buf & 0x03);
        }

        this->version = utils::decodeUint(stream, sizeof(uint32_t));
        this->dstConnID = ConnectionID(stream);
        this->srcConnID = ConnectionID(stream);
    }

    LongHeader(uint32_t version, const ConnectionID& srcConnID,
               const ConnectionID& dstConnID, PacketType type)
        : version(version), dstConnID(dstConnID), srcConnID(srcConnID) {
        assert(type == PacketType::RETRY);

        this->headerForm = 1;
        this->fixedBit = 1;
        this->packetType = 3;
        this->unusedLower = 0;
        this->unusedUpper = 0;
    }

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        auto buf = stream.Consume(1).first;
        if (this->packetType != 0x3) {
            assert(this->pktNumLen >= 0);
            buf[0] = ((this->headerForm << 7) | (this->fixedBit << 6) |
                      (this->packetType << 4) | (this->unusedLower << 2) |
                      (this->pktNumLen));
        } else {
            buf[0] = ((this->headerForm << 7) | (this->fixedBit << 6) |
                      (this->packetType << 4) | (this->unusedLower << 2) |
                      (this->unusedUpper));
        }
        utils::encodeUInt(stream, this->version, sizeof(uint32_t));
        this->dstConnID.Encode(stream, true);
        this->srcConnID.Encode(stream, true);
        return 0;
    }

    size_t EncodeLen() const override {
        return 5 + this->srcConnID.EncodeLen(true) +
               this->dstConnID.EncodeLen(true);
    }

    const ConnectionID& GetSrcID() const { return this->srcConnID; }

    const ConnectionID& GetDstID() const final { return this->dstConnID; }

    virtual uint64_t GetPktNum() const = 0;

    virtual size_t PayloadLen() const = 0;

   protected:
    uint8_t headerForm : 1;
    uint8_t fixedBit : 1;
    uint8_t packetType : 2;

    union {
        uint8_t reserved : 2;     // For Initial / 0-RTT / Handshake packets
        uint8_t unusedLower : 2;  // For Retry packets
    };

    union {
        uint8_t pktNumLen : 2;    // For Initial / 0-RTT / Handshake packets
        uint8_t unusedUpper : 2;  // For Retry packets
    };

    uint32_t version;
    ConnectionID dstConnID;
    ConnectionID srcConnID;
};

class Initial : public LongHeader {
   public:
    Initial(uint32_t version, uint8_t pktNumLen, const ConnectionID& srcConnID,
            const ConnectionID& dstConnID, uint32_t pktNum)
        : LongHeader(version, pktNumLen, srcConnID, dstConnID,
                     PacketType::INITIAL),
          tokenLength{0},
          token{},
          length{utils::encodeVarIntLen(pktNum)},
          pktNum(pktNum) {}

    explicit Initial(utils::ByteStream& stream) : LongHeader(stream), token{} {
        this->tokenLength = utils::decodeVarInt(stream);
        auto buf = stream.Consume(this->tokenLength);
        std::copy(buf.first, buf.second, std::begin(token));
        this->length = utils::decodeVarInt(stream);
        this->pktNum = utils::decodeUint(stream, this->pktNumLen + 1);
    };

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        this->LongHeader::Encode(stream);
        utils::encodeVarInt(stream, this->tokenLength);
        auto buf = stream.Consume(this->tokenLength).first;
        std::copy(std::cbegin(this->token),
                  std::cbegin(this->token) + this->tokenLength, buf);
        utils::encodeVarInt(stream, this->length);
        utils::encodeUInt(stream, this->pktNum, this->pktNumLen + 1);
        return 0;
    }

    size_t EncodeLen() const override {
        return this->LongHeader::EncodeLen() +
               utils::encodeVarIntLen(this->tokenLength) + this->tokenLength +
               utils::encodeVarIntLen(this->length) + this->pktNumLen + 1;
    }

    PacketType Type() const override { return PacketType::INITIAL; }

    uint64_t GetPktNum() const override { return this->pktNum; }

    size_t PayloadLen() const override {
        printf("this length = %d, pktNumLen = %d\n", this->length, this->pktNumLen);
        return this->length - (this->pktNumLen + 1);
    }

    bool IsFrameAllowed(FrameType type) const override {
        return type == FrameType::PADDING || type == FrameType::PING ||
               type == FrameType::ACK || type == FrameType::CRYPTO;
    }

    size_t GetTokenLength() const { return this->tokenLength; }

    const std::array<uint8_t, 512>& GetToken() const { return this->token; }

   private:
    uint64_t tokenLength;
    // TODO: maximum token length should be a config.
    std::array<uint8_t, 512> token;
    uint64_t length;
    uint32_t pktNum;
};

class Handshake : public LongHeader {
   public:
    Handshake(uint32_t version, uint8_t pktNumLen,
              const ConnectionID& srcConnID, const ConnectionID& dstConnID,
              uint32_t pktNum)
        : LongHeader(version, pktNumLen, srcConnID, dstConnID,
                     PacketType::HANDSHAKE),
          length{utils::encodeVarIntLen(pktNum)},
          pktNum(pktNum) {}

    explicit Handshake(utils::ByteStream& stream) : LongHeader(stream) {
        this->length = utils::decodeVarInt(stream);
        this->pktNum = utils::decodeUint(stream, this->pktNumLen + 1);
    };

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        this->LongHeader::Encode(stream);
        utils::encodeVarInt(stream, this->length);
        utils::encodeUInt(stream, this->pktNum, this->pktNumLen + 1);
        return 0;
    }

    size_t EncodeLen() const override {
        return this->LongHeader::EncodeLen() +
               utils::encodeVarIntLen(this->length) + this->pktNumLen + 1;
    }

    PacketType Type() const override { return PacketType::HANDSHAKE; }

    size_t PayloadLen() const override {
        return this->length - (this->pktNumLen + 1);
    }

    uint64_t GetPktNum() const override { return this->pktNum; }

    bool IsFrameAllowed(FrameType type) const override {
        return type == FrameType::PADDING || type == FrameType::PING ||
               type == FrameType::ACK || type == FrameType::CRYPTO;
    }

   private:
    uint64_t length;
    uint32_t pktNum;
};

class ZeroRTT : public LongHeader {
   public:
    ZeroRTT(uint32_t version, uint8_t pktNumLen, const ConnectionID& srcConnID,
            const ConnectionID& dstConnID, uint32_t pktNum)
        : LongHeader(version, pktNumLen, srcConnID, dstConnID,
                     PacketType::HANDSHAKE),
          length{utils::encodeVarIntLen(pktNum)},
          pktNum(pktNum) {}

    explicit ZeroRTT(utils::ByteStream& stream) : LongHeader(stream) {
        this->length = utils::decodeVarInt(stream);
        this->pktNum = utils::decodeUint(stream, this->pktNumLen + 1);
    };

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        this->LongHeader::Encode(stream);
        utils::encodeVarInt(stream, this->length);
        utils::encodeUInt(stream, this->pktNum, this->pktNumLen + 1);
        return 0;
    }

    size_t EncodeLen() const override {
        return this->LongHeader::EncodeLen() +
               utils::encodeVarIntLen(this->length) + this->pktNumLen + 1;
    }

    PacketType Type() const override { return PacketType::ZERO_RTT; }

    size_t PayloadLen() const override {
        return this->length - (this->pktNumLen + 1);
    }

    uint64_t GetPktNum() const override { return this->pktNum; }

    bool IsFrameAllowed(FrameType type) const override {
        return type != FrameType::ACK && type != FrameType::CRYPTO &&
               type != FrameType::NEW_TOKEN &&
               type != FrameType::HANDSHAKE_DONE;
    }

   private:
    uint64_t length;
    uint32_t pktNum;
};

class Retry : public LongHeader {
   public:
    Retry(uint32_t version, const ConnectionID& srcConnID,
          const ConnectionID& dstConnID, std::unique_ptr<uint8_t[]> retryToken,
          size_t tokenLength, std::array<uint8_t, 16> tag)
        : LongHeader(version, srcConnID, dstConnID, PacketType::HANDSHAKE),
          retryToken{std::move(retryToken)},
          tokenLength{tokenLength},
          retryIntegrityTag{tag} {}

    explicit Retry(utils::ByteStream& stream)
        : LongHeader(stream), retryIntegrityTag{} {
        size_t left = stream.GetFree();
        this->tokenLength = left - 16;
        auto buf = stream.Consume(this->tokenLength);
        this->retryToken = std::make_unique<uint8_t[]>(this->tokenLength);
        std::copy(buf.first, buf.second, retryToken.get());
        buf = stream.Consume(16);
        std::copy(buf.first, buf.second, std::begin(retryIntegrityTag));
    };

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        this->LongHeader::Encode(stream);
        auto buf = stream.Consume(this->tokenLength);
        std::copy(retryToken.get(), retryToken.get() + tokenLength, buf.first);
        buf = stream.Consume(16);
        std::copy(std::begin(retryIntegrityTag), std::end(retryIntegrityTag),
                  buf.first);
        return 0;
    }

    size_t EncodeLen() const override {
        return this->LongHeader::EncodeLen() + this->tokenLength + 16;
    }

    PacketType Type() const override { return PacketType::RETRY; }

    size_t PayloadLen() const override { return 0; }

    // In fact, retry has no Packet Number, so we always return zero.
    // TODO: maybe throw a exception to warn the caller not to get the packet
    // number of a Retry packet.
    uint64_t GetPktNum() const override { return 0; }

    bool IsFrameAllowed([[maybe_unused]] FrameType type) const override {
        return false;
    }

   private:
    std::unique_ptr<uint8_t[]> retryToken;
    size_t tokenLength;
    std::array<uint8_t, 16> retryIntegrityTag;
};

class Packet : Serialization {
   public:
    Packet(std::shared_ptr<Header> header, std::shared_ptr<Payload> payload,
           const struct sockaddr_in& addrDst)
        : header(std::move(header)),
          payload(std::move(payload)),
          addrSrc{},
          addrDst(addrDst) {}

    Packet(utils::ByteStream& stream, const struct sockaddr_in& addrSrc,
           const struct sockaddr_in& addrDst, utils::timepoint timepoint)
        : addrSrc(addrSrc), addrDst(addrDst), recvTimestamp{timepoint} {
        this->header = Header::Parse(stream);
        switch (this->header->Type()) {
            case PacketType::INITIAL:
            case PacketType::ZERO_RTT:
            case PacketType::HANDSHAKE:
                this->payload = std::make_shared<Payload>(
                    stream, std::dynamic_pointer_cast<LongHeader>(header)
                                ->PayloadLen());
                break;
            case PacketType::ONE_RTT:
                this->payload = std::make_shared<Payload>(stream);
                break;
            case PacketType::RETRY:
                break;
        }
    }

    int Encode(utils::ByteStream& stream) override {
        this->CheckBufferRoom(stream);
        this->header->Encode(stream);
        if (this->payload) {
            this->payload->Encode(stream);
        }
        return 0;
    }

    size_t EncodeLen() const override {
        return (this->header ? this->header->EncodeLen() : 0) +
               (this->payload ? this->payload->EncodeLen() : 0);
    }

    uint64_t GetPktNum() const { return header->GetPktNum(); }

    const ConnectionID& GetDstConnID() const { return header->GetDstID(); }

    std::shared_ptr<Header> GetPktHeader() { return this->header; }

    std::shared_ptr<Payload> GetPktPayload() { return this->payload; }

    const struct sockaddr_in& GetAddrSrc() const { return this->addrSrc; }

    const struct sockaddr_in& GetAddrDst() const { return this->addrDst; }

    void MarkSendTimestamp(utils::timepoint sendTime) {
        this->sendTimestamp = sendTime;
    }

    utils::timepoint GetSendTimestamp() const { return this->sendTimestamp; }

    utils::timepoint GetRecvTimestamp() const { return this->recvTimestamp; }

    PacketType GetPacketType() const { return this->header->Type(); }

    int AttachFrames(std::shared_ptr<Frame> frame) {
        payload->AttachFrame(std::move(frame));
        return 0;
    }

    bool IsACKEliciting() const {
        return this->payload && this->payload->IsACKEliciting();
    }

    utils::IntervalSet CollectACKRange() const {
        return this->payload->CollectACKRange();
    }

   private:
    std::shared_ptr<Header> header;
    std::shared_ptr<Payload> payload;

    struct sockaddr_in addrSrc;
    struct sockaddr_in addrDst;

    utils::timepoint sendTimestamp;
    utils::timepoint recvTimestamp;
};

}  // namespace thquic::payload
#endif
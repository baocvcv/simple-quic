#include "quic.hh"

namespace thquic::context {
thquic::context::QUIC::QUIC(thquic::context::PeerType type) : type(type) {
    if (type != PeerType::CLIENT) {
        throw std::invalid_argument("illegal client context config");
    }
    utils::logger::warn("create a QUIC client context");
}

QUIC::QUIC(PeerType type, uint16_t port) : type(type), socket(port) {
    if (type != PeerType::SERVER || port == 0) {
        throw std::invalid_argument("illegal server context config.");
    }
    utils::logger::warn("create a QUIC server context");
}

int QUIC::CloseConnection([[maybe_unused]] uint64_t descriptor,[[maybe_unused]] const std::string& reason,
                        [[maybe_unused]] uint64_t errorCode) {
    return 0;
}

int QUIC::SetConnectionCloseCallback([[maybe_unused]] uint64_t descriptor,
                                     [[maybe_unused]] ConnectionCloseCallbackType callback) {
    return 0;
}

int QUICServer::SetConnectionReadyCallback([[maybe_unused]] ConnectionReadyCallbackType callback) {
    return 0;
}

uint64_t QUICClient::CreateConnection([[maybe_unused]] struct sockaddr_in& addrTo,
                          [[maybe_unused]] const ConnectionReadyCallbackType& callback) {
    return 0;
}

uint64_t QUIC::CreateStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] bool bidirectional) {
    return 0;
}

int QUIC::CloseStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID) {
    return 0;
}

int QUIC::SendData([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                  [[maybe_unused]] std::unique_ptr<uint8_t[]> buf, [[maybe_unused]] size_t len,
                  [[maybe_unused]] bool FIN) {
    return 0;
}

int QUIC::SetStreamReadyCallback([[maybe_unused]] uint64_t descriptor,
                           [[maybe_unused]] StreamReadyCallbackType callback) {
    return 0;
}

int QUIC::SetStreamDataReadyCallback([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                               [[maybe_unused]] StreamDataReadyCallbackType callback) {
    return 0;
}


int QUIC::SocketLoop() {
    for(;;) {
        auto datagram = this->socket.tryRecvMsg(10ms);
        if (datagram) {
            this->incomingMsg(std::move(datagram));
        }

        for (auto& connection : this->connections) {
            auto& pendingPackets = connection.second->GetPendingPackets();
            while (!pendingPackets.empty()) {
                auto newDatagram = QUIC::encodeDatagram(pendingPackets.front());
                this->socket.sendMsg(newDatagram);
                pendingPackets.pop_front();
            }
        }

        std::this_thread::sleep_for(1s);
    }
    return 0;
}

std::shared_ptr<utils::UDPDatagram> QUIC::encodeDatagram(
    const std::shared_ptr<payload::Packet>& pkt) {
    utils::ByteStream stream(pkt->EncodeLen());
    pkt->Encode(stream);
    return std::make_shared<utils::UDPDatagram>(stream, pkt->GetAddrSrc(),
                                                pkt->GetAddrDst(), 0);
}

int QUIC::incomingMsg([[maybe_unused]] std::unique_ptr<utils::UDPDatagram> datagram) {
    /* YOUR CODE HERE */
    return 0;
}

QUICServer::QUICServer(uint16_t port) : QUIC(PeerType::SERVER, port) {}

QUICClient::QUICClient() : QUIC(PeerType::CLIENT) {}

}  // namespace thquic::context
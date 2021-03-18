#ifndef _THQUIC_CONTEXT_QUIC_H_
#define _THQUIC_CONTEXT_QUIC_H_

#include <chrono>
#include <list>
#include <thread>
#include <unordered_map>
#include <utility>
#include <memory>
#include <map>

#include "context/common.hh"
#include "context/callback.hh"
#include "context/connection_id.hh"
#include "context/connection.hh"
#include "payload/packet.hh"
#include "utils/log.hh"
#include "utils/socket.hh"
#include "utils/time.hh"

namespace thquic::context {

class QUIC {
   public:
    explicit QUIC(PeerType type);

    QUIC(PeerType type, uint16_t port);


    int CloseConnection(uint64_t sequence, const std::string& reason,
                        uint64_t errorCode);

    int SetConnectionCloseCallback(uint64_t sequence,
                                   ConnectionCloseCallbackType callback);

    uint64_t CreateStream(uint64_t sequence, bool bidirectional);

    uint64_t CloseStream(uint64_t sequence, uint64_t streamID);

    uint64_t SendData(uint64_t sequence, uint64_t streamID,
                      std::unique_ptr<uint8_t[]> buf, size_t len,
                      bool FIN = false);

    int SetStreamReadyCallback(uint64_t sequence,
                               StreamReadyCallbackType callback);

    int SetStreamDataReadyCallback(uint64_t sequence, uint64_t streamID,
                                   StreamDataReadyCallbackType callback);

    int SocketLoop();

   protected:
    static std::shared_ptr<utils::UDPDatagram> encodeDatagram(
        const std::shared_ptr<payload::Packet>& pkt);

    int incomingMsg(std::unique_ptr<utils::UDPDatagram> datagram);

   protected:
    bool alive{true};
    PeerType type;
    utils::UDPSocket socket;
    std::map<uint64_t, std::shared_ptr<Connection>> connections;

};

class QUICServer : public QUIC {
   public:
    explicit QUICServer(uint16_t port);

    int SetConnectionReadyCallback(ConnectionReadyCallbackType callback);

   private:
    ConnectionReadyCallbackType connectionReadyCallback;
};

class QUICClient : public QUIC {
   public:
    QUICClient();

    uint64_t CreateConnection(struct sockaddr_in& addrTo,
                              const ConnectionReadyCallbackType& callback);
};

}  // namespace thquic::context

#endif

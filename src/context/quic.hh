#ifndef _THQUIC_CONTEXT_QUIC_H_
#define _THQUIC_CONTEXT_QUIC_H_

#include <chrono>
#include <list>
#include <thread>
#include <unordered_map>
#include <utility>
#include <memory>
#include <map>
#include <iostream>
#include <vector>
#include <sys/time.h>
#include <stdlib.h>

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

    /*
     * Close a connection immediately
     * @param descriptor: descriptor of the connection to be closed
     * @param reason: a message sent to the peer explaining why connection is closed
     * @param errorCode: error code sent to the peer
     * @return: error code
     * */
    int CloseConnection(uint64_t descriptor, const std::string& reason,
                        uint64_t errorCode);

    /*
     * Set the callback when a connection is closed by the peer
     * @param descriptor: descriptor of the target connection
     * @param callback: callback function
     * @return error code
     * */
    int SetConnectionCloseCallback(uint64_t descriptor,
                                   ConnectionCloseCallbackType callback);

    /*
     * Create a new stream
     * @param descriptor: descriptor of the target connection
     * @param whether the stream is unidirectional
     * @return stream ID of the created stream
     * */
    uint64_t CreateStream(uint64_t descriptor, bool bidirectional);

    /* End a stream, send all pending data and mark the stream as FIN
     * @param descriptor: descriptor of the target connection
     * @param streamID of the target stream
     * @return errorCode
     * */
    int CloseStream(uint64_t descriptor, uint64_t streamID);

    /* send data to the peer in a certain stream
     * @param descriptor: descriptor of the target connection
     * @param streamID: streamID of the target stream
     * @param buf: the sending buffer
     * @param len: length of the sending buffer
     * @param FIN: whether the stream ends after sending the buffer
     * @return errorCode
     * */
    int SendData(uint64_t descriptor, uint64_t streamID,
                      std::unique_ptr<uint8_t[]> buf, size_t len,
                      bool FIN = false);

    /*
     * set the callback function when a new stream arrives for a certain connection
     * @param descriptor: descriptor of the target connection
     * @param callback: callback function
     * @return errorCode
     * */
    int SetStreamReadyCallback(uint64_t descriptor,
                               StreamReadyCallbackType callback);

    /*
     * set the callback function invoked when receives application data
     * @param descriptor: descriptor of the target connection
     * @param descriptor: stream ID of the target stream
     * @param callback: callback function
     * @return errorCode
     * */
    int SetStreamDataReadyCallback(uint64_t descriptor, uint64_t streamID,
                                   StreamDataReadyCallbackType callback);

    int SetLocalStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID) { return 0; };

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
    std::map<std::shared_ptr<Connection>, std::shared_ptr<LocalConnectionContext> > connectionToLocalConect;
    std::map<std::shared_ptr<Connection>, std::shared_ptr<RemoteConnectionContext> > connectionToRemoteConect;
    // std::map<std::shared_ptr<Connection>, ConnectionID > connectionToID;
    // std::map<std::shared_ptr<Connection>, std::shared_ptr<ConnectionID> > connectionToDstID;
    // std::map<ConnectionID, std::shared_ptr<Connection> > connectionIDToConnection;
    sockaddr_in addrDst;
    StreamReadyCallbackType streamReadyCallback;
    ConnectionReadyCallbackType connectionReadyCallback;
    ConnectionCloseCallbackType connectionCloseCallback;
    StreamDataReadyCallbackType streamDataReadyCallback;
};

class QUICServer : public QUIC {
   public:
    explicit QUICServer(uint16_t port);

    /*
     * Set the callback when server receives a new connection
     * @param callback: callback when the connection is ready
     * @return: error code
     * */
    int SetConnectionReadyCallback(ConnectionReadyCallbackType callback);

   private:
    
};

class QUICClient : public QUIC {
   public:
    QUICClient();

    /*
     * create a new connection
     * @param addrTo: target network address (ip and port)
     * @param callback: callback when the connection is ready
     * @return: descriptor of the created connection
     * */
    uint64_t CreateConnection(struct sockaddr_in& addrTo,
                              const ConnectionReadyCallbackType& callback);
    private:
       // ConnectionReadyCallbackType connectionReadyCallback;
};

}  // namespace thquic::context

#endif

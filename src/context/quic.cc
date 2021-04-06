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
    auto connection = this->connections[descriptor];
    connection->SetConnectionState(ConnectionState::CLOSED);
    // pktNumLen | dstConID | pktNum
    payload::ShortHeader shHdr(3, connection->getRemoteConnectionID(), 0);
    payload::Payload pld;
    std::shared_ptr<payload::ConnectionCloseQUICFrame> ccqFrm = std::make_shared<payload::ConnectionCloseQUICFrame>(errorCode, 18, reason);
    pld.AttachFrame(ccqFrm);
    payload::Packet pkt(std::make_shared<payload::ShortHeader>(shHdr), std::make_shared<payload::Payload>(pld), 
                        connection->GetSockaddrTo());
    connection->AddPacket(std::make_shared<payload::Packet>(pkt));
    
    // Deregister connection: (1) erase connection from this->connections;
    //                        (2) erase local & remote ConnectionID from the usedID;
    auto _it = this->connections.find(descriptor);
    this->connections.erase(_it);
    utils::logger::warn("Deregister connection {}", descriptor);
    ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
    generator.EraseConnectionID(connection->getLocalConnectionID());
    utils::logger::warn("Deregister local connection ID {} for connection {}", 
                            connection->getLocalConnectionID().ToString(), descriptor);    
    return 0;
}

int QUIC::SetConnectionCloseCallback([[maybe_unused]] uint64_t descriptor,
                                     [[maybe_unused]] ConnectionCloseCallbackType callback) {
    this->connectionCloseCallback = callback;
    return 0;
}

int QUICServer::SetConnectionReadyCallback([[maybe_unused]] ConnectionReadyCallbackType callback) {
    this->connectionReadyCallback = callback;
    return 0;
}

uint64_t QUICClient::CreateConnection([[maybe_unused]] struct sockaddr_in& addrTo,
                          [[maybe_unused]] const ConnectionReadyCallbackType& callback) {
    this->addrDst = addrTo;
    this->connectionReadyCallback = callback;
    ConnectionIDGenerator& generator = ConnectionIDGenerator::Get();
    ConnectionID tmpLocalID = generator.Generate();
    ConnectionID expRemoteID = generator.Generate();
    uint64_t connectionDes = Connection::GenerateConnectionDescriptor();
    utils::logger::warn("[connection {}] generate initial connection ID {}", connectionDes, 
                        tmpLocalID.ToString());
    // send package: version | pktNumLen | srcConID | dstConID | pktNum
    std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, tmpLocalID, expRemoteID, connectionDes);
    // payload::Initial initHdr(4, 3, tmpLocalID, expRemoteID, connectionDes);
    utils::ByteStream emptyBys(nullptr, 0);
    // payload::Payload pld(emptyBys, 0);
    std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
    std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, addrTo);
    std::shared_ptr<Connection> connection = std::make_shared<Connection>();
    this->connections[connectionDes] = connection;
    connection->SetSockaddrTo(addrTo);
    connection->AddPacket(pkt);
    connection->SetSrcConnectionID(tmpLocalID);
    connection->SetDstConnectionID(expRemoteID);
    
    return connectionDes;
    // return 0;
}

uint64_t QUIC::CreateStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] bool bidirectional) {
    std::shared_ptr<Connection> streamConnection = this->connections[descriptor];
    uint64_t newStreamID = streamConnection->GenerateStreamID();
    streamConnection->SetStreamFeature(newStreamID, bidirectional);
    return newStreamID;
    // return 0;
}

int QUIC::CloseStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID) {
    std::shared_ptr<Connection> streamConnection = this->connections[descriptor];
    streamConnection->CloseStreamByID(streamID);
    // std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, nullptr, 0, 0, true, true);
    size_t finalSize = streamConnection->GetFinalSizeByStreamID(streamID);
    std::shared_ptr<payload::ResetStreamFrame> fr = std::make_shared<payload::ResetStreamFrame>(streamID, 0, finalSize);
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    const ConnectionID& localConID = connection->getLocalConnectionID();
    // pktNumLen | dstConID | pktNum
    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(1, connection->getRemoteConnectionID(), 0);
    std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
    pl->AttachFrame(fr);
    std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, connection->GetSockaddrTo());
    // ADD pkt to this connection
    this->connections[descriptor]->AddPacket(pk);
    return 0;
}

int QUIC::SendData([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                  [[maybe_unused]] std::unique_ptr<uint8_t[]> buf, [[maybe_unused]] size_t len,
                  [[maybe_unused]] bool FIN) {
    // thquic::utils::ByteStream byteStream(buf.get(), len);
    // stream frame: streamID | unique_ptr<uint8_t[]> buf | bufLen | offset | LEN | FIN
    // thquic::payload::StreamFrame fr(streamID, std::move(buf), len, 0, true, FIN);
    std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, std::move(buf), len, 0, true, FIN);
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    const ConnectionID& localConID = connection->getLocalConnectionID();
    // pktNumLen | dstConID | pktNum
    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(1, 
                                                    connection->getRemoteConnectionID(), 0);
    std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
    pl->AttachFrame(fr);
    std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, connection->GetSockaddrTo());
    // ADD pkt to this connection
    this->connections[descriptor]->AddPacket(pk);
    return 0;
}

int QUIC::SetStreamReadyCallback([[maybe_unused]] uint64_t descriptor,
                           [[maybe_unused]] StreamReadyCallbackType callback) {
    // this->connections[descriptor]->SetStreamReadyCallback(callback);
    this->streamReadyCallback = callback;
    return 0;
}

int QUIC::SetStreamDataReadyCallback([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                               [[maybe_unused]] StreamDataReadyCallbackType callback) {
    // this->connections[descriptor]->SetStreamDataReadyCallbackByStreamID(streamID, callback);
    this->streamDataReadyCallback = callback;
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
    std::unique_ptr<uint8_t[]> buf = datagram->FetchBuffer();
    
    size_t bufLen = datagram->BufferLen();
    utils::ByteStream bstream(std::move(buf), bufLen);
    utils::timepoint tp;
    payload::Packet pkt(bstream, datagram->GetAddrSrc(), datagram->GetAddrDst(), tp);
    std::shared_ptr<payload::Header> hdr = pkt.GetPktHeader();
    std::shared_ptr<payload::Payload> pld = pkt.GetPktPayload();
    
    if (hdr->Type() == payload::PacketType::INITIAL) {
        std::shared_ptr<payload::Initial> initHdr = std::dynamic_pointer_cast<payload::Initial>(hdr);
        const ConnectionID& remoteConID = initHdr->GetSrcID();
        const ConnectionID& localConID = initHdr->GetDstID();
        bool isNewCon = true;
        std::shared_ptr<Connection> foundConnection = nullptr;
        uint64_t descriptor;
        for (auto con: this->connections) {
            if (con.second->getRemoteConnectionID() == remoteConID) {
                isNewCon = false;
                descriptor = con.first;
                foundConnection = con.second;
                break;
            }
        }       
        if (isNewCon) {
            // A new Connection: (1) create a one locally; (2) crypto; (3) ACK and Crypto
            uint64_t conDes = Connection::GenerateConnectionDescriptor();
            ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
            ConnectionID expRemoteID = generator.Generate();
            std::shared_ptr<Connection> connection = std::make_shared<Connection>();
            connection->SetSockaddrTo(datagram->GetAddrSrc());
            connection->SetSrcConnectionID(localConID);
            utils::logger::warn("[Connection {}] allocate local ID {}", conDes, localConID.ToString());
            connection->SetDstConnectionID(expRemoteID);
            utils::logger::warn("[Connection {}] peer ID exchanged,local: {}, remote: {}", localConID.ToString(), remoteConID.ToString());
            this->connections[conDes] = connection;
            // payload::Initial initHdr;
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, expRemoteID, conDes);
            utils::ByteStream emptyBys(nullptr, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            connection->AddPacket(pkt);
            // NO reliabel transmission is garuanteed here, so set the state to ESTABLISED now
            connection->SetConnectionState(ConnectionState::ESTABLISHED);
            this->connectionReadyCallback(conDes);
        } else if (foundConnection->GetConnectionState() == ConnectionState::CREATED) {
            // reply from remote to the connection created locally: (1) Cropto; (2) ConnectionID; (3) ACK
            assert(foundConnection->getRemoteConnectionID() == remoteConID);
            foundConnection->SetSrcConnectionID(localConID);
            ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
            generator.AddUsedConnectionID(localConID);
            utils::logger::warn("[Connection 0] peer ID exchanged,local: {}, remote: {}", localConID.ToString(), remoteConID.ToString());
            // NO reliable transmission, set to ESTABLISHED now
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
        } else {
            // Only one ACK payload.
        }
    } else if (hdr->Type() == payload::PacketType::ONE_RTT) {
        utils::logger::warn("Receive a ONE-RTT package! Going to get frames from it!\n");
        std::shared_ptr<payload::ShortHeader> shHdr = std::dynamic_pointer_cast<payload::ShortHeader>(hdr);
        auto frames = pld->GetFrames();
        utils::logger::info("Got frames, number = {}", frames.size());
        for (auto frm: frames) {
            if (frm->Type() == payload::FrameType::STREAM) {
                // STREAM Frame
                std::shared_ptr<payload::StreamFrame> streamFrm = std::dynamic_pointer_cast<payload::StreamFrame>(frm);
                uint64_t streamID = streamFrm->StreamID();
                const ConnectionID& localConID = shHdr->GetDstID();
                std::shared_ptr<Connection> foundCon = nullptr;
                uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->getLocalConnectionID() == localConID) {
                        foundCon = con.second;
                        conSeq = con.first;
                        break;
                    }
                }
                assert(foundCon != nullptr);
                if (foundCon->IsStreamIDUsed(streamID) == false) {
                    // Stream NOT used
                    foundCon->InsertStream(streamID, true);
                    // Stream READY
                    this->streamReadyCallback(conSeq, streamID);
                }
                auto buf = streamFrm->FetchBuffer();
                size_t buflen = streamFrm->GetLength();
                uint8_t fin = streamFrm->FINFlag();
                this->streamDataReadyCallback(conSeq, streamID, std::move(buf), buflen, (bool)fin);
            } else if (frm->Type() == payload::FrameType::RESET_STREAM) {
                std::shared_ptr<payload::ResetStreamFrame> rstStrFrm = std::dynamic_pointer_cast<payload::ResetStreamFrame>(frm);
                uint64_t errorCode = rstStrFrm->GetAppProtoErrCode();
                uint64_t finalSize = rstStrFrm->GetFinalSize();
                const ConnectionID& localConID = shHdr->GetDstID();
                uint64_t streamID = rstStrFrm->StreamID();
                std::shared_ptr<Connection> foundCon = nullptr;
                uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->getLocalConnectionID() == localConID) {
                        foundCon = con.second;
                        conSeq = con.first;
                        break;
                    }
                }
                assert(foundCon != nullptr);
                utils::logger::warn("Receive a STREAM_RESET frame, with errorCode = {}, finalSize = {}.", 
                                    errorCode, finalSize);
                // seq | streamID | buf | bufLen | fin
                this->streamDataReadyCallback(conSeq, streamID, nullptr, 0, true);

            } else if (frm->Type() == payload::FrameType::CONNECTION_CLOSE) {
                std::shared_ptr<payload::ConnectionCloseQUICFrame> ccqFrm = std::dynamic_pointer_cast<payload::ConnectionCloseQUICFrame>(frm);
                uint64_t errorCode = ccqFrm->GetErrorCode();
                std::string reason = ccqFrm->GetReasonPhrase();
                const ConnectionID& localConID = shHdr->GetDstID();
                std::shared_ptr<Connection> foundCon = nullptr;
                uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->getLocalConnectionID() == localConID) {
                        foundCon = con.second;
                        conSeq = con.first;
                        break;
                    }
                }
                assert(foundCon != nullptr);
                
                foundCon->SetConnectionState(ConnectionState::CLOSED);
                utils::logger::warn("Receive CONNECTION_CLOSE frame, transition to DRAIN state");
                this->connectionCloseCallback(conSeq, reason, errorCode);
                // registter?? --- what's this?
            }
        }
    }
    return 0;
}

QUICServer::QUICServer(uint16_t port) : QUIC(PeerType::SERVER, port) {}

QUICClient::QUICClient() : QUIC(PeerType::CLIENT) {}

}  // namespace thquic::context
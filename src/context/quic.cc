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
    // pktNumLen | dstConID | pktNum --- does set pktNum to 0 correct?
    payload::ShortHeader shHdr(3, connection->getRemoteConnectionID(), 0);
    // uint64_t errorCode, uint64_t frameType, const std::string& reason
    // payload::ConnectionCloseQUICFrame ccqFrm(errorCode, 18, reason);
    payload::Payload pld;
    std::shared_ptr<payload::ConnectionCloseQUICFrame> ccqFrm = std::make_shared<payload::ConnectionCloseQUICFrame>(errorCode, 18, reason);
    // pld.AttachFrame(std::make_shared<payload::ConnectionCloseQUICFrame>(ccqFrm));
    pld.AttachFrame(ccqFrm);
    payload::Packet pkt(std::make_shared<payload::ShortHeader>(shHdr), std::make_shared<payload::Payload>(pld), 
                        connection->GetSockaddrTo());
    connection->AddPacket(std::make_shared<payload::Packet>(pkt));
    
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
    utils::logger::warn("[connection %d] generate initial connection ID %s", connectionDes, 
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
    // TODO: any possible error?
    std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, nullptr, 0, 0, true, true);
    printf("In close stream function 2\n");
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    const ConnectionID& localConID = connection->getLocalConnectionID();
    // pktNumLen | dstConID | pktNum
    // payload::ShortHeader shHdr(3, connection->getRemoteConnectionID(), 0);
    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(1, connection->getRemoteConnectionID(), 0);
    // thquic::payload::Payload pl;
    std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
    pl->AttachFrame(fr);
    // thquic::payload::Packet pk(shHdr, pl, connection->GetSockaddrTo());
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
    printf("In Sed data function 1; buflen = %d\n", len);
    std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, std::move(buf), len, 0, true, FIN);
    printf("In Sed data function 2\n");
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    const ConnectionID& localConID = connection->getLocalConnectionID();
    // pktNumLen | dstConID | pktNum
    // payload::ShortHeader shHdr(3, connection->getRemoteConnectionID(), 0);
    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(1, connection->getRemoteConnectionID(), 0);
    // thquic::payload::Payload pl;
    std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
    pl->AttachFrame(fr);
    // thquic::payload::Packet pk(shHdr, pl, connection->GetSockaddrTo());
    std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, connection->GetSockaddrTo());
    // ADD pkt to this connection
    this->connections[descriptor]->AddPacket(pk);
    // TODO: any possible error?
    return 0;
}

int QUIC::SetStreamReadyCallback([[maybe_unused]] uint64_t descriptor,
                           [[maybe_unused]] StreamReadyCallbackType callback) {
    // called when a connection is created? --- whether created itself or received from the other end?
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

// Stream ready callback function
/*
int QUIC::SetLocalStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID) {
    auto nowConnection = this->connections[descriptor];
    auto insertRes = nowConnection->InsertStream(streamID, false); // streamID | bidirectional
    if (insertRes == -1) {
        return -1; // insert error
    }
    // version | pktNumLen | scrConnectionID | dstConnectionID | pktNum
    thquic::payload::Initial initPktForNewStream(4, 32, this->connectionToID[nowConnection], 
                                                    this->connectionToDstID[nowConnection], 0);
    thquic::utils::IntervalSet intervalSet;
    intervalSet.AddInterval(1, 40);
    thquic::payload::ACKFrame ackFrame(1, intervalSet); // ACKDelay | ACKRange ???
    thquic::payload::Packet ackPktForNewStream(std::make_shared<payload::Header>(initPktForNewStream), 
                                                    std::make_shared<payload::Payload>(ackFrame), 
                                                    this->addrDst);
    nowConnection->AddPacket(std::make_shared<payload::Packet>(ackPktForNewStream));
    return 0;
}*/


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
                printf("Send message!\n");
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
    printf("Incomming message!\n");
    std::unique_ptr<uint8_t[]> buf = datagram->FetchBuffer();
    
    size_t bufLen = datagram->BufferLen();
    printf("buflen = %d\n", bufLen);
    utils::ByteStream bstream(std::move(buf), bufLen);
    printf("here 1");
    utils::timepoint tp;
    printf("heer 2\n");
    payload::Packet pkt(bstream, datagram->GetAddrSrc(), datagram->GetAddrDst(), tp);
    printf("here 3\n");
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
            // utils::logger::warn("[Connection %d] receive connection from %s:%d", conDes, datagram->GetAddrSrc().sin_addr, datagram->GetAddrSrc().sin_port);
            ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
            ConnectionID expRemoteID = generator.Generate();
            std::shared_ptr<Connection> connection = std::make_shared<Connection>();
            connection->SetSockaddrTo(datagram->GetAddrSrc());
            connection->SetSrcConnectionID(localConID);
            utils::logger::warn("[Connection %d] allocate local ID %s", conDes, localConID.ToString());
            connection->SetDstConnectionID(expRemoteID);
            utils::logger::warn("[Connection 0] peer ID exchanged,local: %s, remote: %s", localConID.ToString(), remoteConID.ToString());
            this->connections[conDes] = connection;
            // ADD the package to the created connection and set remote conenction ID and local connection ID
            // payload::Initial initHdr;
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, expRemoteID, conDes);
            utils::ByteStream emptyBys(nullptr, 0);
            // payload::Payload pld(emptyBys, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            // payload::Packet pkt();
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
            utils::logger::warn("[Connection 0] peer ID exchanged,local: %s, remote: %s", localConID.ToString(), remoteConID.ToString());
            // NO reliable transmission, set to ESTABLISHED now
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
        } else {
            // Only one ACK payload.
        }
    } else if (hdr->Type() == payload::PacketType::ONE_RTT) {
        printf("Receive a one-RTT package! Going to get frames from it!\n");
        std::shared_ptr<payload::ShortHeader> shHdr = std::dynamic_pointer_cast<payload::ShortHeader>(hdr);
        auto frames = pld->GetFrames();
        printf("Got frames, number = %d", frames.size());
        for (auto frm: frames) {
            if (frm->Type() == payload::FrameType::STREAM) {
                // STREAM Frame
                printf("Got a STREAM frame!\n");
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
                printf("buflen = %d\n", buflen);
                this->streamDataReadyCallback(conSeq, streamID, std::move(buf), buflen, (bool)fin);
                
                // create a new stream
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
                utils::logger::warn("receive CONNECTION_CLOSE frame, transition to DRAIN state");
                this->connectionCloseCallback(conSeq, reason, errorCode);
                // registter?? --- what's this?
            }
        }
    }

    /*
    switch (hdr->Type())
    {
    case payload::PacketType::INITIAL :
        if (initHdr->PayloadLen() == 0) {
            // connection init
            this->addrDst = datagram->GetAddrSrc(); // get dest addr
            pktNum = initHdr->GetPktNum(); // pkt Num = sequence = descriptor
            // self connection ID
            std::shared_ptr<ConnectionID> srcConID = std::make_shared<ConnectionID>(initHdr->GetDstID());
            Connection connection;
            connection.SetSrcConnectionID(srcConID);
            std::shared_ptr<ConnectionID> dstConID = std::make_shared<ConnectionID>(initHdr->GetSrcID());
            connection.SetDstConnectionID(dstConID);
            std::shared_ptr<Connection> conPtr = std::make_shared<Connection>(connection);
            this->connectionToID[conPtr] = srcConID;
            this->connectionToDstID[conPtr] = dstConID;
            ConnectionIDGenerator cg = ConnectionIDGenerator::Get();
            cg.AddUsedConnectionID(initHdr->GetDstID());
            cg.AddUsedConnectionID(initHdr->GetSrcID()); // add to used connection id set
            payload::Initial ackInitHdr(4, 64, initHdr->GetDstID(), initHdr->GetSrcID(), pktNum);
            utils::IntervalSet ackInterval;
            ackInterval.AddInterval(0, 1);
            payload::ACKFrame ackFr(0, ackInterval);
            payload::Packet pkt(std::make_shared<payload::Header>(ackInitHdr), 
                                std::make_shared<payload::Payload>(ackFr), this->addrDst);
            connection.AddPacket(std::make_shared<payload::Packet>(pkt)); // add pkt to connection
            payload::ShortHeader shHdr(64, initHdr->GetSrcID(), pktNum);
            uint16_t streamID = this->CreateStream(pktNum, true);
            
            payload::StreamFrame strFrm(streamID, nullptr, 0, 0, false, false);
            payload::Packet pkt(std::make_shared<payload::Header>(shHdr), 
                                std::make_shared<payload::Payload>(strFrm), this->addrDst);
            connection.AddPacket(std::make_shared<payload::Packet>(pkt));
        }
        break;
    case payload::PacketType::ONE_RTT:
        auto addrDst = datagram->GetAddrDst();
        auto addrSrc = datagram->GetAddrSrc();
        payload::Packet pkt(bstream, addrSrc, addrDst, 111);
        auto pld = pkt.GetPktPayload();
        for (auto frm: pld->GetFrames()) {
            if (frm->Type() == payload::FrameType::STREAM) {
                auto streamFrm = std::dynamic_pointer_cast<payload::StreamFrame>(frm);
                uint64_t streamID = streamFrm->StreamID();
                ConnectionID myConnectionID = shtHdr->GetDstID();
                auto connection = this->connectionIDToConnection[myConnectionID];
                connection->InsertStream(streamID, true);
            }
        }
    
    default:
        break;
    }
    */

    return 0;
}

QUICServer::QUICServer(uint16_t port) : QUIC(PeerType::SERVER, port) {}

QUICClient::QUICClient() : QUIC(PeerType::CLIENT) {}

}  // namespace thquic::context
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
    // connection->SetConnectionState(ConnectionState::CLOSED);
    uint64_t _usePktNum = connection->GetNewPktNum();
    // pktNumLen | dstConID | pktNum
    payload::ShortHeader shHdr(3, connection->getRemoteConnectionID(), _usePktNum);
    payload::Payload pld;
    std::shared_ptr<payload::ConnectionCloseQUICFrame> ccqFrm = std::make_shared<payload::ConnectionCloseQUICFrame>(errorCode, 18, reason);
    pld.AttachFrame(ccqFrm);
    payload::Packet pkt(std::make_shared<payload::ShortHeader>(shHdr), std::make_shared<payload::Payload>(pld), 
                        connection->GetSockaddrTo());
    connection->AddPacket(std::make_shared<payload::Packet>(pkt));
    // CloseConnection function will only be called by the active end.
    connection->AddWhetherNeedACK(true);
    
    // Deregister connection: (1) erase connection from this->connections;
    //                        (2) erase local & remote ConnectionID from the usedID;
    // auto _it = this->connections.find(descriptor);
    // this->connections.erase(_it);
    this->connections[descriptor]->SetConnectionState(ConnectionState::WAIT_FOR_PEER_CLOSE);
    connection->SetWaitForPeerConCloseACKPktNum(_usePktNum);
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
    
    std::shared_ptr<Connection> connection = std::make_shared<Connection>();

    uint64_t _usePktNum = connection->GetNewPktNum();
    std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, 
                                                tmpLocalID, expRemoteID, _usePktNum);
    // payload::Initial initHdr(4, 3, tmpLocalID, expRemoteID, connectionDes);
    utils::ByteStream emptyBys(nullptr, 0);
    // payload::Payload pld(emptyBys, 0);
    std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
    std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, addrTo);

    this->connections[connectionDes] = connection;
    connection->Initial();
    connection->SetSockaddrTo(addrTo);
    connection->AddPacket(pkt);
    connection->AddWhetherNeedACK(true);
    connection->SetSrcConnectionID(tmpLocalID);
    connection->SetDstConnectionID(expRemoteID);
    
    return connectionDes;
    // return 0;
}

uint64_t QUIC::CreateStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] bool bidirectional) {
    std::shared_ptr<Connection> streamConnection = this->connections[descriptor];
    uint64_t newStreamID = streamConnection->GenerateStreamID(type, bidirectional);
    streamConnection->SetStreamFeature(newStreamID, bidirectional);
    return newStreamID;
    // return 0;
}

int QUIC::CloseStream([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID) {
    // std::shared_ptr<Connection> streamConnection = this->connections[descriptor];
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    
    // std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, nullptr, 0, 0, true, true);
    size_t finalSize = connection->GetFinalSizeByStreamID(streamID);
    std::shared_ptr<payload::ResetStreamFrame> fr = std::make_shared<payload::ResetStreamFrame>(streamID, 0, finalSize);
    
    // const ConnectionID& localConID = connection->getLocalConnectionID();
    uint64_t _usePktNum = connection->GetNewPktNum();
    // pktNumLen | dstConID | pktNum
    uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                            connection->getRemoteConnectionID(), _usePktNum);
    std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
    pl->AttachFrame(fr);
    std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, connection->GetSockaddrTo());
    // ADD pkt to this connection
    connection->AddPacket(pk);
    // ACTIVE end to close the stream --- need ack
    if (connection->GetStreamStateByID(streamID) != StreamState::FIN) {
        utils::logger::info("Closing stream which need ack with packet number = {}", _usePktNum);
        connection->AddWhetherNeedACK(true);
    } else {
        connection->AddWhetherNeedACK(true);
    }
    connection->CloseStreamByID(streamID);
    return 0;
}

int QUIC::SendData([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                  [[maybe_unused]] std::unique_ptr<uint8_t[]> buf, [[maybe_unused]] size_t len,
                  [[maybe_unused]] bool FIN) {
    // thquic::utils::ByteStream byteStream(buf.get(), len);
    // stream frame: streamID | unique_ptr<uint8_t[]> buf | bufLen | offset | LEN | FIN
    // thquic::payload::StreamFrame fr(streamID, std::move(buf), len, 0, true, FIN);
    std::shared_ptr<Connection> connection = this->connections[descriptor];
    std::shared_ptr<payload::Packet> pk = nullptr;
    utils::logger::info("Sending data, totlen = {}", len);
    if (len <= MAX_PACKET_DATA_LENGTH) {
        uint64_t nowOffset = connection->GetStreamByID(streamID).GetUpdateOffset(len);
        std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(streamID, std::move(buf), len, nowOffset, true, FIN);
        
        // const ConnectionID& localConID = connection->getLocalConnectionID();
        uint64_t _usePktNum = connection->GetNewPktNum();
        utils::logger::info("Sending data with packet numbeer = {}, len = {}. fin = {}", _usePktNum, 
                            len, FIN);
        uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
        // pktNumLen | dstConID | pktNum
        std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                                        connection->getRemoteConnectionID(), _usePktNum);
        std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
        pl->AttachFrame(fr);
        pk = std::make_shared<payload::Packet>(shHdr, pl, connection->GetSockaddrTo());
    } else {
        connection->AddToUnsentBuf(std::move(buf), streamID, len, FIN);
        pk = connection->GetPktFromUnsentBuf();
    }
    // ADD pkt to this connection
    this->connections[descriptor]->AddPacket(pk);
    // ADD needACK property to the added pending packet
    this->connections[descriptor]->AddWhetherNeedACK(true);
    return 0;
}

int QUIC::SetStreamReadyCallback([[maybe_unused]] uint64_t descriptor,
                           [[maybe_unused]] StreamReadyCallbackType callback) {
    // this->connections[descriptor]->SetStreamReadyCallback(callback);
    utils::logger::info("Setting stream ready callback function.");
    this->streamReadyCallback = callback;
    return 0;
}

int QUIC::SetStreamDataReadyCallback([[maybe_unused]] uint64_t descriptor, [[maybe_unused]] uint64_t streamID,
                               [[maybe_unused]] StreamDataReadyCallbackType callback) {
    // this->connections[descriptor]->SetStreamDataReadyCallbackByStreamID(streamID, callback);
    this->streamDataReadyCallback = callback;
    return 0;
}

/*
- ACK scheme: (1) Need to send an ACKFrame for a received packet that needs to ACK back. --- Add to the notACKedRecPkt list and form a ACKFrame when 
              (2) When sending packets, append an ACK frame at the end of the packet's payload if we have not ACKed packets.
              (3) When appending a packet at the end of the connection's pending packets, append whether this packet need to ACK.
              (4) When sending a packet, if it needs to ACK, add it to the NotACKedSentPkt list.
              (5) Check if there existing sent packets whose ACK have been timeout and add such packets to the pending packets.
- Disordered stream frame: (1) Set the offset of the sent stream frame;
                           (2) Update the offset value of the stream;
                           (3) When receiving a stream, check if it should be uploaded to the upper end;
                           (4) If true, try to upload the buffered stream frames; 
                           (5) If not, add this buffer to the buffered stream (and other related information).
*/
// 
int QUIC::SocketLoop() {
    for(;;) {
        srand((unsigned int)(time(nullptr)));
        auto datagram = this->socket.tryRecvMsg(10ms);
        while (datagram) {
            this->incomingMsg(std::move(datagram));
            datagram = this->socket.tryRecvMsg(10ms);
        }
        // if (datagram) {
        //     this->incomingMsg(std::move(datagram));
        // }
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t current_time = curTime.tv_sec * 1000 + curTime.tv_usec / 1000;


        // send packages
        for (auto& connection : this->connections) {
            // utils::logger::info("Now going to print the sent but not acked packets and rec but not acked packets");
            
            // connection.second->PrintSentNeedACKPktNum();
            // connection.second->PrintRecNotACKPktNum();
            auto& pendingPackets = connection.second->GetPendingPackets();
            int sentWhenCongested = 0;

            // deal with if PTO expired
            bool PTO_expired = false;
            for(auto& connection : this->connections) {
                std::list<PTOTimer> connection_PTO_timers = connection.second->GetPTOTimers();
                for(auto PTO_timer : connection_PTO_timers) {
                    if(current_time > PTO_timer.PTO_expire_time) {
                        PTO_expired = true;
                        connection.second->updatePTO();
                    }
                }
            }
            utils::logger::info("PTO_expired = {} for localConnectionID = {} and remoteConnectionID = {}",PTO_expired,connection.second->getLocalConnectionID().ToString(),connection.second->getRemoteConnectionID().ToString());                
            

            // if nothing to send, send a packet with only a ping frame
            if (pendingPackets.empty()) {
                utils::logger::info("PendingPackets is emtpy, check the connectionState = {}",connection.second->GetConnectionState());                
                if(connection.second->GetConnectionState() != ConnectionState::CREATED &&
                    connection.second->GetConnectionState() != ConnectionState::WAIT_FOR_PEER_CLOSE &&
                    connection.second->GetConnectionState() != ConnectionState::CLOSED) {
                // send a Ping Frame
                    uint64_t _usePktNum = connection.second->GetNewPktNum();
                    utils::logger::info("Sending ping frame packet with packet numbeer = {}, DstID = {}",_usePktNum,connection.second->getRemoteConnectionID().ToString());
                    uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
                    // pktNumLen | dstConID | pktNum
                    std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                                                connection.second->getRemoteConnectionID(), _usePktNum);
                    utils::ByteStream emptyBys(nullptr, 0);
                    std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
                    std::shared_ptr<payload::PingFrame> pingFrm = std::make_shared<payload::PingFrame>();
                    pld->AttachFrame(pingFrm);
                    std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pld, connection.second->GetSockaddrTo());
                    auto newDatagram = QUIC::encodeDatagram(pk);
                    this->socket.sendMsg(newDatagram);
                    connection.second->UpdateOnFlightBySentPktLen(pk->EncodeLen());
                }
            }

            // SEND regular packets
            // utils::logger::info("Number of pending packets = {}", pendingPackets.size());
            int explicit_ack_packet_sent = 0;
            while (!pendingPackets.empty()) {
                // Have been ACKed --- need not to send again
                if (connection.second->WhetherAckedPktNum(pendingPackets.front()->GetPktNum())) {
                    connection.second->RemoveToSendPktNum(pendingPackets.front()->GetPktNum());
                    pendingPackets.pop_front();
                    connection.second->PopWhetherNeedACK();
                    continue;
                }
                // IF cannot be sent due to the limitation by the current congestionWindow, break;
                bool _whetherCanSend = connection.second->WhetherCanSendPkt(pendingPackets.front()->EncodeLen());
                if (connection.second->GetPendingPackageNeedACK() == true && 
                    !_whetherCanSend && 
                    sentWhenCongested >= 2) {
                    
                    // utils::logger::info("Need ack but cannot be sent. pktnum = {}, encodlen = {}", 
                    //                     pendingPackets.front()->GetPktNum(), 
                    //                     pendingPackets.front()->EncodeLen());
                    break;
                } else if (connection.second->GetPendingPackageNeedACK() == true && 
                    !_whetherCanSend) {
                        sentWhenCongested += 1;
                        utils::logger::info("Sending packet when congested.");
                    }
                if (connection.second->GetPendingPackageNeedACK() == true) {
                    connection.second->addNeedACKSentPkt(pendingPackets.front());
                    explicit_ack_packet_sent++;
                    if(PTO_expired)
                        break;
                } else {
                    if(!PTO_expired)
                        connection.second->addSentPkt(pendingPackets.front());
                    else
                        break;                    
                }

                if(PTO_expired) {
                    if(explicit_ack_packet_sent > 0) {
                        assert(explicit_ack_packet_sent==1);                        
                        auto newDatagram = QUIC::encodeDatagram(pendingPackets.front());
                        if (rand() % 10 < 8)
                            this->socket.sendMsg(newDatagram);
                        connection.second->RemoveToSendPktNum(pendingPackets.front()->GetPktNum());
                        // UPDATE this connection's onFlight packet length;
                        if (connection.second->GetPendingPackageNeedACK() == true)
                            connection.second->UpdateOnFlightBySentPktLen(pendingPackets.front()->EncodeLen());
                        pendingPackets.pop_front();
                        connection.second->PopWhetherNeedACK();
                    }
                    else {
                        assert(explicit_ack_packet_sent==0);
                        // send a Ping Frame
                        uint64_t _usePktNum = connection.second->GetNewPktNum();
                        utils::logger::info("Sending ping frame packet with packet numbeer= {}, as the pendingPackets.front() is not explicit ack, DstID = {}",_usePktNum,connection.second->getRemoteConnectionID().ToString());
                        uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
                        // pktNumLen | dstConID | pktNum
                        std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                                                    connection.second->getRemoteConnectionID(), _usePktNum);
                        utils::ByteStream emptyBys(nullptr, 0);
                        std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
                        std::shared_ptr<payload::PingFrame> pingFrm = std::make_shared<payload::PingFrame>();
                        pld->AttachFrame(pingFrm);
                        std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pld, connection.second->GetSockaddrTo());
                        auto newDatagram = QUIC::encodeDatagram(pk);
                        this->socket.sendMsg(newDatagram);
                        connection.second->UpdateOnFlightBySentPktLen(pk->EncodeLen());
                    }
                }
                else {
                    // GET ack frame for the connection --- the list is then cleared ---- also reasonable...
                    // std::shared_ptr<payload::ACKFrame> _ackRecFrm = connection.second->GetACKFrameForRecPackages();
                    // If still having packages that need ACK --- attach an ack frame to the sending package.
                    // if (_ackRecFrm != nullptr)
                    //     pendingPackets.front()->GetPktPayload()->AttachFrame(_ackRecFrm);
                    auto newDatagram = QUIC::encodeDatagram(pendingPackets.front());
                    if (rand() % 10 < 8)
                        this->socket.sendMsg(newDatagram);
                    connection.second->RemoveToSendPktNum(pendingPackets.front()->GetPktNum());
                    // UPDATE this connection's onFlight packet length;
                    if (connection.second->GetPendingPackageNeedACK() == true)
                        connection.second->UpdateOnFlightBySentPktLen(pendingPackets.front()->EncodeLen());
                    pendingPackets.pop_front();
                    connection.second->PopWhetherNeedACK();
                }
            }

            if (connection.second->GetConnectionState() == ConnectionState::CLOSED) {
                continue;
            }

            // SEND those packets that must be sent for ACK
            // 
            std::shared_ptr<payload::Packet> mustACKPkt = connection.second->GetSpeACKPacketForRecPkt();
            if (mustACKPkt != nullptr) {
                auto newDatagram = QUIC::encodeDatagram(mustACKPkt);
                if (rand() % 10 < 8)
                    this->socket.sendMsg(newDatagram);
            }

            // ADD packets that need to be re-transmitted
            struct timeval curTime;
            gettimeofday(&curTime, nullptr);
            // uint64_t makes sure this is a positive number
            uint64_t msec = curTime.tv_sec * 1000 + curTime.tv_usec / 1000;

            // /* add needResendPkts to the end of pendingPackets */
            // auto notAckedSentPkt = connection.second->GetNotACKedSentPkt();
            // uint64_t _needACKIdx = 0;
            // std::list<ACKTimer> newNeedACK;
            // newNeedACK.clear();
            // utils::IntervalSet ackedPktNum;
            // for (auto _needACKStPkt: notAckedSentPkt) { // For those not acked packages.
            //     if ((msec - _needACKStPkt.remTime > MAX_RTT) && 
            //         (!ackedPktNum.Contain(_needACKStPkt.pktNum))) {
            //         ackedPktNum.AddInterval(_needACKStPkt.pktNum, _needACKStPkt.pktNum);
            //         // utils::logger::info("not acked packet.. msec = {}, remTime = {}", msec, _needACKStPkt.remTime);
            //         pendingPackets.push_back(connection.second->GetSentPktByIdx(_needACKStPkt.idx));
            //         connection.second->AddWhetherNeedACK(true);
            //     } else {
            //         // utils::logger::info("tout not acked packet.. msec = {}, remTime = {}", msec, _needACKStPkt.remTime);
            //         newNeedACK.push_back(ACKTimer{_needACKStPkt.pktNum, msec, _needACKStPkt.idx});
            //     }
            // }
            // notAckedSentPkt.clear();
            // notAckedSentPkt = newNeedACK;

            /* add needResendPkts to the front of pendingPackets */
            auto notAckedSentPkt = connection.second->GetNotACKedSentPkt();
            // uint64_t _needACKIdx = 0;
            std::list<ACKTimer> newNeedACK;
            newNeedACK.clear();
            utils::IntervalSet ackedPktNum;
            utils::logger::info("Number of not acked sent packets = {}", notAckedSentPkt.size());
            for (auto _needACKStPkt: notAckedSentPkt) { // For those not acked packages.
                if ( // (msec - _needACKStPkt.remTime > connection.second->getConnectionRTT()) && 
                    (msec - _needACKStPkt.remTime > 500) && 
                    (!ackedPktNum.Contain(_needACKStPkt.pktNum)) && 
                    (!connection.second->WhetherToSendPktNum(_needACKStPkt.pktNum))) {
                    ackedPktNum.AddInterval(_needACKStPkt.pktNum, _needACKStPkt.pktNum);
                    // utils::logger::info("not acked packet.. msec = {}, remTime = {}", msec, _needACKStPkt.remTime);
                    connection.second->AddPacket(connection.second->GetSentPktByIdx(_needACKStPkt.idx));
                    // pendingPackets.push_back(connection.second->GetSentPktByIdx(_needACKStPkt.idx));
                    // RETRANSMITTED packets need no ack: they should not be added to the notACKedSentPkt again;
                    connection.second->AddWhetherNeedACK(true);
                } else {
                    // utils::logger::info("tout not acked packet.. msec = {}, remTime = {}", msec, _needACKStPkt.remTime);
                    newNeedACK.push_front(ACKTimer{_needACKStPkt.pktNum, msec, _needACKStPkt.idx, _needACKStPkt.pktLen});
                }
            }
            notAckedSentPkt.clear();
            notAckedSentPkt = newNeedACK;

            // Get unsend pakcets.
            std::shared_ptr<payload::Packet> toSendPkt = connection.second->GetPktFromUnsentBuf();
            if (toSendPkt != nullptr) {
                connection.second->AddPacket(toSendPkt);
                connection.second->AddWhetherNeedACK(true);
            }
        }
        std::this_thread::sleep_for(100ms);
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
    std::shared_ptr<payload::Packet> recPkt = std::make_shared<payload::Packet>(bstream, datagram->GetAddrSrc(), datagram->GetAddrDst(), tp);
    
    // payload::Packet pkt(bstream, datagram->GetAddrSrc(), datagram->GetAddrDst(), tp);
    std::shared_ptr<payload::Header> hdr = recPkt->GetPktHeader();
    std::shared_ptr<payload::Payload> recPld = recPkt->GetPktPayload();
    uint64_t _recPktNum = recPkt->GetPktNum();
    bool _recPktAdded = false;

    switch (hdr->Type())
    {
    case(payload::PacketType::INITIAL): {
        // utils::logger::info("Receive a INITIAL packet with packet number = {}", _recPktNum);
        std::shared_ptr<payload::Initial> initHdr = std::dynamic_pointer_cast<payload::Initial>(hdr);
        const ConnectionID& remoteConID = initHdr->GetSrcID();
        const ConnectionID& localConID = initHdr->GetDstID();
        bool isNewCon = true;
        std::shared_ptr<Connection> foundConnection = nullptr;
        uint64_t descriptor;
        for (auto con: this->connections) {
            if (con.second->GetConnectionState() != ConnectionState::WAIT_FOR_PEER_CLOSE &&
                con.second->GetConnectionState() != ConnectionState::CLOSED &&
                (con.second->getRemoteConnectionID() == remoteConID || 
                con.second->getLocalConnectionID() == localConID)) {
                isNewCon = false;
                descriptor = con.first;
                foundConnection = con.second;
                break;
            }
        }
        // If a new connection: (1) create a connection and set the state to CREATED;
        //                      (2) add the pkt to the need ack list; and recPkt list;
        //                      (3) add the connection package to the connection list;
        // else, if receive an ACK, update the need ack list ---- remove this package; 
        // Or send the ACK once receive the connection in the connection creation stage?
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
            connection->initPTO();
            utils::logger::warn("[Connection {}] peer ID exchanged,local: {}, remote: {}", conDes, localConID.ToString(), remoteConID.ToString());
            this->connections[conDes] = connection;
            // payload::Initial initHdr;
            uint64_t _usePktNum = connection->GetNewPktNum();
            utils::logger::info("Got packet number = {} for a new connection.", conDes);
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, expRemoteID, _usePktNum);
            utils::ByteStream emptyBys(nullptr, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            
            // NO reliabel transmission is garuanteed here, so set the state to ESTABLISED now
            // connection->SetConnectionState(ConnectionState::ESTABLISHED);
            // this->connectionReadyCallback(conDes);
            // RELIABLE transmission --- we should wait for ACK.
            // ACKed
            if (!_recPktAdded) {
                connection->addRecPkt(recPkt);
                _recPktAdded = true;
            }
            // this->addNeedACKRecPkt(recPkt);

            // Add ackframe --- the ack delay --- actually it should be calculated when sending ack
            utils::IntervalSet inS = connection->getNeedACKRecPkt();
            inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum());
            std::shared_ptr<payload::ACKFrame> ackFrm = std::make_shared<payload::ACKFrame>(0, inS);
            pld->AttachFrame(ackFrm);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            connection->AddPacket(pkt);
            connection->AddWhetherNeedACK(true);
            
            connection->SetConnectionState(ConnectionState::PEER_ESTABLISHED);
            this->connectionReadyCallback(conDes); // callback connection ready function
            
        } else if (foundConnection->GetConnectionState() == ConnectionState::CREATED) {
            if (foundConnection->HaveReceivedPkt(_recPktNum)) {
                break;
            }
            assert(foundConnection->getRemoteConnectionID() == remoteConID);
            foundConnection->SetSrcConnectionID(localConID);
            ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
            generator.AddUsedConnectionID(localConID);
            utils::logger::warn("[Connection 0] peer ID exchanged,local: {}, remote: {}", 
                                localConID.ToString(), remoteConID.ToString());
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
            foundConnection->InitCongestionState(MAX_PACKET_LENGTH, 
                                                MAX_PACKET_LENGTH * 10);
            
            // ACKed
            // uint64_t recPktNum = recPkt->GetPktNum();
            uint64_t _usePktNum = foundConnection->GetNewPktNum();
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, remoteConID, _usePktNum);
            utils::ByteStream emptyBys(nullptr, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            utils::IntervalSet inS = foundConnection->getNeedACKRecPkt();
            inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum());
            std::shared_ptr<payload::ACKFrame> ackFrm = std::make_shared<payload::ACKFrame>(0, inS);
            pld->AttachFrame(ackFrm);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            foundConnection->AddPacket(pkt);
            foundConnection->AddWhetherNeedACK(false);
            if (!_recPktAdded) { // Add to rec pkt
                foundConnection->addRecPkt(recPkt);
                _recPktAdded = true;
            }
            
            // do not need ack..
            // parse frames in the payload --- to update the need ack package numbers?
            for (auto _recFrm: recPld->GetFrames()) {
                if (_recFrm->Type() == payload::FrameType::ACK) {
                    std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(_recFrm);
                    utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                    // utils::logger::warn("Going to remove ACKed sent packets from the connection INITIAL header");
                    foundConnection->updateLargestACKedPacket(_recACKInS);
                    foundConnection->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                    foundConnection->AddAckedSentPktNum(_recACKInS);
                }
            }
        } else if (foundConnection->GetConnectionState() == ConnectionState::PEER_ESTABLISHED) {
            // utils::logger::info("Got a feedback connection peer_established packet = {}", _recPktNum);
            if (foundConnection->HaveReceivedPkt(_recPktNum)) {
                break;
            }
            // utils::logger::info("Got a feedback connection peer_established packet = {} not contained", _recPktNum);
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
            foundConnection->InitCongestionState(MAX_PACKET_LENGTH * 10, MAX_PACKET_LENGTH * 100);
            if (!_recPktAdded) { // Add to rec pkt
                foundConnection->addRecPkt(recPkt);
                _recPktAdded = true;
            }
            
            for (auto _recFrm: recPld->GetFrames()) {
                // then other possible frames?
                if (_recFrm->Type() == payload::FrameType::ACK) {
                    std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(_recFrm);
                    utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                    // utils::logger::warn("Going to remove ACKed sent packets from the connection INITIAL header");
                    foundConnection->updateLargestACKedPacket(_recACKInS);
                    foundConnection->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                    foundConnection->AddAckedSentPktNum(_recACKInS);
                }
            }
            // NEED not send ACK again
        } else if (foundConnection->GetConnectionState() == ConnectionState::ESTABLISHED) {
            utils::logger::info("Got packet initial ESTAB");
            /*
            if (!foundConnection->HaveReceivedPkt(_recPktNum)) {
                break;
            }
            uint64_t recPktNum = recPkt->GetPktNum();
            uint64_t _usePktNum = foundConnection->GetNewPktNum();
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, remoteConID, _usePktNum);
            utils::ByteStream emptyBys(nullptr, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            utils::IntervalSet inS = foundConnection->getNeedACKRecPkt();
            inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum());
             
            std::shared_ptr<payload::ACKFrame> ackFrm = std::make_shared<payload::ACKFrame>(0, inS);
            pld->AttachFrame(ackFrm);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            foundConnection->AddPacket(pkt);
            foundConnection->AddWhetherNeedACK(false);
            if (!_recPktAdded) { // Add to rec pkt
                // foundConnection->addRecPkt(recPkt);
                // _recPktAdded = true;
            }
            */
            
            // do not need ack..
            // parse frames in the payload --- to update the need ack package numbers?
            for (auto _recFrm: recPld->GetFrames()) {
                if (_recFrm->Type() == payload::FrameType::ACK) {
                    std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(_recFrm);
                    utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                    // utils::logger::warn("Going to remove ACKed sent packets from the connection INITIAL header");
                    foundConnection->updateLargestACKedPacket(_recACKInS);
                    foundConnection->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                    foundConnection->updateRTT(_ackRecFrm);
                    foundConnection->AddAckedSentPktNum(_recACKInS);
                }
            }
        }
        break;
    }
    case(payload::PacketType::ONE_RTT): {

        utils::logger::warn("Receive a ONE-RTT packet with packet number = {}", _recPktNum);
        std::shared_ptr<payload::ShortHeader> shHdr = std::dynamic_pointer_cast<payload::ShortHeader>(hdr);
        auto frames = recPld->GetFrames();
        // utils::logger::info("Got frames, number = {}", frames.size());
        bool haveAddedToACK = false;
        uint64_t nowIdx = 0;
        for (auto frm: frames) {
            nowIdx += 1;
            if (frm->Type() == payload::FrameType::STREAM) {
                // STREAM Frame
                utils::logger::info("Got a stream frame from the one-rtt packet.");
                std::shared_ptr<payload::StreamFrame> streamFrm = std::dynamic_pointer_cast<payload::StreamFrame>(frm);
                uint64_t streamID = streamFrm->StreamID();
                const ConnectionID& localConID = shHdr->GetDstID();
                std::shared_ptr<Connection> foundCon = nullptr;
                uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->GetConnectionState() != ConnectionState::WAIT_FOR_PEER_CLOSE &&
                        con.second->GetConnectionState() != ConnectionState::CLOSED &&
                        con.second->getLocalConnectionID() == localConID) {
                        foundCon = con.second;
                        conSeq = con.first;
                        break;
                    }
                }
                assert(foundCon != nullptr);
                // utils::logger::info("In Stream frame with the pkt number = {}", _recPktNum);
                if (foundCon->HaveReceivedPkt(_recPktNum)) {
                    // utils::logger::info("Haved received the packet = {}", _recPktNum);
                    break;
                }
                // utils::logger::info("In Stream frame with the pkt number = {} not rec yet", _recPktNum);
                // ADD the received packet to need ACK packet list
                if (!haveAddedToACK) {
                    foundCon->addNeedACKRecPkt(recPkt);
                    haveAddedToACK = true;
                }
                // utils::logger::info("In Stream frame with the pkt number = {} not rec yet 2", _recPktNum);
                if (foundCon->IsStreamIDUsed(streamID) == false) {
                    // Stream NOT used
                    // utils::logger::info("In Stream frame with the pkt number = {} not rec yet 4", _recPktNum);
                    foundCon->InsertStream(streamID, true);
                    // Stream READY
                    // utils::logger::info("conseq = {}, streamid = {}", conSeq, streamID);
                    this->streamReadyCallback(conSeq, streamID);
                    // utils::logger::info("In Stream frame with the pkt number = {} not rec yet 6", _recPktNum);
                }
                // utils::logger::info("In Stream frame with the pkt number = {} not rec yet 3", _recPktNum);
                auto buf = streamFrm->FetchBuffer();
                size_t buflen = streamFrm->GetLength();
                size_t bufOffset = streamFrm->GetOffset();
                uint8_t fin = streamFrm->FINFlag();
                Stream& _nowStream = foundCon->GetStreamByID(streamID);
                if (_nowStream.WhetherTpUper(bufOffset)) {
                    this->streamDataReadyCallback(conSeq, streamID, std::move(buf), buflen, (bool)fin);
                    _nowStream.UpdateExpOffset(buflen);
                    std::pair<std::unique_ptr<uint8_t[]>, std::pair<uint64_t, bool>> _bufStreamInfo;
                    while (true) {
                        _bufStreamInfo = _nowStream.GetBufferedStream();
                        if (_bufStreamInfo.first == nullptr) {
                            break;
                        }
                        bool _nowfin = _bufStreamInfo.second.second;
                        // bool _nowfin = _nowStream.GetAndPopBufferedFin();
                        this->streamDataReadyCallback(conSeq, streamID, 
                                    std::move(_bufStreamInfo.first), _bufStreamInfo.second.first, _nowfin);
                    }
                } else {
                    utils::logger::warn("Got a disorder stream frame.");
                    // _nowStream.UpdateExpOffset(buflen);
                    _nowStream.AddToBufferedFin((bool)fin);
                    _nowStream.AddToBufferedStream(std::move(buf), bufOffset, buflen);
                    utils::logger::warn("Buffered stream packet number = {}", _nowStream.GetBufferedStreamLength());
                }
                // utils::logger::info("Going to callback!");
                // this->streamDataReadyCallback(conSeq, streamID, std::move(buf), buflen, (bool)fin);
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
                if (foundCon->HaveReceivedPkt(_recPktNum)) { // HAVE received the packet.
                    break;
                }
                // THEN it is the pasive end that receives CLOSE_STREAM stream.
                if (foundCon->GetStreamStateByID(streamID) != StreamState::FIN) {
                    // IF have not added this packet to the need rec pkt list, then add it to the list
                    if (!haveAddedToACK) {
                        foundCon->addNeedACKRecPkt(recPkt);
                        haveAddedToACK = true;
                    }
                } else {
                    foundCon->addRecPkt(recPkt);
                    haveAddedToACK = true;
                }
                // CLOSE the stream HERE, then the CLOSE_STREAM packet do not need ack again.
                foundCon->CloseStreamByID(streamID);
                // foundCon->addNeedACKRecPkt(recPkt); // NEED to send ack for the received packet.
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
                if (foundCon->HaveReceivedPkt(_recPktNum)) { // HAVE close the connection
                    break;
                }
                // THE passive end that receives the CONNECTION_CLOSE packet
                if (foundCon->GetConnectionState() != ConnectionState::CLOSED) {
                    if (!haveAddedToACK) {
                        // have been acked alone
                        foundCon->addRecPkt(recPkt);
                        // foundCon->addNeedACKRecPkt(recPkt);
                        haveAddedToACK = true;
                    }
                }
                // add a ack packet to the founcon
                utils::IntervalSet inS = foundCon->getNeedACKRecPkt();
                inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum());
                uint64_t _usePktNum = foundCon->GetNewPktNum();
                uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
                std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                                    foundCon->getRemoteConnectionID(), _usePktNum);
                std::shared_ptr<payload::ACKFrame> _sentACKFrm = std::make_shared<payload::ACKFrame>(0, 
                                                                    inS);
                std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
                pl->AttachFrame(_sentACKFrm);
                std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, foundCon->GetSockaddrTo());
                // ADD pkt to this connection
                foundCon->AddPacket(pk);
                foundCon->AddWhetherNeedACK(false);

                foundCon->SetConnectionState(ConnectionState::CLOSED);
                utils::logger::warn("Receive CONNECTION_CLOSE frame, transition to DRAIN state");
                this->connectionCloseCallback(conSeq, reason, errorCode);
                // registter?? --- what's this?
            } else if (frm->Type() == payload::FrameType::ACK) {
                // utils::logger::warn("Receive an ACK frame");
                const ConnectionID& localConID = shHdr->GetDstID();
                
                std::shared_ptr<Connection> foundCon = nullptr;
                // uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->GetConnectionState() != ConnectionState::CLOSED &&
                        (con.second->getLocalConnectionID() == localConID)) {
                        foundCon = con.second;
                        // conSeq = con.first;
                        break;
                    }
                }
                if (foundCon == nullptr) {
                    // utils::logger::info("Got an ACK frame for a closed or erased connection. Break.");
                    break;
                }
                std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(frm);
                utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                assert(foundCon != nullptr);
                // utils::logger::warn("Wait for peer close pkt number = {}", 
                //     foundCon->GetWaitForPeerConCloseACKPktNum());
                if (foundCon->GetConnectionState() == ConnectionState::WAIT_FOR_PEER_CLOSE) {
                    foundCon->SetConnectionState(ConnectionState::CLOSED);
                    utils::logger::info("Receive an ACK for Connection_CLOSE frame, transition to CLOSED state.");
                }
                /*
                if (foundCon->HaveReceivedPkt(_recPktNum)) { // HAVE close the connection
                    if (!haveAddedToACK && nowIdx == frames.size()) {
                        foundCon->addRecPkt(recPkt);
                        haveAddedToACK = true;
                    }
                    break;
                }*/
                // ACK frame only packet need not to be added to the need ack packet list.
                foundCon->updateLargestACKedPacket(_recACKInS);
                utils::logger::warn("Going to remove ACKed sent packets from the connection");
                // utils::logger::warn("Going to remove ACKed sent packets from the connection");
                foundCon->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                foundCon->AddAckedSentPktNum(_recACKInS);
            } else if (frm->Type() == payload::FrameType::PING) {
                utils::logger::info("Got a ping frame from the one-rtt packet.");
                std::shared_ptr<payload::PingFrame> pingFrm = std::dynamic_pointer_cast<payload::PingFrame>(frm);
                const ConnectionID& localConID = shHdr->GetDstID();
                utils::logger::info("Got a ping frame from the one-rtt packet, DstID = {}",localConID.ToString());
                std::shared_ptr<Connection> foundCon = nullptr;
                uint64_t conSeq;
                for (auto con: this->connections) {
                    if (con.second->GetConnectionState() != ConnectionState::WAIT_FOR_PEER_CLOSE &&
                        con.second->GetConnectionState() != ConnectionState::CLOSED &&
                        con.second->getLocalConnectionID() == localConID) {
                        foundCon = con.second;
                        conSeq = con.first;
                        break;
                    }
                }
                assert(foundCon != nullptr);
                utils::logger::info("In Ping frame with the pkt number = {}", _recPktNum);
                if (foundCon->HaveReceivedPkt(_recPktNum)) {
                    utils::logger::info("Haved received the Ping packet = {}", _recPktNum);
                    break;
                }
                utils::logger::info("In Ping frame with the pkt number = {} not rec yet", _recPktNum);
                // ADD the received packet to need ACK packet list
                if (!haveAddedToACK) {
                    foundCon->addNeedACKRecPkt(recPkt);
                    haveAddedToACK = true;
                }                
            } 
        }
        break;
    }
    default:
        break;
    }
    
    /* Check if PTO expired */



    /*
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
        // If a new connection: (1) create a connection and set the state to CREATED;
        //                      (2) add the pkt to the need ack list; and recPkt list;
        //                      (3) add the connection package to the connection list;
        // else, if receive an ACK, update the need ack list ---- remove this package; 
        // Or send the ACK once receive the connection in the connection creation stage?
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
            
            // NO reliabel transmission is garuanteed here, so set the state to ESTABLISED now
            // connection->SetConnectionState(ConnectionState::ESTABLISHED);
            // this->connectionReadyCallback(conDes);
            // RELIABLE transmission --- we should wait for ACK.
            connection->addRecPkt(recPkt);
            // this->addNeedACKRecPkt(recPkt);

            // Add ackframe --- the ack delay --- actually it should be calculated when sending ack
            utils::IntervalSet inS = connection->getNeedACKRecPkt();
            inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum() + 1);
            std::shared_ptr<payload::ACKFrame> ackFrm = std::make_shared<payload::ACKFrame>(0, inS);
            pld->AttachFrame(ackFrm);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            connection->AddPacket(pkt);
            // connection->addNeedACKSentPkt(pkt);
            connection->AddWhetherNeedACK(true);
            
            connection->SetConnectionState(ConnectionState::PEER_ESTABLISHED);
            
        } else if (foundConnection->GetConnectionState() == ConnectionState::CREATED) {
            assert(foundConnection->getRemoteConnectionID() == remoteConID);
            foundConnection->SetSrcConnectionID(localConID);
            ConnectionIDGenerator generator = ConnectionIDGenerator::Get();
            generator.AddUsedConnectionID(localConID);
            utils::logger::warn("[Connection 0] peer ID exchanged,local: {}, remote: {}", 
                                localConID.ToString(), remoteConID.ToString());
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
            
            uint64_t recPktNum = recPkt->GetPktNum();
            std::shared_ptr<payload::Initial> initHdr = std::make_shared<payload::Initial>(4, 1, localConID, remoteConID, recPktNum);
            utils::ByteStream emptyBys(nullptr, 0);
            std::shared_ptr<payload::Payload> pld = std::make_shared<payload::Payload>(emptyBys, 0);
            utils::IntervalSet inS = foundConnection->getNeedACKRecPkt();
            inS.AddInterval(recPkt->GetPktNum(), recPkt->GetPktNum() + 1);
            std::shared_ptr<payload::ACKFrame> ackFrm = std::make_shared<payload::ACKFrame>(0, inS);
            pld->AttachFrame(ackFrm);
            std::shared_ptr<payload::Packet> pkt = std::make_shared<payload::Packet>(initHdr, pld, datagram->GetAddrSrc());
            foundConnection->AddPacket(pkt);
            foundConnection->AddWhetherNeedACK(false);
            // do not need ack..
            // parse frames in the payload --- to update the need ack package numbers?
            for (auto _recFrm: recPld->GetFrames()) {
                if (_recFrm->Type() == payload::FrameType::ACK) {
                    std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(_recFrm);
                    utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                    foundCon->updateLargestACKedPacket(_recACKInS);
                    foundConnection->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                }
            }
        } else if (foundConnection->GetConnectionState() == ConnectionState::PEER_ESTABLISHED) {
            this->connectionReadyCallback(descriptor); // callback connection ready function
            foundConnection->SetConnectionState(ConnectionState::ESTABLISHED);
            for (auto _recFrm: recPld->GetFrames()) {
                if (_recFrm->Type() == payload::FrameType::ACK) {
                    std::shared_ptr<payload::ACKFrame> _ackRecFrm = std::dynamic_pointer_cast<payload::ACKFrame>(_recFrm);
                    utils::IntervalSet _recACKInS = _ackRecFrm->GetACKRanges();
                    foundCon->updateLargestACKedPacket(_recACKInS);
                    foundConnection->remNeedACKPkt(_recACKInS); // remove the sent packages that need ACK.
                }
            }
            // NEED not send ACK again
        }
    } else if (hdr->Type() == payload::PacketType::ONE_RTT) {
        utils::logger::warn("Receive a ONE-RTT package! Going to get frames from it!\n");
        std::shared_ptr<payload::ShortHeader> shHdr = std::dynamic_pointer_cast<payload::ShortHeader>(hdr);
        auto frames = recPld->GetFrames();
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
    */
    return 0;
}

QUICServer::QUICServer(uint16_t port) : QUIC(PeerType::SERVER, port) {}

QUICClient::QUICClient() : QUIC(PeerType::CLIENT) {}

}  // namespace thquic::context
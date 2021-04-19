#ifndef _THQUIC_CONTEXT_CONNECTION_H_
#define _THQUIC_CONTEXT_CONNECTION_H_

#include "payload/packet.hh"
#include <set>
#include <map>
#include <sys/time.h>
namespace thquic::context {

constexpr uint64_t MAX_STREAM_NUM = 32;
constexpr uint64_t MAX_CONNECTION_NUM = 100;

enum class StreamState {
    UNDEFINED,
    RUNNING,    // sec 17.2.2
    FIN,   // sec 17.2.3
};

enum class ConnectionState {
    CREATED,
    ESTABLISHED,    // sec 17.2.2
    PEER_ESTABLISHED,
    WAIT_FOR_PEER_CLOSE,
    CLOSED,   // sec 17.2.3
};

const int MAC_ACK_DELAY = 25;

int MAX_RTT = 200;

struct ACKTimer {
   uint64_t pktNum;
   uint64_t remTime;
   uint64_t idx;
};

class Stream {
    public:
        Stream() { myState = StreamState::UNDEFINED; }
        Stream(uint64_t _streamID, StreamState _state) {
            this->streamID = streamID;
            this->myState = _state;
        }

        void SetStreamState(StreamState _state) {
            this->myState = _state;
        }

        void SetStreamDataReadyCallback(StreamDataReadyCallbackType _sdrc) {
            this->streamDataReadyCallback = _sdrc;
        }
        
        void SetBidirectional(bool _bir) {
            this->bidirectional = _bir;
        }

        StreamState GetStreamState() {
            return this->myState;
        }

        uint64_t GetUpdateOffset(uint64_t sentBufLen) {
            uint64_t nowOffset = this->_offset;
            this->_offset += sentBufLen;
            return nowOffset;
        }

        bool WhetherTpUper(uint64_t recOffset) {
            return this->expOffset == recOffset;
        }

        void UpdateExpOffset(uint64_t recBufLen) {
            this->expOffset += recBufLen;
        }

        void AddToBufferedStream(std::unique_ptr<uint8_t[]> recBuf, uint64_t recBufOffset, uint64_t recBufLen) {
            this->bufferedStream.push_back(std::move(recBuf));
            this->bufferedOffset.push_back(recBufOffset);
            this->bufferedLen.push_back(recBufLen);
        }

        void AddToBufferedFin(bool _fin) {
            this->bufferedFin.push_back(_fin);
        }

        std::pair<std::unique_ptr<uint8_t[]>, uint64_t> GetBufferedStream() {
            if (this->bufferedStream.size() == 0 || 
                this->expOffset != this->bufferedOffset.front()) {
                    return std::make_pair(nullptr, 0);
                }
            uint64_t _rtBufLen = this->bufferedLen.front();
            this->bufferedLen.pop_front();
            this->bufferedOffset.pop_front();
            std::unique_ptr<uint8_t[]> _rtBuffer = std::move(this->bufferedStream.front());
            this->bufferedStream.pop_front();
            return std::make_pair(std::move(_rtBuffer), _rtBufLen);
        }

        bool GetAndPopBufferedFin() {
            bool _rtfin = this->bufferedFin.front();
            this->bufferedFin.pop_front();
            return _rtfin;
        }
        

    private:
        uint64_t streamID;
        StreamState myState;
        StreamDataReadyCallbackType streamDataReadyCallback;
        bool bidirectional;
        uint64_t _offset = 0;
        uint64_t expOffset = 0;
        std::list<std::unique_ptr<uint8_t[]> > bufferedStream;
        std::list<uint64_t> bufferedOffset;
        std::list<uint64_t> bufferedLen;
        std::list<bool> bufferedFin;
};

class Connection {
   public:
    static uint64_t connectionDescriptor;

    std::list<std::shared_ptr<payload::Packet>>& GetPendingPackets() {
        return this->pendingPackets;
    }

    void SetConnectionState(ConnectionState _sta) {
        this->curState = _sta;
    }

    const ConnectionState& GetConnectionState() {
        return this->curState;
    }

    static uint64_t GenerateConnectionDescriptor() {
        connectionDescriptor += 1;
        return connectionDescriptor - 1;
        /*
        for (uint64_t i = 0; i < MAX_CONNECTION_NUM; i++) {
            if (connectionDescriptorToState.find(i) == connectionDescriptorToState.end()) {
                connectionDescriptorToState[i] = true;
                return i;
            }
        }
        throw std::runtime_error("Connection descriptor exhausted!");*/
    }

    uint64_t GenerateStreamID() {
        for (uint64_t i = 0; i < MAX_STREAM_NUM; i++) {
            auto insertRes = usedStreamID.insert(i);
            if (insertRes.second) {
                this->streamState[i] = StreamState::RUNNING;
                this->streamIDToStream[i] = Stream(i, StreamState::RUNNING);
                return i;
            }
        }
        throw std::runtime_error("Stream id for ths connection exhausted");
    }

    void SetStreamFeature(uint64_t streamID, bool bidirectional) {
        this->streamFeature[streamID] = bidirectional;
        this->streamIDToStream[streamID].SetBidirectional(bidirectional);
    }

    void CloseStreamByID(uint64_t streamID) {
        this->streamState[streamID] = StreamState::FIN;
        this->streamIDToStream[streamID].SetStreamState(StreamState::FIN);
    }

    StreamState GetStreamStateByID(uint64_t _streamID) {
        return this->streamIDToStream[_streamID].GetStreamState();
    }

    void AddPacket(std::shared_ptr<payload::Packet> pk) {
        this->pendingPackets.push_back(pk);
        this->toSendPktNum.AddInterval(pk->GetPktNum(), pk->GetPktNum());
    }

    void AddPacketACKCallback(SentPktACKedCallbackType clb) {
        this->pendingPacketsCallback.push_back(clb);
    }

    void SetStreamDataReadyCallbackByStreamID(uint64_t _stid, StreamDataReadyCallbackType _srcb) {
        this->streamIDToStream[_stid].SetStreamDataReadyCallback(_srcb);
    }
    
    void SetStreamReadyCallback(StreamReadyCallbackType _srcbt) {
        this->streamReadyCallback = _srcbt;
    }
    
    // to the socket and add ACK frame in the payload
    // StreamReadyCallback: add the new stream to self stream and send an INITIAL package
    int InsertStream(uint64_t streamID, bool bidirectional) {
        auto insertRes = this->usedStreamID.insert(streamID);
        if (!insertRes.second) {
            return -1; // has existed in this connection
        }
        this->streamIDToStream[streamID] = Stream(streamID, StreamState::RUNNING);
        this->streamFeature[streamID] = bidirectional;
        this->streamState[streamID] = StreamState::RUNNING;
        return 0;
    }

    bool IsStreamIDUsed(uint64_t streamID) {
        return !(this->usedStreamID.find(streamID) == this->usedStreamID.end());
    }

    void SetDstConnectionID(const ConnectionID& cid) {
        this->remoteConnectionID = cid;
    }

    void SetSrcConnectionID(const ConnectionID& cid) {
        this->localConnectionID = cid;
    }

    void SetSockaddrTo(const sockaddr_in& _at) {
        this->addrTo = _at;
    }

    const sockaddr_in& GetSockaddrTo() {
        return this->addrTo;
    }

    const ConnectionID& getRemoteConnectionID() {
        return this->remoteConnectionID;
    }
    
    const ConnectionID& getLocalConnectionID() {
        return this->localConnectionID;
    }

    size_t GetFinalSizeByStreamID(uint64_t streamID) {
        size_t totLen = 0;
        for (auto pkt: this->pendingPackets) {
            // Use length of the total packet (for both header and the payload) or just the payload?
            // totLen += pkt->EncodeLen();
            auto pld = pkt->GetPktPayload();
            for (auto frm: pld->GetFrames()) {
                // Just STREAM frames are needed to be calculated or orther frames?
                if (frm->Type() == payload::FrameType::STREAM || frm->Type() == payload::FrameType::MAX_STREAMS || frm->Type() == payload::FrameType::MAX_STREAM_DATA) {
                    std::shared_ptr<payload::StreamFrame> strmFrm = std::dynamic_pointer_cast<payload::StreamFrame>(frm);
                    if (strmFrm->StreamID() == streamID) {
                        totLen += frm->EncodeLen();
                    }
                }
            }
        }
        return totLen;
    }


    void addNeedACKRecPkt(std::shared_ptr<payload::Packet> needACKPkt) {
        uint64_t pn = needACKPkt->GetPktNum();
        // ADD to the received package vector
        // this->recPkt.push_back(needACKPkt);
        // uint64_t idx = this->recPkt.size();
        // GET the rem time that is needed for the ACKTimer construction
        // Add a package which needs to ack but not acked yet (received package).
        /*
        uint64_t rem_tim = 0;
        for (auto _acp: this->notACKedRecPkt) {
            rem_tim += _acp.remTime;
        }*/
        // Use the packet number to discriminate between different packets ---- then how to generate 
        // the packet number?
        // ADD the ACKTimer for such a needACKPkt
        // this->notACKedRecPkt.push_back(ACKTimer{pn, rem_tim});
        // utils::logger::info("In add to need ack pkt 2");
        uint64_t _recIdx = this->recPkt.size();
        this->recPkt.push_back(needACKPkt);
        this->recPktNum.AddInterval(pn, pn);
        // utils::logger::info("In add to need ack pkt 3");
        
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t msec = curTime.tv_usec; // / 1000;
        this->notACKedRecPkt.push_back(ACKTimer{pn, msec, _recIdx});
        // utils::logger::info("In add to need ack pkt");
    }

    void addNeedACKSentPkt(std::shared_ptr<payload::Packet> needACKPkt) {
        uint64_t pn = needACKPkt->GetPktNum();
        // ADD to the received package vector
        // this->recPkt.push_back(needACKPkt);
        // uint64_t idx = this->recPkt.size();
        // GET the rem time that is needed for the ACKTimer construction
        // ADD the ACKTimer for such a needACKPkt
        // MAX RTT --- 
        // Add a packet that need to be acked but not receive its ack packet yet.
        // FOR re-transmission
        uint64_t _sentIdx = this->sentPkt.size();
        this->sentPkt.push_back(needACKPkt);
        
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t msec = curTime.tv_usec; // / 1000;
        this->notACKedSentPkt.push_back(ACKTimer{pn, msec, _sentIdx});
        // this->notACKedSentPkt.push_back(ACKTimer{pn, MAX_RTT});
    }
    
    void remNeedACKPkt(utils::IntervalSet _recACKInterval) {
        // REMOVE packets that have already been acked from the notACKedSentPkt
        // DO NOT need to remove it from the sentpkt
        // this->tmpRecACKInterval = _recACKInterval;
        // this->notACKedSentPkt.remove_if(_containInACKInterval); // this->_... can only be used to call the func
        // ...Any other solution?
        // utils::logger::info("Now going to print the removed need ACK packets");
        /*
        printf("For received ack interval: \n");
        for (int i = 1; i <= 10; i++) {
            if (_recACKInterval.Contain(i)) {
                printf("%d ", i);
            }
        }
        printf("\n");*/
        std::list<ACKTimer> newNotACKedSentPkt;
        newNotACKedSentPkt.clear();
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t msec = curTime.tv_usec; // / 1000;
        utils::IntervalSet _addedToNewACKedPktNum;
        for (auto _nns: this->notACKedSentPkt) {
            if (!_recACKInterval.Contain(_nns.pktNum) && !_addedToNewACKedPktNum.Contain(_nns.pktNum)) {
                newNotACKedSentPkt.push_back(ACKTimer{_nns.pktNum, msec, _nns.idx});
                _addedToNewACKedPktNum.AddInterval(_nns.pktNum, _nns.pktNum);
            } else {
                // printf("%d ", _nns.pktNum);
            }
        }
        this->notACKedSentPkt.clear();
        this->notACKedSentPkt = newNotACKedSentPkt;
        /*
        printf("For remained not acked sent packets: \n");
        for (auto _newNotACKPkt: this->notACKedSentPkt) {
            printf("%d ", _newNotACKPkt.pktNum);
        }
        printf("\n");*/
        // printf("\n");
    }

    // ADD one received packet that does not need to ack
    void addRecPkt(std::shared_ptr<payload::Packet> _recPkt) {
       
        uint64_t pn = _recPkt->GetPktNum();
        // add rec pkt number
        this->recPktNum.AddInterval(pn, pn);
        // add to the recokt
        this->recPkt.push_back(_recPkt);
    }
    
    void addSentPkt(std::shared_ptr<payload::Packet> _sentPkt) {
        uint64_t pn = _sentPkt->GetPktNum();
        // ADD the sent package number
        this->sentPktNum.AddInterval(pn, pn);
        // ADD to the sent package vector
        this->sentPkt.push_back(_sentPkt);
    }
   
    // GET the package number interval set from the need ACK Rec Pkt list;
    // Then `itS` is used to construct an ack interval set which is used in the ACK frame construction;
    utils::IntervalSet getNeedACKRecPkt() {
        // HAVE acked those packets, and according to ref the packet that only contains ack frame need not to be acked.
        utils::IntervalSet itS;
        for (auto nap: this->notACKedRecPkt) {
            uint64_t pn = nap.pktNum;
            itS.AddInterval(pn, pn);
        }
        itS.AddIntervalSet(this->recPktNum); // ADD package numbers that have been received in itS
        this->notACKedRecPkt.clear(); // CLEAR the not acked package????????
        return itS;
    }

    std::shared_ptr<payload::ACKFrame> GetACKFrameForRecPackages() {
        // if (this->notACKedRecPkt.size() == 0) {
        //     return nullptr;
        // }
        utils::IntervalSet _notACKInS;
        for (auto _notACKPkt: this->notACKedRecPkt) {
            uint64_t pn = _notACKPkt.pktNum;
            _notACKInS.AddInterval(pn, pn);
        }
        _notACKInS.AddIntervalSet(this->recPktNum); // add received package number
        std::shared_ptr<payload::ACKFrame> _ackFrm = std::make_shared<payload::ACKFrame>(MAC_ACK_DELAY, 
                                                            _notACKInS);
        this->notACKedRecPkt.clear();
        return _ackFrm;
    }

    std::shared_ptr<payload::Packet> GetSpeACKPacketForRecPkt() {
        utils::IntervalSet _notACKInS;
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t msec = curTime.tv_usec; // / 1000;
        std::list<ACKTimer> newNotACKedRecPkt;
        for (auto _notACKPkt: this->notACKedRecPkt) {
            uint64_t pn = _notACKPkt.pktNum;
            if (msec - _notACKPkt.remTime > MAC_ACK_DELAY) {
                _notACKInS.AddInterval(pn, pn);
            } else {
                newNotACKedRecPkt.push_back(_notACKPkt);
            }
        }
        _notACKInS.AddIntervalSet(this->recPktNum);
        if (_notACKInS.Empty()) {
            return nullptr;
        }
        std::shared_ptr<payload::ACKFrame> _ackFrm = std::make_shared<payload::ACKFrame>(MAC_ACK_DELAY, 
                                                            _notACKInS);
        // uint64_t _usePktNum = this->GetNewPktNum();
        std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(1, 
                                                    this->getRemoteConnectionID(), 0);
        std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
        pl->AttachFrame(_ackFrm);
        std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, this->GetSockaddrTo());
        this->notACKedRecPkt = newNotACKedRecPkt;
        /*
        printf("For packet numbers contained in the sent ACK frame: \n");
        for (int i = 1; i <= 9; i++) {
            if (_notACKInS.Contain(i)) {
                printf("%d ", i);
            }
        }
        printf("\n");*/
        return pk;
    }

    void AddWhetherNeedACK(bool _wn) {
        // Does this packet need to be acked?
        this->whetherNeedACK.push_back(_wn);
    }

    bool GetPendingPackageNeedACK() {
        return this->whetherNeedACK.front();
    }

    void PopWhetherNeedACK() {
        this->whetherNeedACK.pop_front();
    }

    std::list<ACKTimer>& GetNotACKedSentPkt() {
        return this->notACKedSentPkt;
    }

    std::shared_ptr<payload::Packet> GetSentPktByIdx(int idx) {
        return this->sentPkt[idx];
    }

    bool HaveReceivedPkt(uint64_t _recPktNum) {
        return this->recPktNum.Contain(_recPktNum);
    }

    uint64_t GetNewPktNum() {
        this->nowPktNum += 1;
        return this->nowPktNum;
    }

    Stream& GetStreamByID(uint64_t streamID) {
        return this->streamIDToStream[streamID];
    }

    void AddAckedSentPktNum(utils::IntervalSet _ackedPktNum) {
        this->ackedSentPktNum.AddIntervalSet(_ackedPktNum);
    }

    bool WhetherAckedPktNum(uint64_t _jn) {
        return this->ackedSentPktNum.Contain(_jn);
    }

    void RemoveToSendPktNum(uint64_t _jn) {
        this->toSendPktNum.RemoveInterval(_jn, _jn);
    }

    bool WhetherToSendPktNum(uint64_t _jn) {
        return this->toSendPktNum.Contain(_jn);
    }

    void SetWaitForPeerConCloseACKPktNum(uint64_t _jn) {
        this->waitForPeerConCloseACKPktNum = _jn;
    }

    uint64_t GetWaitForPeerConCloseACKPktNum() {
        return this->waitForPeerConCloseACKPktNum;
    }

    void PrintSentNeedACKPktNum() {
        // utils::logger::info("Now print the packet numbers for those sent packets which have not been acked");
        for (auto _notAckedPkt: this->notACKedSentPkt) {
            // printf("%d ", _notAckedPkt.pktNum);
        }
        // printf("\n");
    }

    void PrintRecNotACKPktNum() {
        // utils::logger::info("Now print the packet numbers for those received packets which have not been acked");
        for (auto _notAckedPkt: this->notACKedRecPkt) {
            // printf("%d ", _notAckedPkt.pktNum);
        }
        // printf("\n");
    }



   private:
    
    static std::map<uint64_t, bool> connectionDescriptorToState;
    std::list<std::shared_ptr<payload::Packet>> pendingPackets;
    std::list<bool> whetherNeedACK;
    std::list<SentPktACKedCallbackType> pendingPacketsCallback;
    ConnectionID localConnectionID;
    ConnectionID remoteConnectionID;

    std::set<uint64_t> usedStreamID;
    std::map<uint64_t, bool> streamFeature;
    std::map<uint64_t, StreamState> streamState;
    std::map<uint64_t, Stream> streamIDToStream;
    StreamReadyCallbackType streamReadyCallback;
    sockaddr_in addrTo;

    ConnectionState curState;


    // use the same package number space ---- but need to change in the future
    utils::IntervalSet sentPktNum;
    utils::IntervalSet recPktNum;
    utils::IntervalSet ackedSentPktNum;
    utils::IntervalSet toSendPktNum;
    std::vector<std::shared_ptr<payload::Packet> > recPkt;
    std::vector<std::shared_ptr<payload::Packet> > sentPkt;
    std::list<SentPktACKedCallbackType> sentPktACKCallback; // sync with sentPkt
    
    // use the index of the package in the recPkt/sentPkt or just the pktNum?
    std::list<ACKTimer> notACKedRecPkt;
    std::list<ACKTimer> notACKedSentPkt;
    

    utils::IntervalSet tmpRecACKInterval;
    uint64_t nowPktNum = 0;
    uint64_t waitForPeerConCloseACKPktNum;

    bool _containInACKInterval(ACKTimer _at) {return this->tmpRecACKInterval.Contain(_at.pktNum); };

};

uint64_t Connection::connectionDescriptor = 0;

}  // namespace thquic::context
#endif
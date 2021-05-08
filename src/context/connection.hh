#ifndef _THQUIC_CONTEXT_CONNECTION_H_
#define _THQUIC_CONTEXT_CONNECTION_H_

#include "payload/packet.hh"
#include <set>
#include <map>
#include <sys/time.h>
#include <tuple>

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

namespace thquic::context {

// TODO: some of these are changeable for flow control
constexpr uint64_t MAX_CONNECTION_NUM = 1000;
constexpr uint64_t MAX_STREAM_NUM = 32;
constexpr uint64_t MAX_PACKET_LENGTH = 1080; // SET maximum packet length to 1000 (Bytes?)
constexpr uint64_t MAX_PACKET_DATA_LENGTH = 1024;
constexpr uint64_t MAX_STREAM_OFFSET = 10240;
constexpr uint64_t MAX_CONNECTION_SIZE = MAX_STREAM_NUM * MAX_STREAM_OFFSET;

constexpr float FLOW_CONTROL_THRESH = 0.8;

enum class StreamState {
    UNDEFINED,
    RUNNING,    // sec 17.2.2
    FIN,   // sec 17.2.3
};

// TODO: more states for parameter exchange
enum class ConnectionState {
    CREATED,
    ESTABLISHED,    // sec 17.2.2
    PEER_ESTABLISHED,
    WAIT_FOR_PEER_CLOSE,
    CLOSED,   // sec 17.2.3
};

enum class CongestionState {
    SLOW_START,
    RECOVERY,
    CONGESTION_AVOIDANCE,
};

//TODO: enum for flow control

// const int MAX_ACK_DELAY = 25;

int INITIAL_RTT = 500;//msec
int kPacketThreshold = 3;
float kTimeThreshold = (9/8);
int kGranualarity = 1;//msec
// int INITIAL_SPACE = 0;
// int HANDSHAKE_SPACE = 1;
// int APPLICATIONDATA_SPACE = 2;

struct ACKTimer {
   uint64_t pktNum;
   uint64_t remTime;
   uint64_t idx;
   uint64_t pktLen; // Bytes?
};

class Stream {
    public:
        Stream(): myState(StreamState::UNDEFINED) {}
        Stream(uint64_t _streamID, StreamState _state) {
            this->streamID = _streamID;
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

        void SetMaxSendOffset(uint64_t _max_offset) {
            this->max_send_offset = _max_offset;
        }

        bool IsSendPermitted(uint64_t sentBufLen) {
            return this->_offset + sentBufLen <= this->max_send_offset;
        }

        uint64_t GetUpdateOffset(uint64_t sentBufLen) {
            uint64_t nowOffset = this->_offset;
            this->_offset += sentBufLen;
            return nowOffset;
        }

        bool WhetherTpUper(uint64_t recOffset) {
            return this->expOffset == recOffset;
        }

        void SetMaxRecvOffset(uint64_t _max_offset) {
            this->max_recv_offset = _max_offset;
        }

        bool IsRecPermitted(uint64_t off, uint64_t recBufLen) {
            return off + recBufLen <= this->max_recv_offset;
        }

        void UpdateExpOffset(uint64_t recBufLen) {
            this->expOffset += recBufLen;
        }

        void AddToBufferedStream(std::unique_ptr<uint8_t[]> recBuf, uint64_t recBufOffset, 
                                    uint64_t recBufLen) {
            this->bufferedStream.push_back(std::move(recBuf));
            this->bufferedOffset.push_back(recBufOffset);
            this->bufferedLen.push_back(recBufLen);
        }

        void AddToBufferedFin(bool _fin) {
            this->bufferedFin.push_back(_fin);
        }

        std::pair<std::unique_ptr<uint8_t[]>, std::pair<uint64_t, bool> > GetBufferedStream() {
            if (this->bufferedStream.size() == 0){
                return std::make_pair(nullptr, std::make_pair(0, false));
            }
            uint ofs = 0;
            for (auto _bufOfs: this->bufferedOffset) {
                if (this->expOffset == _bufOfs) {
                    break;
                }
                ofs += 1;
            }
            if (ofs == this->bufferedOffset.size()) {
                return std::make_pair(nullptr, std::make_pair(0, false));
            }
            uint64_t _rtBufLen = this->bufferedLen[ofs];
            std::unique_ptr<uint8_t[]> _rtBuffer = std::move(this->bufferedStream[ofs]);
            this->expOffset += _rtBufLen;
            bool _rtFin = this->bufferedFin[ofs];
            // REMOVE returned buffer and relevant attributes
            this->bufferedStream.erase(this->bufferedStream.begin() + ofs);
            this->bufferedLen.erase(this->bufferedLen.begin() + ofs);
            this->bufferedOffset.erase(this->bufferedOffset.begin() + ofs);
            this->bufferedFin.erase(this->bufferedFin.begin() + ofs);
            return std::make_pair(std::move(_rtBuffer), std::make_pair(_rtBufLen, _rtFin));
            /*
            if (this->bufferedStream.size() == 0 || 
                this->expOffset != this->bufferedOffset.front()) {
                    return std::make_pair(nullptr, 0);
                }
            uint64_t _rtBufLen = this->bufferedLen.front();
            this->expOffset += _rtBufLen;
            this->bufferedLen.pop_front();
            this->bufferedOffset.pop_front();
            
            std::unique_ptr<uint8_t[]> _rtBuffer = std::move(this->bufferedStream.front());
            this->bufferedStream.pop_front();
            return std::make_pair(std::move(_rtBuffer), _rtBufLen);*/
        }

        bool GetAndPopBufferedFin() {
            bool _rtfin = this->bufferedFin.front();
            // this->bufferedFin.pop_front();
            return _rtfin;
        }
        
        uint64_t GetBufferedStreamLength() {
            return this->bufferedStream.size();
        }

        std::pair<uint64_t, uint64_t> GetFlowControlParams() {
            return {max_recv_offset, max_send_offset};
        }

        uint64_t GetSendOffset() {
            return this->_offset;
        }

        uint64_t GetRecvOffset() {
            return this->expOffset;
        }

        bool ShouldIncRecvLimit() {
            return this->expOffset >= max_recv_offset * FLOW_CONTROL_THRESH;
        }

    private:
        uint64_t streamID;
        StreamState myState;
        StreamDataReadyCallbackType streamDataReadyCallback;
        //TODO: which direction?
        bool bidirectional;
        uint64_t _offset = 0;
        uint64_t expOffset = 0;
        std::vector<std::unique_ptr<uint8_t[]> > bufferedStream;
        std::vector<uint64_t> bufferedOffset;
        std::vector<uint64_t> bufferedLen;
        std::vector<bool> bufferedFin;
        uint64_t max_recv_offset = 0;
        uint64_t max_send_offset = 0;
};

class Connection {
   public:
    static uint64_t connectionDescriptor;

    std::list<std::shared_ptr<payload::Packet>>& GetPendingPackets() {
        return this->pendingPackets;
    }

    void Initial() {
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        // TODO::what is reset of loss_detection_timer??
        loss_detection_timer_msec = curTime.tv_sec * 1000;
        latest_rtt = 0;
        smoothed_rtt = INITIAL_RTT;
        rtt_var = INITIAL_RTT / 2;
        min_rtt = 0;
        first_rtt_sample = 0;
        larget_acked_packet = 0xffffffffUL;  
        max_recv_stream_offset = MAX_STREAM_OFFSET;
        max_recv_stream_offset = MAX_CONNECTION_SIZE;
        max_recv_stream_num = MAX_STREAM_NUM;
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

    bool ShouldIncStreamNumLimit() {
        return this->streamIDToStream.size() >= this->max_recv_stream_num * FLOW_CONTROL_THRESH;
    }

    uint64_t GenerateStreamID(PeerType type, bool bidirectional) {
        for (uint64_t i = 0; i < this->max_send_stream_num; i++) {
            // step1: form a streamID
            uint64_t streamID = 0x0;
            if(type != PeerType::CLIENT)
                streamID |= 0x1;
            if (!bidirectional)
                streamID |= 0x2;
            uint64_t type = streamID; 

            auto insertRes = usedStreamID[type].insert(i);
            if (insertRes.second) {
                this->streamState[i] = StreamState::RUNNING;
                this->streamIDToStream[i] = Stream(i, StreamState::RUNNING);
                this->streamIDToStream[i].SetMaxRecvOffset(this->max_recv_stream_offset);
                this->streamIDToStream[i].SetMaxSendOffset(this->max_send_stream_offset);
                return (((i<<2)&0xFFFFFFFFFFFFFFFC)|type);
            }
        }
        throw std::runtime_error("Stream id for ths connection exhausted");
    }

    void SetStreamFeature(uint64_t streamID, bool bidirectional) {
        this->streamFeature[streamID] = bidirectional;
        this->streamIDToStream[streamID].SetBidirectional(bidirectional);
    }

    void SetStreamMaxRecvOffset(uint64_t streamID, uint64_t max_offset) {
        this->streamIDToStream[streamID].SetMaxRecvOffset(max_offset);
    }

    void SetStreamMaxSendOffset(uint64_t streamID, uint64_t max_offset) {
        this->streamIDToStream[streamID].SetMaxSendOffset(max_offset);
    }

    std::pair<uint64_t, uint64_t> GetStreamFlowControlParams(uint64_t streamID) {
        return this->streamIDToStream[streamID].GetFlowControlParams();
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
        auto insertRes = this->usedStreamID[streamID&0x3].insert((streamID>>2)&0x3FFFFFFFFFFFFFFF);
        if (!insertRes.second) {
            return -1; // has existed in this connection
        }
        this->streamIDToStream[streamID] = Stream(streamID, StreamState::RUNNING);
        this->streamIDToStream[streamID].SetMaxRecvOffset(this->max_recv_stream_offset);
        this->streamIDToStream[streamID].SetMaxSendOffset(this->max_send_stream_offset);
        this->streamFeature[streamID] = bidirectional;
        this->streamState[streamID] = StreamState::RUNNING;
        return 0;
    }

    bool IsStreamIDUsed(uint64_t streamID) {
        return !(this->usedStreamID[streamID&0x3].find((streamID>>2)&0x3FFFFFFFFFFFFFFF) == this->usedStreamID[streamID&0x3].end());
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
                //TODO: only count stream packets?
                if (frm->Type() == payload::FrameType::STREAM
                        || frm->Type() == payload::FrameType::MAX_STREAMS
                        || frm->Type() == payload::FrameType::MAX_STREAM_DATA) {
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
        this->notACKedRecPkt.push_back(ACKTimer{pn, msec, _recIdx, needACKPkt->EncodeLen()});
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
        uint64_t msec = curTime.tv_sec * 1000; // / 1000;
        this->notACKedSentPkt.push_back(ACKTimer{pn, msec, _sentIdx, needACKPkt->EncodeLen()});
        // this->notACKedSentPkt.push_back(ACKTimer{pn, MAX_RTT});
    }
    
    //TODO: read this
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
        std::map<uint64_t,uint64_t> newlatestACKedSentPktNum;
        newNotACKedSentPkt.clear();
        newlatestACKedSentPktNum.clear();
        struct timeval curTime;
        gettimeofday(&curTime, nullptr);
        uint64_t msec = curTime.tv_sec * 1000; // / 1000;
        utils::IntervalSet _addedToNewACKedPktNum;
        utils::IntervalSet _addedToLastestNeedAckPktNum;
        bool _isPacketMiss = false;
        bool _haveNotACKed = false;
        bool _haveNewACKed = false;
        for (auto _nns: this->notACKedSentPkt) {
            if (!_recACKInterval.Contain(_nns.pktNum) && !_addedToNewACKedPktNum.Contain(_nns.pktNum)) {
                newNotACKedSentPkt.push_back(ACKTimer{_nns.pktNum, msec, _nns.idx, _nns.pktLen});
                _addedToNewACKedPktNum.AddInterval(_nns.pktNum, _nns.pktNum);
                _haveNotACKed = true;
            } else if (!_addedToNewACKedPktNum.Contain(_nns.pktNum)) {
                // printf("%d ", _nns.pktNum);
                newlatestACKedSentPktNum[_nns.pktNum] = _nns.remTime;
                _addedToNewACKedPktNum.AddInterval(_nns.pktNum, _nns.pktNum);
                if (_haveNotACKed && !_haveNewACKed) {
                    _isPacketMiss = true;
                }
                _haveNewACKed = true;
                if (this->curState == ConnectionState::ESTABLISHED) {
                    if (this->onFlight > _nns.pktLen)
                        this->onFlight -= _nns.pktLen; // DECREASE the onflight packet size;
                    else this->onFlight = 0;
                    this->UpdateCongestionWindowByACK(_nns.pktLen); // UPDATE congestionWindow based on the current congestionState;
                }

                // update flow control params
                //TODO: check effectiveness
                auto pkt = this->GetSentPktByIdx(_nns.idx);
                if (pkt->GetPacketType() == payload::PacketType::ONE_RTT) {
                    auto hdr = std::dynamic_pointer_cast<payload::ShortHeader>(pkt->GetPktHeader());
                    auto  recPld = pkt->GetPktPayload();
                    for (auto frm: recPld->GetFrames()) {
                        switch (frm->Type())
                        {
                        case payload::FrameType::MAX_DATA: {
                            auto _maxDataFrm = std::dynamic_pointer_cast<payload::MaxDataFrame>(frm);
                            this->SetMaxSendSize(_maxDataFrm->GetMaximumData());
                            break;
                        }
                        case payload::FrameType::MAX_STREAM_DATA: {
                            auto _maxDataFrm = std::dynamic_pointer_cast<payload::MaxStreamDataFrame>(frm);
                            this->SetStreamMaxSendOffset(_maxDataFrm->StreamID(), _maxDataFrm->GetMaximumStreamData());
                            break;
                        }
                        case payload::FrameType::MAX_STREAMS: {
                            auto _maxDataFrm = std::dynamic_pointer_cast<payload::MaxStreamsFrame>(frm);
                            this->SetMaxSendStreamNum(_maxDataFrm->GetStreamsNum());
                            break;
                        }
                        }
                    }
                }
            }
        }
        this->notACKedSentPkt.clear();
        this->notACKedSentPkt = newNotACKedSentPkt;

        if (_isPacketMiss && this->curState == ConnectionState::ESTABLISHED) {
            this->PacketMissCallback(); // SET the congestionThreshold to the half of the current congestionWindow; SET congestionWindow to MAX_PACKET_LENGTH; SET congestionState;
        }
        if (!_haveNewACKed && _haveNotACKed && this->curState == ConnectionState::ESTABLISHED) {
            this->threeTimesACK += 1;
            utils::logger::info("Got a disordered ACK packet.");
            // IF you are confident of your implementation, >= can be safely changed to ==;
            if (this->threeTimesACK >= 13) {
                this->ThreeTimeNoNewACKCallback(); // SET congestionThreshold and congestionWindow to half value of current congestionWindow;
                this->threeTimesACK = 0;
            }
        }
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
        std::shared_ptr<payload::ACKFrame> _ackFrm = std::make_shared<payload::ACKFrame>(latest_rtt, 
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
            if (msec - _notACKPkt.remTime > latest_rtt) {
                _notACKInS.AddInterval(pn, pn);
            } else {
                newNotACKedRecPkt.push_back(_notACKPkt);
            }
        }
        _notACKInS.AddIntervalSet(this->recPktNum);
        if (_notACKInS.Empty()) {
            return nullptr;
        }
        std::shared_ptr<payload::ACKFrame> _ackFrm = std::make_shared<payload::ACKFrame>(latest_rtt, 
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

    void AddWhetherNeedACK_to_front(bool _wn) {
        // Does this packet need to be acked?
        this->whetherNeedACK.push_front(_wn);
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
        // for (auto _notAckedPkt: this->notACKedSentPkt) {
            // printf("%d ", _notAckedPkt.pktNum);
        // }
        // printf("\n");
    }

    void PrintRecNotACKPktNum() {
        // utils::logger::info("Now print the packet numbers for those received packets which have not been acked");
        // for (auto _notAckedPkt: this->notACKedRecPkt) {
            // printf("%d ", _notAckedPkt.pktNum);
        // }
        // printf("\n");
    }

    void updateLargestACKedPacket(utils::IntervalSet _ACKedRange) {
        uint64_t larget_acked = (_ACKedRange.GetStart() >= _ACKedRange.GetEnd()) ? _ACKedRange.GetStart() : _ACKedRange.GetEnd();
        if(this->larget_acked_packet == INFINITY) {
            larget_acked_packet = larget_acked;
        }
        else {
            larget_acked_packet = max(larget_acked_packet,larget_acked);
        }
    }

    void updateRTT(std::shared_ptr<payload::ACKFrame> _ackRecFrm) {
        uint64_t ACKdelay = _ackRecFrm->GetACKDelay();
        uint64_t largestACKed = _ackRecFrm->GetLargestACKed();
        if(this->latestACKedSentPktNum.size() > 0) {
            auto latestACKedSentPktNum_last = latestACKedSentPktNum.end();
            latestACKedSentPktNum_last--;
            uint64_t larget_new_acked_packets_number = latestACKedSentPktNum_last->first;
            uint64_t larget_new_acked_packets_sent_time = latestACKedSentPktNum_last->second;
            if(larget_new_acked_packets_number == largestACKed) {
                // TODO:: "IncludesAckEliciting(newly_acked_packets)" for case in "if" above
                struct timeval curTime;
                gettimeofday(&curTime,nullptr);
                this->latest_rtt = curTime.tv_sec * 1000 - larget_new_acked_packets_sent_time;
                if(first_rtt_sample == 0) {
                    min_rtt = latest_rtt;
                    smoothed_rtt = latest_rtt;
                    rtt_var = latest_rtt / 2;
                    first_rtt_sample = curTime.tv_sec * 1000;
                }
                else {
                    min_rtt = min(min_rtt, latest_rtt);
                    // TODO::what to do with handshake comfirmed
                    // ACKdelay = min(ACKdelay,max_ack_delay);
                    uint64_t adjusted_rtt = latest_rtt;
                    if (min_rtt + ACKdelay < latest_rtt)
                        adjusted_rtt = latest_rtt - ACKdelay;
                    rtt_var = (3 * rtt_var + ((smoothed_rtt-adjusted_rtt)&0x7FFFFFFFFFFFFFFF)) / 4;
                    smoothed_rtt = (7 * smoothed_rtt + adjusted_rtt) / 8;
                }
            }
        }
    }

    uint64_t getConnectionRTT() {
        return this->latest_rtt;
    }

    void InitCongestionState(uint64_t _initCW, uint64_t _initThreshold) {
        this->congestionWindow = _initCW;
        this->congestionThreshold = _initThreshold;
        this->congestionState = CongestionState::SLOW_START;
        this->onFlight = 0;
        this->threeTimesACK = 0;
    }

    void PacketMissCallback() {
        this->congestionThreshold = this->congestionWindow / 2;
        this->congestionWindow = MAX_PACKET_LENGTH;
        this->congestionState = CongestionState::SLOW_START;
    }

    void UpdateCongestionWindowByACK(uint64_t _recACKPktLen) {
        utils::logger::info("Updating cw by ack length = {}, cw = {}", _recACKPktLen, 
                            this->congestionWindow);
        if (this->congestionState == CongestionState::SLOW_START) {
            this->congestionWindow += _recACKPktLen;
            if (this->congestionWindow > this->congestionThreshold) {
                this->congestionState = CongestionState::CONGESTION_AVOIDANCE;
            }
        } else {
            this->congestionWindow += (MAX_PACKET_LENGTH) * (_recACKPktLen / this->congestionWindow);
        }
    }

    void UpdateOnFlightBySentPktLen(uint64_t _sentPktLen) {
        if (this->curState != ConnectionState::ESTABLISHED) {
            return;
        }
        this->onFlight += _sentPktLen;
        // if (this->onFlight > this->congestionThreshold) {
            // ENTER congestion avoidance state;
        //     this->congestionState = CongestionState::CONGESTION_AVOIDANCE; 
        // }
    }

    // TODO: add conditions here
    bool WhetherCanSendPkt(uint64_t _toSendPktLen) {
        // utils::logger::info("On flight pkt length = {}, to send pkt num = {}, congestion window = {}", 
        //                     this->onFlight, _toSendPktLen, this->congestionWindow);
        return (this->curState != ConnectionState::ESTABLISHED) || (this->onFlight + _toSendPktLen < this->congestionWindow);
    }

    void ThreeTimeNoNewACKCallback() {
        utils::logger::info("Receive same ack range for three times, cw = {}", 
                            this->congestionWindow);
        this->congestionWindow = this->congestionWindow / 2;
        this->congestionThreshold = this->congestionWindow;
    }

    void AddToUnsentBuf(std::unique_ptr<uint8_t[]> _unsentBuf, uint64_t _sID, uint64_t _buflen, bool FIN) {
        this->unsentBuf.push_back(std::move(_unsentBuf));
        this->unsentBufStreamID.push_back(_sID);
        this->unsentBufLen.push_back(_buflen);
        this->unsentBufFin.push_back(FIN);
    }

    std::shared_ptr<payload::Packet> GetPktFromUnsentBuf() {
        if (this->unsentBuf.size() == 0) {
            return nullptr;
        }
        std::unique_ptr<uint8_t[]> tmpBuf = std::move(this->unsentBuf[0]);
        uint8_t* tmpBufArr = tmpBuf.get();
        uint64_t tmpBufLen = this->unsentBufLen[0];
        uint64_t toSendBufLen = min(tmpBufLen, MAX_PACKET_DATA_LENGTH);
        uint64_t strmID = this->unsentBufStreamID[0];
        // flow control, check both stream and connection limits
        if (!this->GetStreamByID(strmID).IsSendPermitted(toSendBufLen) 
                || !this->IsSendPermitted(toSendBufLen)) {
            //TODO: optionally create an STREAM_DATA_BLOCKED frame
            return nullptr;
        }

        // uint8_t toSendBuf[MAX_PACKET_DATA_LENGTH];
        std::unique_ptr<uint8_t[]> toSendBuf = std::make_unique<uint8_t[]>(toSendBufLen);
        memcpy(toSendBuf.get(), tmpBufArr, toSendBufLen);
        tmpBufLen -= toSendBufLen;
        //TODO: if divided into smaller chunks, should fin only be true on the last chunk?
        // bool _fin = this->unsentBufFin[0];
        bool _fin;
        if (tmpBufLen <= 0) {
            this->unsentBuf.erase(this->unsentBuf.begin());
            this->unsentBufStreamID.erase(this->unsentBufStreamID.begin());
            this->unsentBufLen.erase(this->unsentBufLen.begin());
            this->unsentBufFin.erase(this->unsentBufFin.begin());
            _fin = this->unsentBufFin[0];
        } else {
            tmpBufArr += toSendBufLen;
            std::unique_ptr<uint8_t[]> aftBuf = std::make_unique<uint8_t[]>(tmpBufLen);
            memcpy(aftBuf.get(), tmpBufArr, tmpBufLen);
            this->unsentBuf[0] = std::move(aftBuf);
            this->unsentBufLen[0] = tmpBufLen;
            _fin = false;
        }

        uint64_t nowOffset = this->GetStreamByID(strmID).GetUpdateOffset(toSendBufLen);
        std::shared_ptr<payload::StreamFrame> fr = std::make_shared<payload::StreamFrame>(strmID, 
                                                    std::move(toSendBuf), 
                                                    toSendBufLen, 
                                                    nowOffset, true, _fin);
        
        uint64_t _usePktNum = this->GetNewPktNum();
        utils::logger::info("Sending data with packet numbeer = {}, len = {}. fin = {}", _usePktNum, 
                            toSendBufLen, _fin);
        uint64_t _pktNumLen = utils::encodeVarIntLen(_usePktNum);
        // pktNumLen | dstConID | pktNum
        std::shared_ptr<payload::ShortHeader> shHdr = std::make_shared<payload::ShortHeader>(_pktNumLen, 
                                                        this->getRemoteConnectionID(), _usePktNum);
        std::shared_ptr<payload::Payload> pl = std::make_shared<payload::Payload>();
        pl->AttachFrame(fr);
        std::shared_ptr<payload::Packet> pk = std::make_shared<payload::Packet>(shHdr, pl, 
                                                                    this->GetSockaddrTo());
        
        return pk;
    }

    void SetMaxRecvStreamOff(uint64_t max_offset) {
        this->max_recv_stream_offset = max_offset;
    }

    void SetMaxSendStreamOff(uint64_t max_offset) {
        this->max_send_stream_offset = max_offset;
    }

    void SetMaxRecvSize(uint64_t max_size) {
        this->max_recv_size = max_size;
    }

    void SetMaxSendSize(uint64_t max_size) {
        this->max_send_size = max_size;
    }

    void SetMaxRecvStreamNum(uint64_t max_num) {
        this->max_recv_stream_num = max_num;
    }

    void SetMaxSendStreamNum(uint64_t max_num) {
        this->max_send_stream_num = max_num;
    }

    std::tuple<uint64_t, uint64_t, uint64_t> GetFlowControlParams() {
        return {max_recv_stream_offset, max_recv_size, max_recv_stream_num};
    }

    bool ShouldIncStreamRecvLimit(uint64_t strmID) {
        return this->streamIDToStream[strmID].ShouldIncRecvLimit();
    }

    bool ShouldIncRecvLimit() {
        uint64_t cur_size = 0;
        for (auto& e: this->streamIDToStream) {
            cur_size += e.second.GetRecvOffset();
        }
        return cur_size <= max_recv_size * FLOW_CONTROL_THRESH;
    }

    bool IsRecvPermitted(uint64_t bufLen) {
        uint64_t cur_size = 0;
        for (auto& e: this->streamIDToStream) {
            cur_size += e.second.GetRecvOffset();
        }
        return cur_size + bufLen <= max_recv_size;
    }

    bool IsSendPermitted(uint64_t bufLen) {
        uint64_t cur_size = 0;
        for (auto& e: this->streamIDToStream) {
            cur_size += e.second.GetSendOffset();
        }
        return cur_size + bufLen <= max_send_size;
    }

   private:
    
    static std::map<uint64_t, bool> connectionDescriptorToState;
    std::list<std::shared_ptr<payload::Packet>> pendingPackets;
    std::list<bool> whetherNeedACK;
    std::list<SentPktACKedCallbackType> pendingPacketsCallback;
    ConnectionID localConnectionID;
    ConnectionID remoteConnectionID;

    std::set<uint64_t> usedStreamID[4];
    std::map<uint64_t, bool> streamFeature;
    std::map<uint64_t, StreamState> streamState;
    std::map<uint64_t, Stream> streamIDToStream;
    StreamReadyCallbackType streamReadyCallback;
    sockaddr_in addrTo;

    ConnectionState curState;

    std::vector<std::unique_ptr<uint8_t[]> > unsentBuf;
    std::vector<uint64_t> unsentBufStreamID;
    std::vector<uint64_t> unsentBufLen;
    std::vector<bool> unsentBufFin;


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
    std::map<uint64_t,uint64_t> latestACKedSentPktNum;
    

    utils::IntervalSet tmpRecACKInterval;
    uint64_t nowPktNum = 0;
    uint64_t waitForPeerConCloseACKPktNum;

    bool _containInACKInterval(ACKTimer _at) {return this->tmpRecACKInterval.Contain(_at.pktNum); };

    // RTT related
    uint64_t loss_detection_timer_msec;
    uint64_t latest_rtt;
    uint64_t smoothed_rtt;
    uint64_t rtt_var;
    uint64_t min_rtt;
    uint64_t first_rtt_sample;
    uint64_t larget_acked_packet;
    uint64_t loss_time;

    // congestion control
    uint64_t congestionWindow;
    uint64_t congestionThreshold;
    CongestionState congestionState;
    uint64_t onFlight;
    uint64_t threeTimesACK;

    // flow control
    // uint64_t max_recv_stream_offset; // default max_recv_offset for each stream
    // uint64_t max_send_stream_offset = 0; // default max_send_offset for each stream
    // uint64_t max_recv_size; // max (sum of offset of all the receiving streams)
    // uint64_t max_send_size = 0; // max (sum of offset of all the sending streams)
    // uint64_t max_recv_stream_num; // maximum number of receiving streams
    // uint64_t max_send_stream_num = 0; // maximum number of sending streams
    uint64_t max_recv_stream_offset = MAX_STREAM_OFFSET; // default max_recv_offset for each stream
    uint64_t max_send_stream_offset = MAX_STREAM_OFFSET; // default max_send_offset for each stream
    uint64_t max_recv_size = MAX_CONNECTION_SIZE; // max (sum of offset of all the receiving streams)
    uint64_t max_send_size = MAX_CONNECTION_SIZE; // max (sum of offset of all the sending streams)
    uint64_t max_recv_stream_num = MAX_STREAM_NUM; // maximum number of receiving streams
    uint64_t max_send_stream_num = MAX_STREAM_NUM; // maximum number of sending streams

};

uint64_t Connection::connectionDescriptor = 0;

}  // namespace thquic::context
#endif
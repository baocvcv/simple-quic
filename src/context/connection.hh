#ifndef _THQUIC_CONTEXT_CONNECTION_H_
#define _THQUIC_CONTEXT_CONNECTION_H_

#include "payload/packet.hh"
#include <set>
#include <map>
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
    CLOSED,   // sec 17.2.3
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

    private:
        uint64_t streamID;
        StreamState myState;
        StreamDataReadyCallbackType streamDataReadyCallback;
        bool bidirectional;
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

    void AddPacket(std::shared_ptr<payload::Packet> pk) {
        this->pendingPackets.push_back(pk);
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

   private:
    static std::map<uint64_t, bool> connectionDescriptorToState;
    std::list<std::shared_ptr<payload::Packet>> pendingPackets;
    ConnectionID localConnectionID;
    ConnectionID remoteConnectionID;

    std::set<uint64_t> usedStreamID;
    std::map<uint64_t, bool> streamFeature;
    std::map<uint64_t, StreamState> streamState;
    std::map<uint64_t, Stream> streamIDToStream;
    StreamReadyCallbackType streamReadyCallback;
    sockaddr_in addrTo;

    ConnectionState curState;

};

uint64_t Connection::connectionDescriptor = 0;

}  // namespace thquic::context
#endif
#ifndef _THQUIC_CONTEXT_CONNECTION_H_
#define _THQUIC_CONTEXT_CONNECTION_H_

#include "payload/packet.hh"
namespace thquic::context {

class Connection {
   public:
    std::list<std::shared_ptr<payload::Packet>>& GetPendingPackets() {
        return this->pendingPackets;
    }
   private:
    std::list<std::shared_ptr<payload::Packet>> pendingPackets;
};

}  // namespace thquic::context
#endif
#include "utils/log.hh"

namespace thquic::utils {

int initLogger() {
    logger::set_pattern("[%H:%M:%S %z] %^[%l]%$ %v");
    return 0;
}

std::string formatNetworkAddress(const struct sockaddr_in& addr) {
    char ipAddress[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ipAddress, sizeof(ipAddress));
    return std::string(ipAddress) + ":" + std::to_string(addr.sin_port);
}

}  // namespace thquic::utils

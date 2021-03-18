#ifndef _QUIC_UTILS_RANDOM_H_
#define _QUIC_UTILS_RANDOM_H_
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <random>

namespace thquic::utils {
// WARNING: INSECURE
// TODO: use crypto-secure random
class RandomByteGenerator {
   public:
    static RandomByteGenerator& Get();

    template <class iterator>
    int Fill(iterator begin, size_t len) {
        static constexpr auto chars =
            "0123456789"
            "abcdefghijklmnopqrstuvwxyz";

        static std::uniform_int_distribution dist{{}, std::strlen(chars) - 1};

        std::generate_n(begin, len,
                        [this]() { return chars[dist(this->rnd)]; });
        return 0;
    }

   private:
    RandomByteGenerator();
    std::mt19937 rnd;
};

}  // namespace thquic::utils

#endif
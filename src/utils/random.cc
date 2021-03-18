#include "utils/random.hh"

namespace thquic::utils {

RandomByteGenerator& RandomByteGenerator::Get() {
    static RandomByteGenerator generator{};
    return generator;
}

RandomByteGenerator::RandomByteGenerator() {
    auto constexpr seed_bytes =
        sizeof(typename std::mt19937::result_type) * std::mt19937::state_size;
    auto constexpr seed_len = seed_bytes / sizeof(std::seed_seq::result_type);
    std::array<std::seed_seq::result_type, seed_len> seed;
    std::random_device dev;
    std::generate_n(std::begin(seed), seed_len, std::ref(dev));
    std::seed_seq seed_seq(std::begin(seed), std::end(seed));
    rnd.seed(seed_seq);
}

}  // namespace thquic::utils
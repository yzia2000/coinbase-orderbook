#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>

namespace cob {

// Seqlock for trivially copyable types.
// Single writer, multiple readers. Writer never blocks.
// Reader retries on torn read.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class Seqlock {
public:
    Seqlock() : seq_(0) {}

    void store(const T& value) noexcept {
        seq_.fetch_add(1, std::memory_order_release); // odd = writing
        std::memcpy(&data_, &value, sizeof(T));
        seq_.fetch_add(1, std::memory_order_release); // even = done
    }

    T load() const noexcept {
        T result;
        uint32_t seq0;
        do {
            seq0 = seq_.load(std::memory_order_acquire);
            if (seq0 & 1) continue; // writer active, spin
            std::memcpy(&result, &data_, sizeof(T));
        } while (seq_.load(std::memory_order_acquire) != seq0);
        return result;
    }

private:
    alignas(64) T data_{};
    alignas(64) std::atomic<uint32_t> seq_;
};

} // namespace cob

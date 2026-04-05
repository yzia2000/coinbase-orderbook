#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>
#include <cstdint>

namespace cob {

// Seqlock for trivially copyable types.
// Single writer, multiple readers. Writer never blocks.
// Reader retries on torn read.
//
// Data is stored in atomic uint64_t words to avoid data races under
// the C++ memory model. The seqlock protocol detects torn reads and
// retries.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class Seqlock {
    static constexpr std::size_t NUM_WORDS = (sizeof(T) + sizeof(uint64_t) - 1) / sizeof(uint64_t);

public:
    Seqlock() : seq_(0) {
        for (auto& w : data_) w.store(0, std::memory_order_relaxed);
    }

    void store(const T& value) noexcept {
        uint64_t words[NUM_WORDS]{};
        std::memcpy(words, &value, sizeof(T));

        seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        for (std::size_t i = 0; i < NUM_WORDS; ++i) {
            data_[i].store(words[i], std::memory_order_relaxed);
        }

        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    T load() const noexcept {
        uint64_t words[NUM_WORDS];
        for (;;) {
            uint32_t seq0 = seq_.load(std::memory_order_acquire);
            if (seq0 & 1) continue; // writer active, spin

            for (std::size_t i = 0; i < NUM_WORDS; ++i) {
                words[i] = data_[i].load(std::memory_order_relaxed);
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t seq1 = seq_.load(std::memory_order_relaxed);

            if (seq0 == seq1) break;
        }

        T result;
        std::memcpy(&result, words, sizeof(T));
        return result;
    }

private:
    alignas(64) std::atomic<uint64_t> data_[NUM_WORDS];
    alignas(64) std::atomic<uint32_t> seq_;
};

} // namespace cob

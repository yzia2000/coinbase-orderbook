#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>
#include <cstdint>

namespace cob {

// Lock-free buffer that stores a trivially copyable T as atomic
// uint64_t words. Each word is a single MOV on x86-64.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class AtomicBuffer {
    static constexpr auto N = (sizeof(T) + 7) / 8;

public:
    AtomicBuffer() { for (auto& w : words_) w.store(0, std::memory_order_relaxed); }

    void write(const T& value) noexcept {
        uint64_t tmp[N]{};
        std::memcpy(tmp, &value, sizeof(T));
        for (std::size_t i = 0; i < N; ++i)
            words_[i].store(tmp[i], std::memory_order_relaxed);
    }

    T read() const noexcept {
        uint64_t tmp[N];
        for (std::size_t i = 0; i < N; ++i)
            tmp[i] = words_[i].load(std::memory_order_relaxed);
        T result;
        std::memcpy(&result, tmp, sizeof(T));
        return result;
    }

private:
    std::atomic<uint64_t> words_[N];
};

// Seqlock: single writer, multiple readers, writer never blocks.
// Reader retries on torn read detected via sequence counter.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class Seqlock {
public:
    void store(const T& value) noexcept {
        seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        data_.write(value);

        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(seq_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    T load() const noexcept {
        for (;;) {
            uint32_t seq0 = seq_.load(std::memory_order_acquire);
            if (seq0 & 1) continue;

            T result = data_.read();

            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == seq0)
                return result;
        }
    }

private:
    alignas(64) AtomicBuffer<T> data_;
    alignas(64) std::atomic<uint32_t> seq_{0};
};

} // namespace cob

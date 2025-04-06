#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

namespace alp_utils {
    namespace details {
        template<typename Lock_Type>
        struct atomic_lock {
            static constexpr Lock_Type LOCKED = 1;
            static constexpr Lock_Type UNLOCK = 0;

            bool try_lock() {
                Lock_Type expected = 0;
                return lock_.compare_exchange_strong(expected, LOCKED,
                                                     std::memory_order_acquire,
                                                     std::memory_order_relaxed);
            }

            void lock_impl() {
                for (;;) {
                    auto lk = lock_.load(std::memory_order_relaxed);
                    if (lk & LOCKED) {
                        lock_.wait(lk);
                    } else {
                        if (lock_.fetch_or(LOCKED, std::memory_order_relaxed) & LOCKED) {
                            continue;
                        }
                        std::atomic_thread_fence(std::memory_order_acquire);
                        return;
                    }
                }
            }

            void lock() {
                if (!try_lock()) {
                    lock_impl();
                }
            }

            void unlock() {
                lock_.store(UNLOCK, std::memory_order_release);
                lock_.notify_one();
            }

            std::atomic<Lock_Type> lock_{0};
        };
    }

    template<typename T>
    class seq_lock {
    public:
        seq_lock() : value_(std::make_shared<T>()) {
        }

        template<typename ...Args>
        explicit seq_lock(Args &&... args) {
            value_ = std::make_shared<T>(std::forward<Args>(args)...);
        }

        ~seq_lock() = default;

        seq_lock(const seq_lock &) = delete;

        seq_lock &operator=(const seq_lock &) = delete;

        seq_lock(seq_lock &&) noexcept = delete;

        seq_lock &operator=(seq_lock &&) noexcept = delete;

        std::shared_ptr<T> load_shared() {
            std::shared_ptr<T> data;
            uint32_t seq1 = seq_.load(std::memory_order_relaxed);
            data = value_.load(std::memory_order_relaxed);
            uint32_t seq2 = seq_.load(std::memory_order_relaxed);
            while (((seq1 & 1U) != 0U) || seq1 != seq2) {
                value_.wait(data, std::memory_order_relaxed);
                seq1 = seq_.load(std::memory_order_relaxed);
                data = value_.load(std::memory_order_relaxed);
                seq2 = seq_.load(std::memory_order_relaxed);
            }
            return data;
        }

        std::shared_ptr<T> load_shared(int retry) {
            if (retry <= 0) {
                return nullptr;
            }

            std::shared_ptr<T> data;
            for (int i = 0; i < retry; ++i) {
                uint32_t seq1 = seq_.load(std::memory_order_relaxed);
                data = value_.load(std::memory_order_relaxed);
                uint32_t seq2 = seq_.load(std::memory_order_relaxed);

                if (((seq1 & 1U) == 0U) && seq1 == seq2) {
                    return data;
                }
            }
            return nullptr;
        }

        std::shared_ptr<T> load() {
            return value_;
        }

        template<typename ...Args>
        void store(Args &&... args) {
            value_ = std::make_shared<T>(std::forward<Args>(args)...);
            value_.notify_all();
        }

        // value must be different from current value
        void store(std::shared_ptr<T> value) {
            if (value_ == value) {
                return;
            }
            value_ = value;
            value_.notify_all();
        }

        template<typename Fun>
        void update(Fun &&fun) {
            fun(*value_);
        }

        void lock() {
            lock_.lock();
            seq_.fetch_add(1, std::memory_order_acquire);
        }

        void unlock() {
            value_.store(std::make_shared<T>(*value_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
            seq_.fetch_add(1, std::memory_order_release);
            lock_.unlock();
            value_.notify_all();
        }

    private:
        static constexpr std::size_t align = std::hardware_destructive_interference_size;
        alignas(align) std::atomic<std::shared_ptr<T>> value_{};

        std::atomic<uint32_t> seq_{0};
        details::atomic_lock<uint32_t> lock_{};

        static_assert(align > sizeof(seq_) + sizeof(lock_),
                      "seq_lock alignment is not enough");
        [[maybe_unused]] char padding[align - sizeof(seq_) - sizeof(lock_)]{};
    };
} // namespace alp_utils

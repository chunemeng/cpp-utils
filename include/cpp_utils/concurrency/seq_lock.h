#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

namespace alp_utils {

	// NOTE: may no safety guarantee
	// because data = value_ in load() may trigger data race

	// no defer single write
	// deffer read op and may result in read-side contention
	template <typename T>
	class seq_lock {
	public:
		seq_lock() = default;

		~seq_lock() = default;

		seq_lock(const seq_lock&)			 = delete;
		seq_lock& operator=(const seq_lock&) = delete;

		seq_lock(seq_lock&&) noexcept			 = delete;
		seq_lock& operator=(seq_lock&&) noexcept = delete;

		auto load() -> T {
			T data;
			uint32_t seq1{};
			uint32_t seq2{};
			do {
				seq1 = seq_.load(std::memory_order_acquire);
				std::atomic_signal_fence(std::memory_order_acq_rel);
				data = value_;
				seq2 = seq_.load(std::memory_order_acquire);
				std::atomic_signal_fence(std::memory_order_acq_rel);
			} while (((seq1 & 1U) != 0U) || seq1 != seq2);
			return data;
		}

		void store(const T& data) {
			std::unique_lock<std::mutex> lock(mtx_);
			auto seq = seq_.load(std::memory_order_relaxed);
			seq_.store(seq + 1, std::memory_order_release);
			value_ = data;
			seq_.store(seq + 2, std::memory_order_release);
		}

	private:
		static constexpr std::size_t align = std::hardware_destructive_interference_size;
		alignas(align) T value_{};

		std::atomic<uint32_t> seq_{0};
		std::mutex mtx_;

		static_assert(align > sizeof(seq_) + sizeof(mtx_));
		[[maybe_unused]] char padding[align - sizeof(seq_) - sizeof(mtx_)];
	};
} // namespace alp_utils

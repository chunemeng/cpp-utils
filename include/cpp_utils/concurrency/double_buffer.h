#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace alp_utils {
	template <typename T>
	class double_buffer {
	public:
		double_buffer() : current_(0) {
			buffers_[0].store(std::make_shared<T>(), std::memory_order_relaxed);
			buffers_[1].store(std::make_shared<T>(), std::memory_order_relaxed);
		}

		template <typename... Args>
		double_buffer(Args&&... args) : current_(0) {
			buffers_[0].store(std::make_shared<T>(std::forward<Args>(args)...), std::memory_order_relaxed);
			buffers_[1].store(std::make_shared<T>(buffers_[0]), std::memory_order_relaxed);
		}

		template <typename Fun>
		void update(Fun&& fun) {
			std::unique_lock<std::mutex> lock(mutex_);
			auto buffers = wait_for_write();

			std::forward<Fun>(fun)(buffers);
			swap();
			buffers = wait_for_write();
			std::forward<Fun>(fun)(buffers);
		}

		std::shared_ptr<T> read() {
			auto cur = current_.load(std::memory_order_acquire);
			auto ptr = buffers_[cur & 1].load(std::memory_order_relaxed);

			// the reason for the while loop is to ensure that
			// the buffer is swapped out and update before we really load it (means add the reference count)
			// etc. get cur = 0, then writer swaps to 1 and update 0 cause no one read it, then we load buffer 0 here
			// I understand it's rare, but it's possible
			while (cur != current_.load(std::memory_order_acquire)) {
				cur = current_.load(std::memory_order_acquire);
				ptr = buffers_[cur & 1].load(std::memory_order_relaxed);
			}

			return ptr;
		}

	private:
		std::shared_ptr<T> wait_for_write() {
			auto cur			= next();
			auto buffers		= buffers_[cur].load(std::memory_order_relaxed);
			while (buffers.use_count() != 2) {
				std::this_thread::yield();
			}
			return buffers;
		}

		void swap() { current_.fetch_add(1, std::memory_order_acq_rel); }

		uint64_t current() const { return current_.load(std::memory_order_acquire) & 1; }

		uint64_t next() const { return 1 - current(); }

	private:
		std::atomic<uint64_t> current_;
		std::atomic<std::shared_ptr<T>> buffers_[2];
		std::mutex mutex_;
	};

} // namespace alp_utils

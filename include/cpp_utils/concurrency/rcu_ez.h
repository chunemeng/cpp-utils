#pragma once
#include <atomic>
#include <memory>

namespace alp_utils {
	template <typename T>
	class rcu_ez {

	public:
		rcu_ez() = default;

		~rcu_ez() = default;

		rcu_ez(const rcu_ez&)			 = delete;
		rcu_ez& operator=(const rcu_ez&) = delete;


		std::shared_ptr<T> load() { return data_.load(std::memory_order_relaxed); }

		template <typename... Args>
		void store(Args&&... args) {
			data_.store(std::make_shared<T>(args...), std::memory_order_relaxed);
		}

	private:
		std::atomic<std::shared_ptr<T>> data_{};
	};


} // namespace alp_utils

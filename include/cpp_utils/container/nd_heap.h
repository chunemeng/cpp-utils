#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace alp_utils {
    template<typename T, uint16_t d = 4, typename Compare = std::less<T>, typename Alloc = std::allocator<T>, typename Container = std::vector<T, Alloc>>
    class nd_heap {
    public:
        nd_heap() = default;

        explicit
        nd_heap(const Compare &comp,
                const Alloc &a = Alloc())
                : cmp_(comp), data_(a) {}

        void reserve(size_t n) {
            data_.reserve(n);
        }


        template<typename... Args>
        void emplace(Args &&... args) {
            data_.emplace_back(std::forward<Args>(args)...);
            swim(data_.size() - 1);
        }

        void push(const T &val) {
            emplace(val);
        }

        void pop() {
            assert(!data_.empty());
            if (data_.size() == 1) {
                data_.pop_back();
                return;
            }

            std::swap(data_[0], data_.back());

            data_.pop_back();

            sink(0);
        }

        const T &top() const {
            return data_[0];
        }

        bool empty() const {
            return data_.empty();
        }

        size_t size() const {
            return data_.size();
        }

    private:
        constexpr size_t parent_index(size_t i) const {
            if constexpr (is_power_of_two) {
                return (i - 1) / d;
            } else {
                return (i - 1) >> d_shift;
            }
        }

        constexpr size_t child_index(size_t i, size_t k) const {
            if constexpr (is_power_of_two) {
                return d * i + k + 1;
            } else {
                return (i << d_shift) + (k + 1);
            }
        }

        void swim(size_t index) {
            auto current = data_[index];

            size_t parent = parent_index(index);
            while (index > 0 && cmp_(data_[parent], current)) {
                data_[index] = std::move(data_[parent]);
                index = parent;
                parent = parent_index(index);
            }
            data_[index] = std::move(current);
        }

        template<size_t... K>
        size_t find_extreme_child_impl(size_t index, const size_t size, std::index_sequence<K...>) const {
            size_t extreme_child = child_index(index, 0);

            auto update_extreme = [this]<size_t I>(const size_t index, const size_t size, size_t &extreme_child) {
                const size_t child_idx = child_index(index, I + 1);
                extreme_child = (child_idx < size && cmp_(data_[extreme_child], data_[child_idx])) ? child_idx
                                                                                                   : extreme_child;
            };

            (update_extreme.template operator()<K>(index, size, extreme_child), ...);
            return extreme_child;
        }

        void sink(size_t index) {
            const size_t size = data_.size();
            auto current = std::move(data_[index]);
            if constexpr (is_power_of_two && d <= 8) {
                while (true) {
                    size_t extreme_child = find_extreme_child_impl(index, size, std::make_index_sequence<d - 1>());
                    if (extreme_child >= size || !cmp_(current, data_[extreme_child])) break;
                    data_[index] = std::move(data_[extreme_child]);
                    index = extreme_child;
                }
            } else {
                const size_t min_child = child_index(size, 0);

                while (index < min_child) {
                    size_t extreme_child = min_child;
                    for (uint16_t k = 0; k < d; ++k) {
                        size_t current_child = child_index(index, k);
                        if (current_child >= size) break;
                        extreme_child = cmp_(data_[extreme_child], data_[current_child])
                                        ? current_child : extreme_child;
                    }

                    if (!cmp_(current, data_[extreme_child])) break;
                    data_[index] = std::move(data_[extreme_child]);
                    index = extreme_child;
                }

                while (true) {
                    size_t first_child = child_index(index, 0);
                    if (first_child >= size) break;

                    size_t extreme_child = first_child;
                    for (uint16_t k = 1; k < d; ++k) {
                        size_t current_child = child_index(index, k);
                        if (current_child >= size) break;
                        extreme_child = cmp_(data_[extreme_child], data_[current_child])
                                        ? current_child : extreme_child;
                    }

                    if (!cmp_(current, data_[extreme_child])) break;
                    data_[index] = std::move(data_[extreme_child]);
                    index = extreme_child;
                }
            }

            data_[index] = std::move(current);
        }

        [[no_unique_address]] Compare cmp_;
        Container data_;

        static constexpr bool is_power_of_two = (d > 0) && ((d & (d - 1)) == 0);

        static constexpr uint8_t d_shift = __builtin_ctz(d);
        static_assert(d > 1, "d must be greater than 1");
        static_assert(std::is_same_v<typename Container::value_type, T>, "Container value type must be same as T");
    };
} // namespace alp_utils
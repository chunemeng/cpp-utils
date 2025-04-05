#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <cmath>
#include <cassert>

namespace alp_utils {

    template<typename T, typename Hash = std::hash<T>>
    class bloom_filter {
    public:
        bloom_filter() = default;

        void reserve(std::size_t size, double fp_rate = kDefaultFalsePositiveRate) {
            size_t bit_size = calculate_bit_size(size, fp_rate);
            size_t const byte_size = (bit_size + 7) / 8;
            size_ = static_cast<uint32_t>(bit_size);
            bit_array_ = std::make_unique<uint8_t[]>(byte_size);
            std::fill(bit_array_.get(), bit_array_.get() + byte_size, 0);
        }

        static constexpr size_t calculate_bit_size(size_t n, double p) {
            return static_cast<size_t>(n * (-std::log(p) / kLog2_2));
        }

        template<typename U>
        void insert(U &&value) {
            size_t hash1 = hash_(std::forward<U>(value));
            size_t hash2 = hash1 ^ (hash1 >> 16);
            size_t index1 = hash1 % size_;
            size_t index2 = hash2 % size_;

            bit_array_[index1 >> 3] |= (1 << (index1 & 0x7));
            bit_array_[index2 >> 3] |= (1 << (index2 & 0x7));
        }

        template<typename U>
        bool contains(U &&value) const {
            size_t hash1 = hash_(std::forward<U>(value));
            size_t hash2 = hash1 ^ (hash1 >> 16);
            size_t index1 = hash1 % size_;
            size_t index2 = hash2 % size_;

            return (bit_array_[index1 >> 3] & (1 << (index1 & 0x7))) &&
                   (bit_array_[index2 >> 3] & (1 << (index2 & 0x7)));
        }


    private:
        static constexpr double kDefaultFalsePositiveRate = 0.001;
        static constexpr double kLog2 = 0.69314718056;
        static constexpr double kLog2_2 = kLog2 * kLog2;

        uint32_t size_{0};

        [[no_unique_address]] Hash hash_{};

        std::unique_ptr<uint8_t[]> bit_array_;
    };

} // namespace alp_utils
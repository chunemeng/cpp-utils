#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <cassert>

class Bitmap {
private:
    std::unique_ptr<uint8_t[]> bits_;
    size_t capacity_;

public:
    Bitmap() = default;

    Bitmap(size_t max_val) : capacity_(max_val) {
        size_t byte_count = (max_val + 7) / 8;
        bits_ = std::make_unique<uint8_t[]>(byte_count);
        std::fill(bits_.get(), bits_.get() + byte_count, 0);
    }

    void reserve(size_t max_val) {
        capacity_ = max_val;
        size_t byte_count = (max_val + 7) / 8;
        bits_ = std::make_unique<uint8_t[]>(byte_count);
        std::fill(bits_.get(), bits_.get() + byte_count, 0);
    }

    void set(size_t num) {
        check_range(num);
        size_t byte_idx = num / 8;
        uint8_t bit_mask = 1 << (num % 8);
        bits_[byte_idx] |= bit_mask;
    }

    void reset(size_t num) {
        check_range(num);
        size_t byte_idx = num / 8;
        uint8_t bit_mask = ~(1 << (num % 8));
        bits_[byte_idx] &= bit_mask;
    }

    bool test(size_t num) const {
        check_range(num);
        size_t byte_idx = num / 8;
        uint8_t bit_mask = 1 << (num % 8);
        return (bits_[byte_idx] & bit_mask) != 0;
    }

private:
    void check_range(size_t num) const {
        assert(num < capacity_ && "Bitmap index out of range");
    }
};
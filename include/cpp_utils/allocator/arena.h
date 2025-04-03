#ifndef ARENA_H
#define ARENA_H

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <mutex>

namespace alp_utils {
  // A simple arena allocator
  // must be used in a single thread
  // must be plain type (no need to destruct)
  class Arena {
  private:
	  static constexpr int align = (sizeof(void *) > 8) ? sizeof(void *) : 8;
	  constexpr static int ARENA_BLOCK_SIZE = 4096;

	  uint64_t waste_{0};
	  uint64_t allocs_{0};
	  char *alloc_ptr{};
	  char *alloc_aligned_ptr{};
	  size_t alloc_bytes_remaining;
	  std::vector<char *> pool;

	  void allocate_ptr() {
		  alloc_ptr = allocate_new_block(ARENA_BLOCK_SIZE);

		  auto padding = reinterpret_cast<uintptr_t>(alloc_ptr) & (align - 1);

		  alloc_aligned_ptr = alloc_ptr - padding + ARENA_BLOCK_SIZE;

		  waste_ += ARENA_BLOCK_SIZE;
		  alloc_bytes_remaining = ARENA_BLOCK_SIZE - padding;
	  }

	  char *allocate_fall_back(size_t bytes) {
		  if (bytes > (ARENA_BLOCK_SIZE >> 2)) [[unlikely]] {
			  char *result = allocate_new_block(bytes);
			  return result;
		  }
		  allocate_ptr();

		  char *result = alloc_ptr;
		  alloc_ptr += bytes;
		  alloc_bytes_remaining -= bytes;
		  return result;
	  }

	  char *allocate_align_fallback(size_t bytes) {
		  if (bytes > (ARENA_BLOCK_SIZE >> 2)) {
			  char *result = allocate_new_block(bytes + align);
			  return reinterpret_cast<char *>((reinterpret_cast<uintptr_t>(result) + align - 1) & ~(align - 1));
		  }

		  allocate_ptr();

		  alloc_aligned_ptr -= bytes;

		  alloc_bytes_remaining -= bytes;
		  return alloc_aligned_ptr;
	  }

	  char *allocate_new_block(size_t bytes);

	  template<typename T>
	  void deallocate(T *ptr, size_t bytes) {
		  // do nothing :D
	  }

  public:
	  uint64_t get_waste() const {
		  return allocs_;
	  }

	  uint64_t get_used() const {
		  return allocs_;
	  }

	  Arena() : alloc_ptr(nullptr), alloc_aligned_ptr(nullptr), alloc_bytes_remaining(0) {};

	  ~Arena() {
		  for (auto &i: pool) {
			  delete[] i;
		  }
	  }

	  char *allocate(size_t bytes);

	  char *allocate_aligned(size_t bytes);
  };

  inline char *Arena::allocate(size_t bytes) {
	  allocs_ += bytes;
	  waste_ -= bytes;

	  if (bytes <= alloc_bytes_remaining) {
		  char *result = alloc_ptr;
		  alloc_ptr += bytes;
		  alloc_bytes_remaining -= bytes;
		  assert(alloc_aligned_ptr - alloc_ptr == alloc_bytes_remaining);
		  return result;
	  }
	  return allocate_fall_back(bytes);
  }

  inline char *Arena::allocate_new_block(size_t bytes) {
	  char *result = new char[bytes];
	  pool.push_back(result);
	  return result;
  }

  inline char *Arena::allocate_aligned(size_t bytes) {
	  // alloc_ptr % align
	  allocs_ += bytes;
	  waste_ -= bytes;
	  if (bytes <= alloc_bytes_remaining) {
		  alloc_aligned_ptr -= bytes;

		  char *result = alloc_aligned_ptr;
		  alloc_bytes_remaining -= bytes;
		  return result;
	  }

	  return allocate_align_fallback(bytes);
  }
}// namespace LSMKV

#endif//ARENA_H

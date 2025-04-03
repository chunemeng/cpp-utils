#pragma once

#include <concepts>
#include <memory>
#include <type_traits>

namespace alp_utils {
  template<typename Alloc, typename T>
  concept is_std_alloc = requires {
	  typename Alloc::value_type;
	  requires std::same_as<typename Alloc::value_type, T>;

	  requires requires(Alloc a, size_t n) {
		  { a.allocate(n) } -> std::same_as<T *>;
		  { a.deallocate(std::declval<T *>(), n) } -> std::same_as<void>;
	  };

	  requires requires{
		  requires std::is_same_v<
				  typename Alloc::size_type,
				  std::make_unsigned_t<typename Alloc::difference_type>
		  >;
	  };

	  requires requires{
		  typename std::allocator_traits<Alloc>::propagate_on_container_copy_assignment;
		  typename std::allocator_traits<Alloc>::propagate_on_container_move_assignment;
		  typename std::allocator_traits<Alloc>::propagate_on_container_swap;
	  };

	  requires requires(Alloc a, const Alloc other) {
		  { a == other } -> std::convertible_to<bool>;
		  { a != other } -> std::convertible_to<bool>;
	  };

	  requires requires{
		  typename std::allocator_traits<Alloc>::template rebind_alloc<int>;
	  };
  };

  template<typename Alloc, typename T>
  constexpr bool is_std_alloc_v = is_std_alloc<Alloc, T>;

} // namespace alp_utils
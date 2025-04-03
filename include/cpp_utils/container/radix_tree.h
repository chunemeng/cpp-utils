#ifndef CPP_UTILS_CONTAINER_RADIX_TREE_H
#define CPP_UTILS_CONTAINER_RADIX_TREE_H
#include <cstdint>

namespace alp_utils {
	namespace detail {
		template <typename T>
		concept string_like = requires(T t) {
			{ t.size() } -> std::convertible_to<size_t>;

			requires requires {
				{ t.data() } -> std::convertible_to<const char*>;
			} || requires {
				{ t.data() } -> std::convertible_to<const std::byte*>;
			} || requires {
				{ t.data() } -> std::convertible_to<const uint8_t*>;
			} || requires {
				{ t.data() } -> std::convertible_to<const int8_t*>;
			};
		};

		static constexpr inline uint32_t dynamic_extent = static_cast<uint32_t>(-1);


		enum class node_type : uint8_t { leaf, node_4, node_16, node_48, node_256 };

		class node {
		public:
			node() = default;

			~node() = default;

			node(const node&) = delete;

			node& operator=(const node&) = delete;

			node(node&&) noexcept = delete;

			node& operator=(node&&) noexcept = delete;

			void insert() {}

			node* find() { return nullptr; }

		private:
			node_type type_;
		};

		template<typename K, typename V>
		class node_impl : public node {

			std::string keys;

			std::vector<node*> children;
		};

		template <typename K, typename V>
		class leaf_node {};


	} // namespace detail


	template <typename K, typename V, typename Cmp, typename Alloc, uint32_t len = detail::dynamic_extent>
	class radix_tree {
	public:
		static constexpr bool key_as_string = detail::string_like<K>;

		using key_type = std::conditional_t<key_as_string, std::string, std::hash<K>>;

	public:
		radix_tree() = default;

		~radix_tree() = default;

		radix_tree(const radix_tree&) = delete;

		radix_tree& operator=(const radix_tree&) = delete;

		radix_tree(radix_tree&&) noexcept = delete;

		radix_tree& operator=(radix_tree&&) noexcept = delete;

	private:
		[[no_unique_address]] Cmp cmp_;
		[[no_unique_address]] Alloc alloc_;


		std::vector<detail::leaf_node> leaf_nodes_;
	};
} // namespace alp_utils

#endif // CPP_UTILS_CONTAINER_RADIX_TREE_H

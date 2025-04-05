#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_set>

namespace alp_utils::hazp {
    namespace detail {
        // NOTE: m_next_ is used in reclaimer free_list, and local_holder free_list
        //       next_ is used in reclaimer holder_storage
        struct holder {
            void set_next(holder *next) noexcept {
                m_next_.store(next, std::memory_order_relaxed);
            }

            void set_barrier_next(holder *next) noexcept {
                m_next_.store(next, std::memory_order_relaxed);
            }

            holder *barrier_next() const noexcept {
                return m_next_.load(std::memory_order_acquire);
            }

            holder *next() const noexcept {
                return m_next_.load(std::memory_order_relaxed);
            }

            void set_main_next(holder *next) noexcept {
                next_.store(next, std::memory_order_release);
            }

            holder *main_next() const noexcept {
                return next_.load(std::memory_order_acquire);
            }

            static constexpr uintptr_t NOUSE = 0x1;
            static constexpr uintptr_t INUSE = 0x0;

            std::atomic<holder *> m_next_{nullptr};

            std::atomic<holder *> next_{nullptr};
            std::atomic<uintptr_t> ptr{INUSE};
        };

        // below two list classes are from folly
        template<typename Node>
        class linked_list {
            Node *head_{nullptr};
            Node *tail_{nullptr};
            int size_{0};
        public:
            linked_list() noexcept: head_(nullptr), tail_(nullptr) {}

            explicit linked_list(Node *head, Node *tail) noexcept: head_(head), tail_(tail) {}

            Node *head() const noexcept {
                return head_;
            }

            Node *tail() const noexcept {
                return tail_;
            }

            int count() const noexcept {
                return size_;
            }

            bool empty() const noexcept {
                return head() == nullptr;
            }

            void push(Node *node) noexcept {
                node->set_next(nullptr);
                if (tail_) {
                    tail_->set_next(node);
                } else {
                    head_ = node;
                }
                size_++;
                tail_ = node;
            }

            void splice(linked_list &list) {
                if (list.empty()) {
                    return;
                }

                if (head_ == nullptr) {
                    head_ = list.head();
                    size_ = list.count();
                } else {
                    size_ += list.count();
                    tail_->set_next(list.head());
                }
                tail_ = list.tail();
                list.clear();
            }

            void clear() {
                size_ = 0;
                head_ = nullptr;
                tail_ = nullptr;
            }

        }; // linked_list

        template<typename Node>
        struct forward_list {
            uint32_t size_{0};
            Node *head_{nullptr};

            void push(Node *node) noexcept {
                node->set_next(head_);
                head_ = node;
                size_++;
            }

            Node *pop() noexcept {
                auto node = head_;
                if (node) {
                    size_--;
                    head_ = node->next();
                }
                return node;
            }

            void clear() {
                head_ = nullptr;
            }

            bool empty() const noexcept {
                return head_ == nullptr;
            }

            uint32_t size() const noexcept {
                return size_;
            }
        };

        template<typename Node>
        class shared_head_only_list {
            std::atomic<uintptr_t> head_{0};

        public:
            void push(Node *node) noexcept {
                if (node == nullptr) {
                    return;
                }
                auto oldval = head();
                auto newval = reinterpret_cast<uintptr_t>(node);

                while (true) {
                    auto ptrval = oldval;

                    auto ptr = reinterpret_cast<Node *>(ptrval);
                    node->set_next(ptr);
                    if (cas_head(oldval, newval)) {
                        break;
                    }
                }
            }

            void push_list(linked_list<Node> &list) noexcept {
                if (list.empty()) [[unlikely]] {
                    return;
                }
                auto oldval = head();

                while (true) {
                    auto newval = reinterpret_cast<uintptr_t>(list.head());

                    auto ptrval = oldval;

                    auto ptr = reinterpret_cast<Node *>(ptrval);
                    list.tail()->set_next(ptr);
                    if (cas_head(oldval, newval)) {
                        break;
                    }
                }
            }

            Node *pop_all() noexcept {
                return pop_all_no_lock();
            }

            bool empty() const noexcept {
                return head() == 0U;
            }

        private:
            uintptr_t head() const noexcept {
                return head_.load(std::memory_order_acquire);
            }

            uintptr_t exchange_head() noexcept {
                auto newval = reinterpret_cast<uintptr_t>(nullptr);
                auto oldval = head_.exchange(newval, std::memory_order_acq_rel);
                return oldval;
            }

            bool cas_head(uintptr_t &oldval, uintptr_t newval) noexcept {
                return head_.compare_exchange_weak(oldval, newval, std::memory_order_acq_rel,
                                                   std::memory_order_acquire);
            }

            Node *pop_all_no_lock() noexcept {
                auto oldval = exchange_head();
                return reinterpret_cast<Node *>(oldval);
            }

        }; // shared_head_only_list

        template<typename Node>
        struct concurrent_forward_list {
            std::atomic<Node *> head_{nullptr};

            bool empty() const noexcept {
                return head_.load(std::memory_order_acquire) == nullptr;
            }

            void push(Node *node, void(Node::*set_next)(Node *)) noexcept {
                Node *expected_head = head_.load(std::memory_order_acquire);
                do {
                    (node->*set_next)(expected_head);
                } while (!head_.compare_exchange_weak(
                        expected_head,
                        node,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed
                ));
            }

            Node *pop(Node *(Node::*next_fun)() const) noexcept {
                Node *expected_head = head_.load(std::memory_order_acquire);
                while (expected_head != nullptr) {
                    Node *next = (expected_head->*next_fun)();
                    if (head_.compare_exchange_weak(
                            expected_head,
                            next,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed
                    )) {
                        return expected_head;
                    }
                }
                return nullptr;
            }
        };

        struct read_write_lock {
        private:
            static constexpr uint32_t WRITE_LOCK_MASK = 0x80000000;
            std::atomic<uint32_t> state{0};

        public:
            bool try_lock_shared() {
                uint32_t expected = state.load(std::memory_order_relaxed);
                if ((expected & WRITE_LOCK_MASK) == 0) {
                    uint32_t const desired = expected + 1;
                    return state.compare_exchange_strong(
                            expected, desired,
                            std::memory_order_acquire,
                            std::memory_order_relaxed
                    );
                }
                return false;
            }

            bool try_lock() {
                uint32_t expected = 0;
                return state.compare_exchange_strong(
                        expected, WRITE_LOCK_MASK,
                        std::memory_order_acquire,
                        std::memory_order_relaxed
                );
            }

            void lock_shared() {
                uint32_t expected = state.load(std::memory_order_relaxed);
                while (true) {
                    if ((expected & WRITE_LOCK_MASK) == 0) {
                        uint32_t const desired = expected + 1;
                        if (state.compare_exchange_weak(
                                expected, desired,
                                std::memory_order_acquire,
                                std::memory_order_relaxed
                        )) {
                            break;
                        }
                    } else {
                        std::this_thread::yield();
                        expected = state.load(std::memory_order_relaxed);
                    }
                }
            }

            void unlock_shared() {
                if (state.fetch_sub(1, std::memory_order_release) == 1) {
                    state.notify_all();
                }
            }

            void lock() {
                uint32_t expected = 0;
                while (!state.compare_exchange_weak(
                        expected, WRITE_LOCK_MASK,
                        std::memory_order_acquire,
                        std::memory_order_relaxed
                )) {
                    state.wait(0, std::memory_order_relaxed);
                    expected = 0;
                }
            }

            void unlock() {
                state.store(0, std::memory_order_release);
                state.notify_all();
            }
        };
    } // namespace detail

    class hazard_ptr {
    private:
        detail::holder *holder_;
    public:
        hazard_ptr(detail::holder *holder = nullptr) : holder_(holder) {}

        hazard_ptr(const hazard_ptr &) = delete;

        hazard_ptr &operator=(const hazard_ptr &) = delete;

        hazard_ptr(hazard_ptr &&other) noexcept: holder_(other.holder_) {
            other.holder_ = nullptr;
        }

        hazard_ptr &operator=(hazard_ptr &&other) noexcept;

        ~hazard_ptr();

        template<class T>
        T *try_protect(std::atomic<T *> &ptr) {
            // bellow acquire | release may replace with relaxed(?)
            T *plain_ptr = ptr.load(std::memory_order_acquire);
            holder_->ptr.store(reinterpret_cast<uintptr_t>(plain_ptr), std::memory_order_release);
            return plain_ptr;
        }

        template<class T>
        T *protect(std::atomic<T *> &ptr) {
            while (true) {
                auto plain_ptr = try_protect(ptr);
                std::atomic_thread_fence(std::memory_order_seq_cst);
                auto ptr_val = ptr.load(std::memory_order_acquire);
                if (plain_ptr == ptr_val) [[likely]] {
                    return plain_ptr;
                }
            }
        }

        void reset() { unmark(); }

    private:
        void unmark() {
            holder_->ptr.store(detail::holder::INUSE, std::memory_order_release);
        }
    };

    template<uint8_t size>
    using hazptr_array = std::array<hazard_ptr, size>;

    class reclaimer {
    public:
        reclaimer() = default;

        ~reclaimer() {
            reclaim_all_objects();
        }

        // This class is not copyable
        reclaimer(const reclaimer &) = delete;

        reclaimer &operator=(const reclaimer &) = delete;

        // This class is not movable
        reclaimer(reclaimer &&) = delete;

        reclaimer &operator=(reclaimer &&) = delete;

    private:
        class local_holder final {
        public:
            local_holder() = default;

            ~local_holder() {
                for (auto &node: holder_storage) {
                    node->ptr.store(detail::holder::NOUSE, std::memory_order_relaxed);
                }
                free_list.clear();
                holder_storage.clear();
            }

            local_holder(local_holder &&) = delete;

            local_holder &operator=(local_holder &&) = delete;

            local_holder(const local_holder &) = delete;

            local_holder &operator=(const local_holder &) = delete;

            template<uint8_t size>
            std::array<hazard_ptr, size> get_hazard_ptrs() {
                if (free_list.size() < size) {
                    reserve_hazp(size);
                }
                std::array<hazard_ptr, size> hazard_ptrs;
                for (auto i = 0; i < size; ++i) {
                    hazard_ptrs[i] = free_list.pop();
                }
                return hazard_ptrs;
            }

            void evict_hazard_ptr() {
                auto head = free_list.head_;

                while (head != nullptr) {
                    head->ptr.store(detail::holder::INUSE, std::memory_order_relaxed);
                    holder_storage.erase(head);
                    auto ptr = head;
                    head = head->next();

                    reclaimer::instance().reuse(ptr);
                }

                free_list.clear();
            }

            hazard_ptr get_hazard_ptr() {
                if (free_list.empty()) {
                    auto new_holder = instance().make_holder();
                    holder_storage.emplace(new_holder);
                    return {new_holder};
                }

                auto hzard_ptr = free_list.pop();
                return hzard_ptr;
            }

            void reserve_hazp(uint8_t size) {
                auto old_size = free_list.size();

                if (old_size >= size) [[likely]] {
                    return;
                }

                for (auto i = old_size; i < size; ++i) {
                    auto new_holder = instance().make_holder();
                    free_list.push(new_holder);
                    holder_storage.emplace(new_holder);
                }
            }

            void reuse(detail::holder *holder) {
                free_list.push(holder);
            }

            static local_holder &get_instance() {
                static thread_local local_holder instance;
                return instance;
            }

        private:
            detail::forward_list<detail::holder> free_list{};
            std::unordered_set<detail::holder *> holder_storage{};
        };

        struct retire_node {
            virtual const void *raw_ptr() const { return nullptr; }

            virtual ~retire_node() = default;

            void set_next(retire_node *next) { next_ = next; }

            retire_node *next() const { return next_; }

            retire_node *next_{nullptr};
        };

        struct free_list : public detail::concurrent_forward_list<detail::holder> {
            free_list() = default;

            free_list(const free_list &) = delete;

            free_list &operator=(const free_list &) = delete;

            free_list(free_list &&) = delete;

            free_list &operator=(free_list &&) = delete;

            void push(detail::holder *holder) {
                concurrent_forward_list::push(holder, &detail::holder::set_barrier_next);
            }

            detail::holder *pop() {
                return concurrent_forward_list::pop(&detail::holder::barrier_next);
            }

            detail::holder *pop_all() {
                detail::holder *newval = nullptr;
                return head_.exchange(newval, std::memory_order_acq_rel);
            }
        };

        struct holder_list : public detail::concurrent_forward_list<detail::holder> {
            std::atomic<uint32_t> size_{0};
            mutable detail::read_write_lock rw_lock_{};

            holder_list() = default;

            holder_list(const holder_list &) = delete;

            holder_list &operator=(const holder_list &) = delete;

            holder_list(holder_list &&) = delete;

            holder_list &operator=(holder_list &&) = delete;

            ~holder_list() {
                auto node = head_.load()->next_.load();
                while (node != nullptr) {
                    auto next = node->next_.load();

                    while (node->ptr.load() != detail::holder::NOUSE) {
                        std::this_thread::yield();
                    }

                    delete node;

                    node = next;
                }
                delete head_.load();
            }

            uint32_t size() const {
                return size_.load(std::memory_order_relaxed);
            }

            void push(detail::holder *holder) {
                rw_lock_.lock_shared();
                size_.fetch_add(1, std::memory_order_relaxed);
                concurrent_forward_list::push(holder, &detail::holder::set_main_next);
                rw_lock_.unlock_shared();
            }

            void compact(detail::holder *compacted_list) {
                std::unordered_set<detail::holder *> set;
                while (compacted_list != nullptr) {
                    set.insert(compacted_list);
                    compacted_list = compacted_list->next();
                }

                rw_lock_.lock();

                auto head = head_.load(std::memory_order_acquire);
                uint32_t size = 0;
                detail::holder *new_head = nullptr;

                while (head != nullptr) {
                    if (!set.contains(head)) {
                        new_head = head;
                        size++;
                        head = head->main_next();
                        break;
                    } else {
                        head = head->main_next();
                    }
                }

                auto tmp = new_head;

                while (head != nullptr) {
                    if (!set.contains(head)) {
                        tmp->set_main_next(head);
                        tmp = head;
                        size++;
                    }
                    head = head->main_next();
                }

                if (tmp != nullptr) {
                    tmp->set_main_next(nullptr);
                }

                head_.store(new_head, std::memory_order_release);
                size_.store(size, std::memory_order_relaxed);
                rw_lock_.unlock();

                for (auto &node: set) {
                    delete node;
                }
            }
        };

        using retired_list = detail::shared_head_only_list<retire_node>;
        using list = detail::linked_list<retire_node>;

        constexpr static uint32_t kNumShards = 8;

        static constexpr uint32_t kShardMask = kNumShards - 1;

        static constexpr int kReclaimThreshold = 1000;

        static constexpr uint64_t kSyncTimePeriod{2000000000}; // nanoseconds

        friend class hazard_ptr;

    public:
        static reclaimer &instance() {
            static reclaimer instance;
            return instance;
        }

        static void evict_hazard_ptr() {
            get_instance().evict_hazard_ptr();
        }

        void delete_hazard_ptr() {
            auto list = free_list_.pop_all();
            holder_list_.compact(list);
        }

        void reuse(detail::holder *holder) {
            free_list_.push(holder);
        }

        static void reserve_hazp(uint8_t size) { return get_instance().reserve_hazp(size); }

        template<uint8_t size>
        static std::array<hazard_ptr, size> make_hazard_ptr() { return get_instance().get_hazard_ptrs<size>(); }

        static hazard_ptr make_hazard_ptr() { return get_instance().get_hazard_ptr(); }

        template<typename T, typename D = std::default_delete<T>>
        void retire(T *ptr, D &&deleter = {}) {
            struct retire_node_impl : public retire_node {
                explicit retire_node_impl(std::unique_ptr<T, D> ptr) : ptr_(std::move(ptr)) {}

                const void *raw_ptr() const override { return ptr_.get(); }

                ~retire_node_impl() override = default;

                std::unique_ptr<T, D> ptr_;
            };

            auto unique_ptr = std::unique_ptr<T, D>(ptr, std::forward<D>(deleter));

            auto retired = new retire_node_impl(std::move(unique_ptr));
            push_list(retired);
        }

        void reclaim() {
            inc_num_bulk_reclaims();
            do_reclamation(0);

            wait_for_zero_bulk_reclaims();
        }

    private:
        detail::holder *make_new_holder() {
            auto holder = new detail::holder();
            holder_list_.push(holder);
            return holder;
        }

        detail::holder *make_holder() {
            if (!free_list_.empty()) [[unlikely]] {
                auto holder = free_list_.pop();
                return holder ? holder : make_new_holder();
            }
            return make_new_holder();
        }

        static local_holder &get_instance() { return local_holder::get_instance(); }

        void reclaim_all_objects() {
            for (auto &s: retired_list_) {
                retire_node *head = s.pop_all();
                reclaim_obj(head);
            }
        }

        int check_due_time() {
            uint64_t const time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            auto due = load_due_time();
            if (time < due || !cas_due_time(due, time + kSyncTimePeriod)) {
                return 0;
            }
            int const rcount = exchange_count(0);
            if (rcount < 0) {
                add_count(rcount);
                return 0;
            }
            return rcount;
        }

        void check_threshold_and_reclaim() {
            int rcount = check_count_threshold();
            if (rcount == 0) {
                rcount = check_due_time();
                if (rcount == 0) {
                    return;
                }
            }

            inc_num_bulk_reclaims();
            do_reclamation(rcount);
        }

        template<typename Cond>
        void list_match_condition(retire_node *obj, list &match, list &nomatch, const Cond &cond) {
            while (obj != nullptr) {
                auto next = obj->next();
                assert(obj != next);
                if (cond(obj)) {
                    match.push(obj);
                } else {
                    nomatch.push(obj);
                }
                obj = next;
            }
        }

        bool retired_list_empty() {
            // retired_list_ is std::array<retired_list, kNumShards>
            for (int s = 0; s < kNumShards; ++s) {
                if (!retired_list_[s].empty()) {
                    return false;
                }
            }

            return true;
        }

        void reclaim_obj(retire_node *obj) {
            while (obj != nullptr) {
                auto next = obj->next();
                delete obj;
                obj = next;
            }
        }

        int
        match_reclaim(std::array<retire_node *, kNumShards> retired, std::unordered_set<const void *> &hash_set,
                      bool &done) {
            done = true;
            list not_reclaimed;
            int count = 0;
            for (auto &retire_nodes: retired) {
                list match;
                list nomatch;
                list_match_condition(retire_nodes, match, nomatch,
                                     [&](retire_node *obj) { return hash_set.contains(obj->raw_ptr()); });
                count += nomatch.count();

                reclaim_obj(nomatch.head());
                if (!retired_list_empty()) {
                    done = false;
                }
                not_reclaimed.splice(match);
            }

            list remain_list(not_reclaimed.head(), not_reclaimed.tail());
            retired_list_[0].push_list(remain_list);
            return count;
        }

        void do_reclamation(int rcount) {
            assert(rcount >= 0);

            while (true) {
                std::array<retire_node *, kNumShards> retired{};
                bool done = true;
                if (extract_retired_objects(retired)) {
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    std::unordered_set<const void *> hash_set = load_hazptr_vals();
                    rcount -= match_reclaim(retired, hash_set, done);
                }

                if (rcount != 0) {
                    add_count(rcount);
                }

                rcount = check_count_threshold();
                if (rcount == 0 && done) {
                    break;
                }
            }
            dec_num_bulk_reclaims();
        }

        bool extract_retired_objects(std::array<retire_node *, kNumShards> &list) {
            bool empty = true;

            for (int shared = 0; shared < kNumShards; ++shared) {
                list[shared] = retired_list_[shared].pop_all();
                if (list[shared] != nullptr) {
                    empty = false;
                }
            }
            return !empty;
        }

        int check_count_threshold() {
            int rcount = load_count();
            while (rcount >= kReclaimThreshold) {
                if (cas_count(rcount, 0)) {
                    return rcount;
                }
            }
            return 0;
        }

        [[nodiscard]] std::unordered_set<const void *> load_hazptr_vals() const {
            holder_list_.rw_lock_.lock_shared();
            auto head = holder_list_.head_.load(std::memory_order_acquire);
            // std::unordered_set is slower than what I can imagine :(
            std::unordered_set<const void *> protected_ptrs;

            protected_ptrs.reserve(holder_list_.size());

            while (head != nullptr) {
                auto ptr = head->ptr.load(std::memory_order_acquire);

                if (ptr == detail::holder::NOUSE || ptr == detail::holder::INUSE) {
                    head = head->next_.load(std::memory_order_acquire);
                    continue;
                }

                protected_ptrs.insert(reinterpret_cast<const void *>(ptr));
                head = head->next_.load(std::memory_order_acquire);
            }
            holder_list_.rw_lock_.unlock_shared();

            return protected_ptrs;
        }

        int add_count(int rcount) { return count_.fetch_add(rcount, std::memory_order_release); }

        int load_count() { return count_.load(std::memory_order_acquire); }

        int exchange_count(int rcount) { return count_.exchange(rcount, std::memory_order_acq_rel); }

        bool cas_count(int &expected, int desired) {
            return count_.compare_exchange_weak(
                    expected, desired, std::memory_order_acq_rel, std::memory_order_relaxed);
        }

        uint64_t load_due_time() { return due_time_.load(std::memory_order_acquire); }

        void set_due_time() {
            uint64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            due_time_.store(time + kSyncTimePeriod, std::memory_order_release);
        }

        bool cas_due_time(uint64_t &expected, uint64_t newval) {
            return due_time_.compare_exchange_strong(
                    expected, newval, std::memory_order_acq_rel, std::memory_order_relaxed);
        }


        void push_list(retire_node *ptr) {
            // NOTE: 4 bit may already skip the empty bits.
            auto hash = reinterpret_cast<uintptr_t>(ptr) >> 8U;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            retired_list_[hash & kShardMask].push(ptr);
            add_count(1);

            check_threshold_and_reclaim();
        }

        uint16_t load_num_bulk_reclaims() {
            return num_bulk_reclaims_.load(std::memory_order_acquire);
        }

        void inc_num_bulk_reclaims() {
            num_bulk_reclaims_.fetch_add(1, std::memory_order_release);
        }

        void dec_num_bulk_reclaims() {
            assert(load_num_bulk_reclaims() > 0);
            num_bulk_reclaims_.fetch_sub(1, std::memory_order_release);
        }

        void wait_for_zero_bulk_reclaims() {
            while (load_num_bulk_reclaims() > 0) {
                std::this_thread::yield();
            }
        }

        holder_list holder_list_{};

        free_list free_list_{};

        std::atomic<int> count_{0};
        std::atomic<uint64_t> due_time_{0};


        std::array<retired_list, kNumShards> retired_list_{};

        std::atomic<uint16_t> num_bulk_reclaims_{0};
    };

    hazard_ptr &hazard_ptr::operator=(hazard_ptr &&other) noexcept {
        if (this == &other) [[unlikely]] {
            return *this;
        }

        if (holder_ != nullptr) [[unlikely]] {
            unmark();
            reclaimer::get_instance().reuse(holder_);
            holder_ = nullptr;
        }

        std::swap(holder_, other.holder_);

        return *this;
    }

    hazard_ptr::~hazard_ptr() {
        if (holder_ == nullptr) [[unlikely]] {
            return;
        }

        unmark();
        reclaimer::get_instance().reuse(holder_);
    }


    // make number of size's hazard pointers available
    static inline void reserve_hazp(uint8_t size) { reclaimer::reserve_hazp(size); }

    template<uint8_t size>
    static inline std::array<hazard_ptr, size> make_hazard_ptr() {
        static_assert(size > 0, "size must be greater than 0");
        return reclaimer::make_hazard_ptr<size>();
    }

    static inline hazard_ptr make_hazard_ptr() { return reclaimer::make_hazard_ptr(); }

    // retire the object
    template<typename T, typename D = std::default_delete<T>>
    static inline void retire(T *ptr, D deleter = {}) {
        reclaimer::instance().retire(ptr, deleter);
    }

    static inline void reclaim() {
        reclaimer::instance().reclaim();
    }

    static inline void evict_hazard_ptr() {
        reclaimer::instance().evict_hazard_ptr();
    }

    static inline void delete_hazard_ptr() {
        reclaimer::instance().delete_hazard_ptr();
    }


    template<uint8_t size>
    class hazard_local {
        hazptr_array<size> hazptrs_;

    public:
        hazard_local() : hazptrs_(reclaimer::make_hazard_ptr<size>()) {
        }

        ~hazard_local() {
            for (auto &hzard_ptr: hazptrs_) {
                hzard_ptr.reset();
            }
        }

        hazard_ptr &operator[](size_t index) {
            assert(index < size);
            return hazptrs_[index];
        }
    };

} // namespace alp_utils::hazp

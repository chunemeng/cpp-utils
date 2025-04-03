#include <cpp_utils/concurrency/hazard_ptr.h>
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <iomanip>
#include <barrier>

static constexpr int64_t FLAGS_num_reps = 10;
static constexpr int64_t FLAGS_num_threads = 6;
static constexpr int64_t FLAGS_num_ops = 1003;

using namespace alp_utils::hazp;

// Structures

/** Count */
class Count {
    std::atomic<int> ctors_{0};
    std::atomic<int> dtors_{0};
    std::atomic<int> retires_{0};

public:
    void clear() noexcept {
        ctors_.store(0);
        dtors_.store(0);
        retires_.store(0);
    }

    int ctors() const noexcept { return ctors_.load(); }

    int dtors() const noexcept { return dtors_.load(); }

    int retires() const noexcept { return retires_.load(); }

    void inc_ctors() noexcept { ctors_.fetch_add(1); }

    void inc_dtors() noexcept { dtors_.fetch_add(1); }

    void inc_retires() noexcept { retires_.fetch_add(1); }
}; // Count

static Count c_;

/** Node */
class Node {
    int val_;
    std::atomic<Node *> next_;

public:
    explicit Node(int v = 0, Node *n = nullptr, bool = false) noexcept
            : val_(v), next_(n) {
        c_.inc_ctors();
    }

    ~Node() { c_.inc_dtors(); }

    int value() const noexcept { return val_; }

    Node *next() const noexcept {
        return next_.load(std::memory_order_acquire);
    }

    std::atomic<Node *> *ptr_next() noexcept { return &next_; }
}; // Node

/** NodeRC */
template<bool Mutable>
class NodeRC {
    std::atomic<NodeRC<Mutable> *> next_;
    int val_;

public:
    explicit NodeRC(int v = 0, NodeRC *n = nullptr, bool acq = false) noexcept
            : next_(n), val_(v) {
        c_.inc_ctors();
        if (acq) {
//            if (Mutable) {
//                this->acquire_link_safe();
//            } else {
//                this->acquire_ref_safe();
//            }
        }
    }

    ~NodeRC() { c_.inc_dtors(); }

    int value() const noexcept { return val_; }

    NodeRC<Mutable> *next() const noexcept {
        return next_.load(std::memory_order_acquire);
    }

    template<typename S>
    void push_links(bool m, S &s) {
        if (Mutable == m) {
            auto p = next();
            if (p) {
                s.push(p);
            }
        }
    }
}; // NodeRC

/** List */
template<typename T>
struct List {
    std::atomic<T *> head_{nullptr};

public:
    explicit List(int size) {
        auto p = head_.load(std::memory_order_relaxed);
        for (int i = 0; i < size - 1; ++i) {
            p = new T(i + 10000, p, true);
        }
        p = new T(size + 9999, p);
        head_.store(p, std::memory_order_relaxed);
    }

    ~List() {
        auto curr = head_.load(std::memory_order_relaxed);
        while (curr) {
            auto next = curr->next();

            curr = next;
        }
    }

    bool hand_over_hand(
            int val, hazard_ptr *hptr_prev, hazard_ptr *hptr_curr) {
        while (true) {
            auto prev = &head_;
            auto curr = prev->load(std::memory_order_acquire);
            while (true) {
                if (!curr) {
                    return false;
                }
                if (!hptr_curr->try_protect(*prev)) {
                    break;
                }
                auto next = curr->next();
                if (prev->load(std::memory_order_acquire) != curr) {
                    break;
                }
                if (curr->value() == val) {
                    return true;
                }
                prev = curr->ptr_next();
                curr = next;
                std::swap(hptr_curr, hptr_prev);
            }
        }
    }

    bool hand_over_hand(int val) {
        auto hptr = make_hazard_ptr<2>();
        return hand_over_hand(val, &hptr[0], &hptr[1]);
    }

    bool protect_all(int val, hazard_ptr &hptr) {
        auto curr = hptr.protect(head_);
        while (curr) {
            auto next = curr->next();
            if (curr->value() == val) {
                return true;
            }
            curr = next;
        }
        return false;
    }

    bool protect_all(int val) {
        auto hptr = make_hazard_ptr<1>();
        return protect_all(val, hptr[0]);
    }
}; // List

/** NodeAuto */
class NodeAuto {
    std::atomic<NodeAuto *> link_[2];

public:
    explicit NodeAuto(NodeAuto *l1 = nullptr, NodeAuto *l2 = nullptr) noexcept {
        link_[0].store(l1, std::memory_order_relaxed);
        link_[1].store(l2, std::memory_order_relaxed);
        c_.inc_ctors();
    }

    ~NodeAuto() { c_.inc_dtors(); }

    NodeAuto *link(size_t i) {
        return link_[i].load(std::memory_order_acquire);
    }

    template<typename S>
    void push_links(bool m, S &s) {
        if (m == false) { // Immutable
            for (int i = 0; i < 2; ++i) {
                auto p = link(i);
                if (p) {
                    s.push(p);
                }
            }
        }
    }
}; // NodeAuto

// Test Functions

//void basic_objects_test() {
//    c_.clear();
//    int num = 0;
//    {
//        ++num;
//        auto obj = new Node();
//        retire(obj);
//    }
//    {
//        ++num;
//        auto obj = new NodeRC<false>(0, nullptr);
//        retire(obj);
//    }
//    {
//        ++num;
//        auto obj = new NodeRC<false>(0, nullptr);
//
//        obj->acquire_link_safe();
//        obj->unlink();
//    }
//    {
//        ++num;
//        auto obj = new NodeRC<false>(0, nullptr);
//        obj->acquire_link_safe();
//        obj->unlink_and_reclaim_unchecked();
//    }
//    {
//        ++num;
//        auto obj = new NodeRC<false>(0, nullptr);
//        obj->acquire_link_safe();
//        hazptr_root <NodeRC<false>> root(obj);
//    }
//    ASSERT_EQ(c_.ctors(), num);
//    reclaim();
//    ASSERT_EQ(c_.dtors(), num);
//}

void copy_and_move_test() {
    struct Obj {
        int a;
    };

    auto p1 = new Obj();
    auto p2 = new Obj(*p1);
    retire(p1);
    retire(p2);

    p1 = new Obj();
    p2 = new Obj(std::move(*p1));
    retire(p1);
    retire(p2);

    p1 = new Obj();
    p2 = new Obj();
    *p2 = *p1;
    retire(p1);
    retire(p2);

    p1 = new Obj();
    p2 = new Obj();
    *p2 = std::move(*p1);
    retire(p1);
    retire(p2);

    reclaim();
}

void basic_holders_test() {

    { auto h = make_hazard_ptr(); }
    { auto h = make_hazard_ptr<2>(); }
}

void basic_protection_test() {
    c_.clear();
    auto obj = new Node;
    auto h = make_hazard_ptr();
    std::atomic<Node *> p = obj;
    h.protect(p);
    retire(obj);
    ASSERT_EQ(c_.ctors(), 1);
    reclaim();
    ASSERT_EQ(c_.dtors(), 0);
    h.reset();
    reclaim();
    ASSERT_EQ(c_.dtors(), 1);
}

void virtual_test() {
    struct Thing {
        virtual ~Thing() {}

        int a;
    };
    for (int i = 0; i < 100; i++) {
        auto bar = new Thing;
        bar->a = i;

        auto hptr = make_hazard_ptr();
        std::atomic<Thing *> p = bar;
        hptr.protect(p);
        retire(bar);
        ASSERT_EQ(bar->a, i);
    }

    reclaim();
}

void destruction_test(reclaimer &domain) {
    struct Thing {
        Thing *next;
        reclaimer *domain;
        int val;

        Thing(int v, Thing *n, reclaimer *d)
                : next(n), domain(d), val(v) {}

        ~Thing() {
            if (next) {
                domain->retire(next);
            }
        }
    };
    Thing *last{nullptr};
    for (int i = 0; i < 2000; i++) {
        last = new Thing(i, last, &domain);
    }
    domain.retire(last);
    reclaim();
}

void destruction_protected_test(reclaimer &domain) {
    struct Rec;

    struct RecState {
        reclaimer &domain;
        std::atomic<Rec *> cell{};
    };

    struct Rec {
        int rem_;
        RecState &state_;

        Rec(int rem, RecState &state) : rem_{rem}, state_{state} {}

        ~Rec() { go(rem_, state_); }

        static void go(int rem, RecState &state) {
            if (rem) {
                auto p = new Rec(rem - 1, state);
                state.cell.store(p, std::memory_order_relaxed);
                auto h = state.domain.make_hazard_ptr();
                h.protect(state.cell);
                state.cell.store(nullptr, std::memory_order_relaxed);
                state.domain.retire(p);
            }
        }
    };

    RecState state{domain};
    Rec::go(2000, state);

    reclaim();
}

void move_test() {
    for (int i = 0; i < 100; ++i) {
        auto x = new Node(i);
        hazard_ptr hptr0 = make_hazard_ptr();
        // Protect object
        std::atomic<Node *> p = x;
        hptr0.protect(p);
        // Retire object
        retire(x);
        // Move constructor - still protected
        hazard_ptr hptr1(std::move(hptr0));
        // Self move is no-op - still protected
        auto phptr1 = &hptr1;
        ASSERT_EQ(phptr1, &hptr1);
        hptr1 = std::move(*phptr1);
        // Empty constructor
        hazard_ptr hptr2 = std::move(hptr1);
        // Access object
        ASSERT_EQ(x->value(), i);
        // Unprotect object - hptr2 is nonempty
        hptr2.reset();
    }
    reclaim();
}

void array_test() {
    for (int i = 0; i < 100; ++i) {
        auto x = new Node(i);
        auto hptr = make_hazard_ptr<3>();
        // Protect object
        std::atomic<Node *> p = x;
        hptr[2].protect(p);
        // Empty array
        auto h = make_hazard_ptr<3>();
        // Move assignment
        h = std::move(hptr);
        // Retire object
        retire(x);
        ASSERT_EQ(x->value(), i);
        // Unprotect object - hptr2 is nonempty
        h[2].reset();
    }
    reclaim();
}

void array_dtor_full_tc_test() {
    const uint8_t M = 3;
    {
        // Fill the thread cache
        auto w = make_hazard_ptr<M>();
    }
    {
        // Empty array x
        std::array<hazard_ptr, M> x;
        {
            // y ctor gets elements from the thread cache filled by w dtor.
            auto y = make_hazard_ptr<M>();
            // z ctor gets elements from the default domain.
            auto z = make_hazard_ptr<M>();
            // Elements of y are moved to x.
            x = std::move(y);
            // z dtor fills the thread cache.
        }
        // x dtor finds the thread cache full. It has to call
        // ~hazptr_holder() for each of its elements, which were
        // previously taken from the thread cache by y ctor.
    }
}

void local_test() {
    for (int i = 0; i < 100; ++i) {
        auto x = new Node(i);

        auto hptr = make_hazard_ptr<3>();
        // Protect object
        std::atomic<Node *> p = x;
        hptr[2].protect(p);
        // Retire object
        retire(x);
        hptr[2].reset();
    }
    reclaim();
}

//template<bool Mutable>
//void linked_test() {
//    c_.clear();
//    NodeRC<Mutable> *p = nullptr;
//    int num = 193;
//    for (int i = 0; i < num - 1; ++i) {
//        p = new NodeRC<Mutable>(i, p, true);
//    }
//    p = new NodeRC<Mutable>(num - 1, p, Mutable);
//    auto hptr = make_hazard_ptr();
//    std::atomic<NodeRC<Mutable> *> p1 = p;
//    hptr.protect(p1);
//    if (!Mutable) {
//        for (auto q = p->next(); q; q = q->next()) {
//            retire(q);
//        }
//    }
//    int v = num;
//    for (auto q = p; q; q = q->next()) {
//        ASSERT_GT(v, 0);
//        --v;
//        ASSERT_EQ(q->value(), v);
//    }
//
//    reclaim();
//    ASSERT_EQ(c_.ctors(), num);
//    ASSERT_EQ(c_.dtors(), 0);
//
//    if (Mutable) {
//        hazptr_root <NodeRC<Mutable>> root(p);
//    } else {
//        p->retire();
//    }
//    reclaim();
//    ASSERT_EQ(c_.dtors(), 0);
//
//    hptr.reset();
//    reclaim();
//    ASSERT_EQ(c_.dtors(), num);
//}

//template<bool Mutable>
//void mt_linked_test() {
//    c_.clear();
//
//    std::atomic<bool> ready(false);
//    std::atomic<bool> done(false);
//    std::atomic<int> setHazptrs(0);
//    hazptr_root <NodeRC<Mutable>> head;
//
//    int num = FLAGS_num_ops;
//    int nthr = FLAGS_num_threads;
//    ASSERT_GT(FLAGS_num_threads, 0);
//    std::vector<std::thread> thr(nthr);
//    for (int i = 0; i < nthr; ++i) {
//        thr[i] = std::thread([&] {
//            while (!ready.load()) {
//                /* spin */
//            }
//            auto hptr = make_hazard_ptr();
//            auto p = hptr.protect(head());
//            ++setHazptrs;
//            /* Concurrent with removal */
//            int v = num;
//            for (auto q = p; q; q = q->next()) {
//                ASSERT_GT(v, 0);
//                --v;
//                ASSERT_EQ(q->value(), v);
//            }
//            ASSERT_EQ(v, 0);
//            while (!done.load()) {
//                /* spin */
//            }
//        });
//    }
//
//    NodeRC<Mutable> *p = nullptr;
//    for (int i = 0; i < num - 1; ++i) {
//        p = new NodeRC<Mutable>(i, p, true);
//    }
//    p = new NodeRC<Mutable>(num - 1, p, Mutable);
//    ASSERT_EQ(c_.ctors(), num);
//    head().store(p);
//    ready.store(true);
//    while (setHazptrs.load() < nthr) {
//        /* spin */
//    }
//
//    /* this is concurrent with traversal by readers */
//    head().store(nullptr);
//    if (Mutable) {
//        p->unlink();
//    } else {
//        for (auto q = p; q; q = q->next()) {
//            q->retire();
//        }
//    }
//    ASSERT_EQ(c_.dtors(), 0);
//    done.store(true);
//
//    for (auto &t: thr) {
//        DSched::join(t);
//    }
//
//    reclaim();
//    ASSERT_EQ(c_.dtors(), num);
//}

//void auto_retire_test() {
//    c_.clear();
//    auto d = new NodeAuto;
//    d->acquire_link_safe();
//    auto c = new NodeAuto(d);
//    d->acquire_link_safe();
//    auto b = new NodeAuto(d);
//    c->acquire_link_safe();
//    b->acquire_link_safe();
//    auto a = new NodeAuto(b, c);
//    hazptr_holder h = make_hazard_pointer();
//    {
//        hazptr_root <NodeAuto> root;
//        a->acquire_link_safe();
//        root().store(a);
//        ASSERT_EQ(c_.ctors(), 4);
//        /* So far the links and link counts are:
//               root-->a  a-->b  a-->c  b-->d  c-->d
//               a(1,0) b(1,0) c(1,0) d(2,0)
//        */
//        h.reset_protection(c); /* h protects c */
//        reclaim();
//        ASSERT_EQ(c_.dtors(), 0);
//        /* Nothing is retired or reclaimed yet */
//    }
//    /* root dtor calls a->unlink, which calls a->release_link, which
//       changes a's link counts from (1,0) to (0,0), which triggers calls
//       to c->downgrade_link, b->downgrade_link, and a->retire.
//
//       c->downgrade_link changes c's link counts from (1,0) to (0,1),
//       which triggers calls to d->downgrade_link and c->retire.
//
//       d->downgrade_link changes d's link counts from (2,0) to (1,1).
//
//       b->downgrade_link changes b's link counts from (1,0) to (0,1),
//       which triggers calls to d->downgrade_link and b->retire.
//
//       d->downgrade_link changes d's link counts from (1,1) to (0,2),
//       which triggers a call to d->retire.
//
//       So far (assuming retire-s did not trigger bulk_reclaim):
//             a-->b  a-->c  b-->d  c-->d
//             a(0,0) b(0,1) c(0,1) d(0,2)
//             Retired: a b c d
//             Protected: c
//    */
//    reclaim();
//    /* hazptr_cleanup calls bulk_reclaim which finds a, b, and d
//       unprotected, which triggers calls to a->release_ref,
//       b->release_ref, and d->release_ref (not necessarily in that
//       order).
//
//       a->release_ref finds a's link counts to be (0,0), which triggers
//       calls to c->release_ref, b->release_ref and delete a.
//
//       The call to c->release_ref changes its link counts from (0,1) to
//       (0,0).
//
//       The first call to b->release_ref changes b's link counts to
//       (0,0). The second call finds the link counts to be (0,0), which
//       triggers a call to d->release_ref and delete b.
//
//       The first call to d->release_ref changes its link counts to
//       (0,1), and the second call changes them to (0,0);
//
//       So far:
//             c-->d
//             a(deleted) b(deleted) c(0,0) d(0,0)
//             Retired and protected: c
//             bulk_reclamed-ed (i.e, found not protected): d
//    */
//    ASSERT_EQ(c_.dtors(), 2);
//    h.reset_protection(); /* c is now no longer protected */
//    reclaim();
//    /* hazptr_cleanup calls bulk_reclaim which finds c unprotected,
//       which triggers a call to c->release_ref.
//
//       c->release_ref finds c's link counts to be (0,0), which
//       triggers calls to d->release_ref and delete c.
//
//       d->release_ref finds d's link counts to be (0,0), which triggers
//       a call to delete d.
//
//       Finally:
//             a(deleted) b(deleted) c(deleted) d(deleted)
//    */
//    ASSERT_EQ(c_.dtors(), 4);
//}

void free_function_retire_test() {
    auto foo = new int;
    retire(foo);
    auto foo2 = new int;
    retire(foo2, [](int *obj) { delete obj; });

//    bool retired = false;
//    {
//        hazptr_domain myDomain0;
//        struct delret {
//            bool *retired_;
//
//            explicit delret(bool *retire) : retired_(retire) {}
//
//            ~delret() { *retired_ = true; }
//        };
//        auto foo3 = new delret(&retired);
//        myDomain0.retire(foo3);
//    }
//    ASSERT_TRUE(retired);
}

void cleanup_test() {
    int threadOps = 1007;
    int mainOps = 19;
    c_.clear();
    std::atomic<int> threadsDone{0};
    std::atomic<bool> mainDone{false};
    std::vector<std::thread> threads(FLAGS_num_threads);
    for (int tid = 0; tid < FLAGS_num_threads; ++tid) {
        threads[tid] = std::thread([&, tid]() {
            for (int j = tid; j < threadOps; j += FLAGS_num_threads) {
                auto p = new Node;
                retire(p);
            }
            threadsDone.fetch_add(1);
            while (!mainDone.load()) {
                /* spin */;
            }
        });
    }
    { // include the main thread in the test
        for (int i = 0; i < mainOps; ++i) {
            auto p = new Node;
            retire(p);
        }
    }
    while (threadsDone.load() < FLAGS_num_threads) {
        /* spin */;
    }
    ASSERT_EQ(c_.ctors(), threadOps + mainOps);
    reclaim();
    ASSERT_EQ(c_.dtors(), threadOps + mainOps);
    mainDone.store(true);
    for (auto &t: threads) {
        t.join();
    }
    { // Cleanup after using array
        c_.clear();
        { hazptr_array<2> h = make_hazard_ptr<2>(); }
        {
            hazptr_array<2> h = make_hazard_ptr<2>();
            auto p0 = new Node;
            auto p1 = new Node;
            std::atomic<Node *> p0a = p0;
            std::atomic<Node *> p1a = p1;
            h[0].protect(p0a);
            h[1].protect(p1a);
            retire(p0);
            retire(p1);
        }
        ASSERT_EQ(c_.ctors(), 2);
        reclaim();
        ASSERT_EQ(c_.dtors(), 2);
    }
    { // Cleanup after using local
        c_.clear();
        {
            auto h = make_hazard_ptr<2>();
            auto p0 = new Node;
            auto p1 = new Node;
            std::atomic<Node *> p0a = p0;
            std::atomic<Node *> p1a = p1;
            h[0].protect(p0a);
            h[1].protect(p1a);
            retire(p0);
            retire(p1);
        }
        ASSERT_EQ(c_.ctors(), 2);
        reclaim();
        ASSERT_EQ(c_.dtors(), 2);
    }
}

//template<template<typename> class std::atomic = std::atomic>
//void priv_dtor_test() {
//    c_.clear();
//    using NodeT = NodeRC<true>;
//    auto y = new NodeT;
//    y->acquire_link_safe();
//    struct Foo : hazptr_obj_base<Foo> {
//        hazptr_root <NodeT> r_;
//    };
//    auto x = new Foo;
//    x->r_().store(y);
//    /* Thread retires x. Dtor of TLS priv list pushes x to domain, which
//       triggers bulk reclaim due to timed cleanup (when the test is run
//       by itself). Reclamation of x unlinks and retires y. y should
//       not be pushed into the thread's priv list. It should be pushed to
//       domain instead. */
//    auto thr = DSched::thread([&]() { x->retire(); });
//    DSched::join(thr);
//    ASSERT_EQ(c_.ctors(), 1);
//    reclaim();
//    ASSERT_EQ(c_.dtors(), 1);
//}

//void fork_test() {
//    folly::enable_hazptr_thread_pool_executor();
//    auto trigger_reclamation = [] {
//        hazptr_obj_cohort b;
//        for (int i = 0; i < 2001; ++i) {
//            auto p = new Node;
//            p->set_cohort_no_tag(&b);
//            p->retire();
//        }
//    };
//    std::thread t1(trigger_reclamation);
//    t1.join();
//    folly::SingletonVault::singleton()->destroyInstances();
//    auto pid = fork();
//    folly::SingletonVault::singleton()->reenableInstances();
//    if (pid > 0) {
//        // parent
//        int status = -1;
//        auto pid2 = waitpid(pid, &status, 0);
//        EXPECT_EQ(status, 0);
//        EXPECT_EQ(pid, pid2);
//        trigger_reclamation();
//    } else if (pid == 0) {
//        // child
//        c_.clear();
//        std::thread t2(trigger_reclamation);
//        t2.join();
//        exit(0); // Do not print gtest results
//    } else {
//        std::cout << "Failed to fork()";
//    }
//}

//template<template<typename> class std::atomic = std::atomic>
//void lifo_test() {
//    for (int i = 0; i < FLAGS_num_reps; ++i) {
//        std::atomic<int> sum{0};
//        HazptrLockFreeLIFO<int> s;
//        std::vector<std::thread> threads(FLAGS_num_threads);
//        for (int tid = 0; tid < FLAGS_num_threads; ++tid) {
//            threads[tid] = DSched::thread([&, tid]() {
//                int local = 0;
//                for (int j = tid; j < FLAGS_num_ops; j += FLAGS_num_threads) {
//                    s.push(j);
//                    int v;
//                    ASSERT_TRUE(s.pop(v));
//                    local += v;
//                }
//                sum.fetch_add(local);
//            });
//        }
//        for (auto &t: threads) {
//            DSched::join(t);
//        }
//        reclaim();
//        int expected = FLAGS_num_ops * (FLAGS_num_ops - 1) / 2;
//        ASSERT_EQ(sum.load(), expected);
//    }
//}
//
//template<template<typename> class std::atomic = std::atomic>
//void swmr_test() {
//    using T = uint64_t;
//    for (int i = 0; i < FLAGS_num_reps; ++i) {
//        HazptrSWMRSet <T> s;
//        std::vector<std::thread> threads(FLAGS_num_threads);
//        for (int tid = 0; tid < FLAGS_num_threads; ++tid) {
//            threads[tid] = DSched::thread([&s, tid]() {
//                for (int j = tid; j < FLAGS_num_ops; j += FLAGS_num_threads) {
//                    s.contains(j);
//                }
//            });
//        }
//        for (int j = 0; j < 10; ++j) {
//            s.add(j);
//        }
//        for (int j = 0; j < 10; ++j) {
//            s.remove(j);
//        }
//        for (auto &t: threads) {
//            DSched::join(t);
//        }
//        reclaim();
//    }
//}
//
//template<template<typename> class std::atomic = std::atomic>
//void wide_cas_test() {
//    HazptrWideCAS <std::string> s;
//    std::string u = "";
//    std::string v = "11112222";
//    auto ret = s.cas(u, v);
//    ASSERT_TRUE(ret);
//    u = "";
//    v = "11112222";
//    ret = s.cas(u, v);
//    ASSERT_FALSE(ret);
//    u = "11112222";
//    v = "22223333";
//    ret = s.cas(u, v);
//    ASSERT_TRUE(ret);
//    u = "22223333";
//    v = "333344445555";
//    ret = s.cas(u, v);
//    ASSERT_TRUE(ret);
//    reclaim();
//}

class HazptrPreInitTest : public testing::Test {
private:
    // pre-init to avoid deadlock when using DeterministicAtomic
};

// Tests

//TEST(HazptrTest, basicObjects) {
//    basic_objects_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_basic_objects) {
//    basic_objects_test();
//}

TEST(HazptrTest, copyAndMove) {
    copy_and_move_test();
}

TEST_F(HazptrPreInitTest, dsched_copy_and_move) {
    copy_and_move_test();
}

TEST(HazptrTest, basicHolders) {
    basic_holders_test();
}

TEST_F(HazptrPreInitTest, dsched_basic_holders) {
    basic_holders_test();
}

TEST(HazptrTest, basicProtection) {
    basic_protection_test();
}

TEST_F(HazptrPreInitTest, dsched_basic_protection) {
    basic_protection_test();
}

TEST(HazptrTest, virtual) {
    virtual_test();
}

TEST_F(HazptrPreInitTest, dsched_virtual) {
    virtual_test();
}

TEST(HazptrTest, destruction) {
//    {
//        hazptr_domain<> myDomain0;
//        destruction_test(myDomain0);
//    }
    destruction_test(reclaimer::instance());
}

TEST_F(HazptrPreInitTest, dsched_destruction) {
//    
//    {
//        hazptr_domain  myDomain0;
//        destruction_test(myDomain0);
//    }
    destruction_test(reclaimer::instance());
}

TEST(HazptrTest, destructionProtected) {
//    {
//        hazptr_domain<> myDomain0;
//        destruction_protected_test(myDomain0);
//    }
    destruction_protected_test(reclaimer::instance());
}

TEST_F(HazptrPreInitTest, dsched_destruction_protected) {
//    {
//        hazptr_domain  myDomain0;
//        destruction_protected_test(myDomain0);
//    }
    destruction_protected_test(reclaimer::instance());
}

TEST(HazptrTest, move) {
    move_test();
}

TEST_F(HazptrPreInitTest, dsched_move) {
//    
    move_test();
}

TEST(HazptrTest, array) {
    array_test();
}

TEST_F(HazptrPreInitTest, dsched_array) {
//    
    array_test();
}

TEST(HazptrTest, arrayDtorFullTc) {
    array_dtor_full_tc_test();
}

TEST_F(HazptrPreInitTest, dsched_array_dtor_full_tc) {
//    
    array_dtor_full_tc_test();
}

TEST(HazptrTest, local) {
    local_test();
}

TEST_F(HazptrPreInitTest, dsched_local) {
//    
    local_test();
}

//TEST(HazptrTest, linkedMutable) {
//    linked_test<true>();
//}
//
//TEST_F(HazptrPreInitTest, dsched_linked_mutable) {
////
//    linked_test<true>();
//}
//
//TEST(HazptrTest, linkedImmutable) {
//    linked_test<false>();
//}
//
//TEST_F(HazptrPreInitTest, dsched_linked_immutable) {
////
//    linked_test<false>();
//}

//TEST(HazptrTest, mtLinkedMutable) {
//    mt_linked_test<true>();
//}
//
//TEST_F(HazptrPreInitTest, dsched_mt_linked_mutable) {
////
//    mt_linked_test<true>();
//}
//
//TEST(HazptrTest, mtLinkedImmutable) {
//    mt_linked_test<false>();
//}
//
//TEST_F(HazptrPreInitTest, dsched_mt_linked_immutable) {
////
//    mt_linked_test<false>();
//}

//TEST(HazptrTest, autoRetire) {
//    auto_retire_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_auto_retire) {
////
//    auto_retire_test();
//}

TEST(HazptrTest, freeFunctionRetire) {
    free_function_retire_test();
}

TEST_F(HazptrPreInitTest, dsched_free_function_retire) {
//    
    free_function_retire_test();
}

TEST(HazptrTest, cleanup) {
    cleanup_test();
}

TEST_F(HazptrPreInitTest, dsched_cleanup) {

    cleanup_test();
}

//TEST(HazptrTest, privDtor) {
//    priv_dtor_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_priv_dtor) {
//    priv_dtor_test();
//}

//TEST(HazptrTest, fork) {
//    fork_test();
//}

//TEST(HazptrTest, lifo) {
//    lifo_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_lifo) {
//
//    lifo_test();
//}
//
//TEST(HazptrTest, swmr) {
//    swmr_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_swmr) {
//
//    swmr_test();
//}
//
//TEST(HazptrTest, wideCas) {
//    wide_cas_test();
//}
//
//TEST_F(HazptrPreInitTest, dsched_wide_cas) {
//
//    wide_cas_test();
//}

TEST(HazptrTest, reclamationWithoutCallingCleanup) {
    c_.clear();
    int nthr = 5;
    int objs = 100;
    std::vector<std::thread> thr(nthr);
    for (int tid = 0; tid < nthr; ++tid) {
        thr[tid] = std::thread([&, tid] {
            for (int i = tid; i < objs; i += nthr) {
                auto p = new Node;
                retire(p);
            }
        });
    }
    for (auto &t: thr) {
        t.join();
    }
    while (c_.dtors() == 0)
        /* Wait for asynchronous reclamation. */;
    ASSERT_GT(c_.dtors(), 0);
}

TEST(HazptrTest, standardNames) {
//    struct Foo {
//    };
//    DCHECK_EQ(&hazard_pointer_default_domain<>(), &default_hazptr_domain<>());
//    hazard_pointer<> h = make_hazard_pointer();
//    hazard_pointer_clean_up<>();
}

// Benchmark drivers

struct Barrier {
    explicit Barrier(size_t count) : lock_(), cv_(), count_(count) {}

    void wait() {
        std::unique_lock<std::mutex> lockHeld(lock_);
        auto gen = gen_;
        if (++num_ == count_) {
            num_ = 0;
            gen_++;
            cv_.notify_all();
        } else {
            cv_.wait(lockHeld, [&]() { return gen != gen_; });
        }
    }

private:
    std::mutex lock_;
    std::condition_variable cv_;
    size_t num_{0};
    size_t count_;
    size_t gen_{0};
};


template<typename InitFunc, typename Func, typename EndFunc>
uint64_t run_once(
        int nthreads, const InitFunc &init, const Func &fn, const EndFunc &endFn) {
    Barrier b(nthreads + 1);
    init();
    std::vector<std::thread> threads(nthreads);
    for (int tid = 0; tid < nthreads; ++tid) {
        threads[tid] = std::thread([&, tid] {
            b.wait();
            b.wait();
            fn(tid);
        });
    }
    b.wait();
    // begin time measurement
    auto tbegin = std::chrono::steady_clock::now();
    b.wait();
    for (auto &t: threads) {
        t.join();
    }
    reclaim();
    // end time measurement
    auto tend = std::chrono::steady_clock::now();
    endFn();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tbegin)
            .count();
}

template<typename RepFunc>
uint64_t bench(std::string name, int ops, const RepFunc &repFn) {
    int reps = 10;
    uint64_t min = UINTMAX_MAX;
    uint64_t max = 0;
    uint64_t sum = 0;

    repFn(); // sometimes first run is outlier
    for (int r = 0; r < reps; ++r) {
        uint64_t dur = repFn();
        sum += dur;
        min = std::min(min, dur);
        max = std::max(max, dur);
    }

    const std::string unit = " ns";
    uint64_t avg = sum / reps;
    uint64_t res = min;
    std::cout << name;
    std::cout << "   " << std::setw(4) << max / ops << unit;
    std::cout << "   " << std::setw(4) << avg / ops << unit;
    std::cout << "   " << std::setw(4) << res / ops << unit;
    std::cout << std::endl;
    return res;
}

//
// Benchmarks
//
const int ops = 1000000;

inline uint64_t holder_bench(std::string name, int nthreads) {
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int tid) {
            for (int j = tid; j < 10 * ops; j += nthreads) {
                auto h = make_hazard_ptr();
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

template<size_t M>
inline uint64_t array_bench(std::string name, int nthreads) {
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int tid) {
            for (int j = tid; j < 10 * ops; j += nthreads) {
                hazptr_array<M> a = make_hazard_ptr<M>();
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

template<size_t M>
inline uint64_t local_bench(std::string name, int nthreads) {
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int tid) {
            for (int j = tid; j < 10 * ops; j += nthreads) {
                hazard_local<M> a;
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

inline uint64_t obj_bench(std::string name, int nthreads) {
    struct Foo {
    };
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int tid) {
            for (int j = tid; j < ops; j += nthreads) {
                auto p = new Foo;
                retire(p);
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

uint64_t list_hoh_bench(
        std::string name, int nthreads, int size, bool provided = false) {
    auto repFn = [&] {
        List<Node> l(size);
        auto init = [&] {};
        auto fn = [&](int tid) {
            if (provided) {
                hazard_local<2> hptr;
                for (int j = tid; j < ops; j += nthreads) {
                    l.hand_over_hand(size, &hptr[0], &hptr[1]);
                }
            } else {
                for (int j = tid; j < ops; j += nthreads) {
                    l.hand_over_hand(size);
                }
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

uint64_t list_protect_all_bench(
        std::string name, int nthreads, int size, bool provided = false) {
    auto repFn = [&] {
        List<NodeRC<false>> l(size);
        auto init = [] {};
        auto fn = [&](int tid) {
            if (provided) {
                alp_utils::hazp::reserve_hazp(1);

                auto hptr = alp_utils::hazp::make_hazard_ptr();
                for (int j = tid; j < ops; j += nthreads) {
                    l.protect_all(size, hptr);
                }
            } else {
                for (int j = tid; j < ops; j += nthreads) {
                    l.protect_all(size);
                }
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

uint64_t cleanup_bench(std::string name, int nthreads) {
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int) {
            auto hptr = make_hazard_ptr();
            for (int i = 0; i < ops / 1000; i++) {
                reclaim();
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

uint64_t tc_miss_bench(std::string name, int nthreads) {
    evict_hazard_ptr();
    delete_hazard_ptr();
    // Thread cache capacity
    constexpr int C = 8;
    // Number of unavailable hazard pointers that will be at the head of
    // the main list of hazard pointers before reaching available ones.
    constexpr int N = 10000;
    // Max number of threads
    constexpr int P = 100;
    hazard_ptr aa[N + 2 * C * P];
    // The following creates N+2*C*P hazard pointers
    for (int i = 0; i < N + 2 * C * P; ++i) {
        aa[i] = make_hazard_ptr();
    }
    // Make the last 2*C*P in the domain's hazard pointer list available
    for (int i = 0; i < 2 * C * P; ++i) {
        aa[i] = {};
    }
    evict_hazard_ptr();
    // The domain now has N unavailable hazard pointers at the head of
    // the list following by C*P available ones.
    auto repFn = [&] {
        auto init = [] {};
        auto fn = [&](int tid) {
            for (int j = tid; j < ops; j += nthreads) {
                // By using twice the TC capacity, each iteration does one
                // filling and one eviction of the TC.
                auto a1 = make_hazard_ptr<C>();
                hazptr_array<C> a2 = make_hazard_ptr<C>();
            }
        };
        auto endFn = [] {};
        return run_once(nthreads, init, fn, endFn);
    };
    return bench(name, ops, repFn);
}

const int nthr[] = {1, 10};
const int sizes[] = {10, 20};

void benches() {
    for (int i: nthr) {
        std::cout << "================================ " << std::setw(2) << i
                  << " threads " << "================================" << std::endl;
        std::cout << "10x construct/destruct hazptr_holder          ";
        holder_bench("", i);
        std::cout << "10x construct/destruct hazptr_array<1>        ";
        array_bench<1>("", i);
        std::cout << "10x construct/destruct hazptr_array<2>        ";
        array_bench<2>("", i);
        std::cout << "10x construct/destruct hazptr_array<3>        ";
        array_bench<3>("", i);
        std::cout << "10x construct/destruct hazptr_local<1>        ";
        local_bench<1>("", i);
        std::cout << "10x construct/destruct hazptr_local<2>        ";
        local_bench<2>("", i);
        std::cout << "10x construct/destruct hazptr_local<3>        ";
        local_bench<3>("", i);
        std::cout << "10x construct/destruct hazptr_array<9>        ";
        array_bench<9>("", i);
//        std::cout << "TC hit + miss & overflow                      ";
//        tc_miss_bench("", i);
        std::cout << "allocate/retire/reclaim object                ";
        obj_bench("", i);
        for (int j: sizes) {
            std::cout << j << "-item list hand-over-hand - own hazptrs     ";
            list_hoh_bench("", i, j, true);
            std::cout << j << "-item list hand-over-hand                   ";
            list_hoh_bench("", i, j);
            std::cout << j << "-item list protect all - own hazptr         ";
            list_protect_all_bench("", i, j, true);
            std::cout << j << "-item list protect all                      ";
            list_protect_all_bench("", i, j);
        }
        std::cout << "1/1000 hazptr_cleanup                         ";
        cleanup_bench("", i);
    }
}

TEST(HazptrTest, bench) {
    benches();
}
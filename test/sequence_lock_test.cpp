#include <gtest/gtest.h>
#include <cpp_utils/concurrency/sequence_lock.h>

class SeqLockTest : public testing::Test {

};

TEST_F(SeqLockTest, SingleThreadBasic) {
    alp_utils::seq_lock<int> sl(42);

    auto ptr = sl.load_shared();
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 42);

    sl.lock();
    sl.store(100);
    sl.unlock();

    ptr = sl.load_shared();
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 100);
}

TEST_F(SeqLockTest, MultiThreadConsistency) {
    alp_utils::seq_lock<int> sl(0);
    constexpr int kUpdates = 1000;
    std::atomic<int> read_count{0};
    std::atomic<bool> writer_done{false};

    std::thread writer([&] {
        for (int i = 1; i <= kUpdates; ++i) {
            sl.lock();
            sl.store(i);
            sl.unlock();
        }
        writer_done = true;
    });

    std::thread reader([&] {
        while (!writer_done) {
            auto ptr = sl.load_shared();
            if (ptr) {
                int val = *ptr;
                assert(val >= 0 && val <= kUpdates);
                ++read_count;
            }
        }
    });

    writer.join();
    reader.join();

}

TEST_F(SeqLockTest, MultiThreadRaceCondition) {
    alp_utils::seq_lock<int> sl(0);
    std::atomic<bool> running{true};

    std::thread writer([&] {
        while (running) {
            sl.lock();
            sl.store(1);
            sl.unlock();
        }
    });

    std::thread reader([&] {
        while (running) {
            auto ptr = sl.load_shared();
            if (ptr) {
                int val = *ptr;
                assert(val == 1);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;
    writer.join();
    reader.join();
}

void test_sequence_parity() {
    alp_utils::seq_lock<std::string> sl("init");
    std::atomic<bool> running{true};

    std::thread writer([&] {
        int count = 0;
        while (running) {
            sl.lock();
            sl.store("update" + std::to_string(count));
            sl.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    running = false;
    writer.join();
}

TEST_F(SeqLockTest, SequenceParity) {
    test_sequence_parity();
}

TEST_F(SeqLockTest, RetryLoad) {
    alp_utils::seq_lock<int> sl(0);
    std::atomic<bool> update_trigger{false};

    sl.lock();

    std::thread updater([&] {
        while (!update_trigger) {}
        sl.lock();
        sl.store(42);
        sl.unlock();
    });

    auto ptr = sl.load_shared(5);
    ASSERT_FALSE(ptr);

    sl.unlock();
    update_trigger = true;
    updater.join();

    ptr = sl.load_shared(1);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 42);
}
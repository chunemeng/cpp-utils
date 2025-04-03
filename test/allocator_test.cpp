#include <cpp_utils/allocator/allocator_concepts.h>
#include <cpp_utils/allocator/arena.h>
#include <cpp_utils/concurrency/seq_lock.h>
#include <cpp_utils/container/radix_tree.h>
#include <cpp_utils/concurrency/hazard_ptr.h>
#include <gtest/gtest.h>

TEST(ArenaTest, EmptyArena) {
}


class ArenaTest : public testing::Test {};

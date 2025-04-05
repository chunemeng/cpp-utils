#include <gtest/gtest.h>
#include <cpp_utils/container/nd_heap.h>
#include <vector>
#include <random>
#include <iomanip>
#include <queue>

class NDHeapTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

TEST_F(NDHeapTest, BasicOperations) {
    alp_utils::nd_heap<int> heap;

    heap.push(10);
    heap.push(20);
    heap.push(5);

    EXPECT_EQ(heap.top(), 20);
    EXPECT_EQ(heap.size(), 3);

    heap.pop();
    EXPECT_EQ(heap.top(), 10);
    EXPECT_EQ(heap.size(), 2);

    heap.pop();
    EXPECT_EQ(heap.top(), 5);
    EXPECT_EQ(heap.size(), 1);

    heap.pop();
    EXPECT_TRUE(heap.empty());
}

TEST_F(NDHeapTest, CustomComparator) {
    auto cmp = [](const int &a, const int &b) { return a > b; };
    alp_utils::nd_heap<int, 4, decltype(cmp)> heap(cmp);

    heap.push(10);
    heap.push(20);
    heap.push(5);

    EXPECT_EQ(heap.top(), 5);
    EXPECT_EQ(heap.size(), 3);

    heap.pop();
    EXPECT_EQ(heap.top(), 10);
    EXPECT_EQ(heap.size(), 2);

    heap.pop();
    EXPECT_EQ(heap.top(), 20);
    EXPECT_EQ(heap.size(), 1);
}

TEST_F(NDHeapTest, RandomTest) {
    alp_utils::nd_heap<int> heap;
    std::vector<int> values;
    values.reserve(10000);

    for (int i = 0; i < 10000; ++i) {
        auto v = rand() % 10000;
        values.push_back(v);
        heap.push(v);
    }

    std::sort(values.begin(), values.end(), std::greater<int>());

    for (const auto &v: values) {
        EXPECT_EQ(heap.top(), v);
        heap.pop();
    }
    EXPECT_TRUE(heap.empty());
}

static constexpr int data_size = 1000000;

template<typename Heap>
double put_through_put(Heap &heap) {
    std::vector<uint64_t> data(data_size);
    std::mt19937 rng(std::random_device{}());
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = rng();
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (const auto &v: data) {
        heap.push(v);
    }
    auto end = std::chrono::high_resolution_clock::now();
    return data_size * 1.0 / std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e9;
}

template<typename T>
double pop_through_put(T &heap) {

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < data_size; ++i) {
        heap.pop();
    }
    auto end = std::chrono::high_resolution_clock::now();
    return data_size * 1.0 / std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() * 1e9;
}

consteval auto generate_array() {
    std::array<int, 10> arr{};
    int value = 2;
    for (auto &num: arr) {
        num = value;
        value *= 2;
    }
    return arr;
}

constexpr std::array<int, 10> nums = generate_array();

template<size_t... I>
void benchmark_impl(std::index_sequence<I...>) {
    (([]<size_t K>() {
        alp_utils::nd_heap<uint64_t, nums[K]> heap;
        std::cout << std::left << std::setw(12) << "Heap size:"
                  << std::fixed << nums[K] << "\n";

        auto print_submetric = [](const std::string &label, double value) {
            std::cout << "    " << std::left << std::setw(14) << label + ":"
                      << std::scientific << std::setprecision(2) << value
                      << " ops/s\n";
        };

        print_submetric("Push Throughput", put_through_put(heap));
        print_submetric("Pop Throughput", pop_through_put(heap));
        std::cout << "-------------------------------------\n";
    }.template operator()<I>()), ...);
}

void run_benchmarks() {
    std::priority_queue<int> heap;
    std::cout << std::left << std::setw(12) << "std Heap:\n";

    auto print_submetric = [](const std::string &label, double value) {
        std::cout << "    " << std::left << std::setw(14) << label + ":"
                  << std::scientific << std::setprecision(2) << value
                  << " ops/s\n";
    };

    print_submetric("Push Throughput", put_through_put(heap));
    print_submetric("Pop Throughput", pop_through_put(heap));
    std::cout << "-------------------------------------\n";

    benchmark_impl(std::make_index_sequence<10>{});
}


TEST_F(NDHeapTest, ThroughPut) {
    run_benchmarks();
}

TEST_F(NDHeapTest, StressTest) {
    alp_utils::nd_heap<uint64_t> heap;
    std::cout << put_through_put(heap) << " " << pop_through_put(heap) << std::endl;
}
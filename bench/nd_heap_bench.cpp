#include <queue>
#include <vector>
#include <random>
#include <benchmark/benchmark.h>
#include <cpp_utils/container/nd_heap.h>

// 生成随机测试数据
static std::vector<int> GenerateTestData(size_t size) {
    std::vector<int> data(size);
    std::mt19937 gen(42);
    std::uniform_int_distribution<> dis(1, 1000000);
    for (auto &num: data) num = dis(gen);
    return data;
}

// 基准测试基类
template<typename Container>
class HeapBenchmark : public benchmark::Fixture {
protected:
    std::vector<int> test_data;
    Container container;

public:
    void SetUp(const ::benchmark::State &state) override {
        test_data = GenerateTestData(state.range(0));
    }

    void TearDown(const ::benchmark::State &) override {}
};

// 标准优先队列测试
using StdQueue = HeapBenchmark<std::priority_queue<int>>;

BENCHMARK_DEFINE_F(StdQueue, PushPop)(benchmark::State &state) {
    for (auto _: state) {
        std::priority_queue<int> q;
        for (auto num: test_data) q.push(num);
        while (!q.empty()) q.pop();
    }
}

BENCHMARK_REGISTER_F(StdQueue, PushPop)->Arg(1 << 15)->Arg(1 << 18);

using KaryHeap4 = HeapBenchmark<alp_utils::nd_heap<int, 4>>;

BENCHMARK_DEFINE_F(KaryHeap4, PushPop)(benchmark::State &state) {
    for (auto _: state) {
        alp_utils::nd_heap<int, 4> q;
        for (auto num: test_data) q.push(num);
        while (!q.empty()) q.pop();
    }
}

BENCHMARK_REGISTER_F(KaryHeap4, PushPop)->Arg(1 << 15)->Arg(1 << 18);

BENCHMARK_MAIN();
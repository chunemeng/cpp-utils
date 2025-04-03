#include <benchmark/benchmark.h>
#include <ctime>
#include <random>

// virtual function
struct BaseDynamic {
	virtual int operation(int x) const noexcept { return x; }
	virtual ~BaseDynamic() = default;
};

struct DerivedDynamicA : public BaseDynamic {
	int operation(int x) const noexcept override { return x * (x & 0xff); }
};

struct DerivedDynamicB : public BaseDynamic {
	int operation(int x) const noexcept override { return x * x * (x & 0xee); }
};

// CRTP
template <typename Derived>
struct BaseStatic {
	auto operation(int x) const noexcept -> int { return static_cast<const Derived*>(this)->impl_operation(x); }
};

struct DerivedStaticA : BaseStatic<DerivedStaticA> {
	int impl_operation(int x) const noexcept { return x * (x & 0xff); }
};

struct DerivedStaticB : BaseStatic<DerivedStaticB> {
	int impl_operation(int x) const noexcept { return x * x * (x & 0xee); }
};

struct Base {
	int operation(int x) const noexcept { return x; }
};

struct DerivedA : public Base {
	int operation(int x) const noexcept { return x * (x & 0xff); }
};

struct DerivedB : public Base {
	int operation(int x) const noexcept { return x * x * (x & 0xee); }
};

struct Pack {
	template <typename T, typename U>
	Pack(T* obj, U* obj2) : basea(obj), baseb(obj2) {
		funca = +[](Base* obj, int x) { return static_cast<T*>(obj)->operation(x); };
		funcb = +[](Base* obj, int x) { return static_cast<U*>(obj)->operation(x); };
	}

	int operationa(int x) const noexcept { return funca(basea, x); }
	int operationb(int x) const noexcept { return funcb(baseb, x); }

	int (*funca)(Base* obj, int x) = nullptr;
	int (*funcb)(Base* obj, int x) = nullptr;
	Base* basea;
	Base* baseb;
};

struct Base_Dynamic {
	virtual int operation(int x) const noexcept { return x; }
	virtual ~Base_Dynamic() = default;
};

template <typename Derived>
struct Base_Static : public Base_Dynamic {
	int operation(int x) const noexcept { return static_cast<const Derived*>(this)->impl_operation(x); }
};

struct Derived_A : public Base_Static<Derived_A> {
	int impl_operation(int x) const noexcept { return x * (x & 0xff); }
};

struct Derived_B : public Base_Static<Derived_B> {
	int impl_operation(int x) const noexcept { return x * (x & 0xff); }
};

static constexpr int kIterations = 1'000'000;

static void BM_DynamicPolymorphism(benchmark::State& state) {
	DerivedDynamicA objA;
	DerivedDynamicB objB;
	BaseDynamic* base = &objA;
	uint32_t result	  = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			bool use_a = i & 1;
			base	   = (use_a) ? static_cast<BaseDynamic*>(&objA) : &objB;
			benchmark::DoNotOptimize(result += base->operation(i));
		}
	}
}
BENCHMARK(BM_DynamicPolymorphism);

static void BM_DynamicPolymorphism_Clean(benchmark::State& state) {
	DerivedDynamicA objA;
	BaseDynamic* base = &objA;
	uint32_t result	  = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			benchmark::DoNotOptimize(result += base->operation(i));
		}
	}
}
BENCHMARK(BM_DynamicPolymorphism_Clean);

static void BM_StaticPolymorphism(benchmark::State& state) {
	DerivedStaticA objA;
	DerivedStaticB objB;
	uint32_t result = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			bool use_a = i & 1;
			benchmark::DoNotOptimize(result += use_a ? objA.operation(i) : objB.operation(i));
		}
	}
}
BENCHMARK(BM_StaticPolymorphism);

static void BM_StaticPolymorphism_Clean(benchmark::State& state) {
	DerivedStaticA objA;
	uint32_t result = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			benchmark::DoNotOptimize(result += objA.operation(i));
		}
	}
}
BENCHMARK(BM_StaticPolymorphism_Clean);


static void BM_DynamicCrtpPolymorphism(benchmark::State& state) {
	Derived_A objA;
	Derived_B objB;
	Base_Dynamic* base = &objA;
	uint32_t result	   = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			bool use_a = i & 1;
			base	   = (use_a) ? static_cast<Base_Dynamic*>(&objA) : &objB;
			benchmark::DoNotOptimize(result += base->operation(i));
		}
	}
}

BENCHMARK(BM_DynamicCrtpPolymorphism);


static void BM_DynamicCrtpPolymorphism_Clean(benchmark::State& state) {
	Derived_A objA;
	Base_Dynamic* base = &objA;
	uint32_t result	   = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			benchmark::DoNotOptimize(result += base->operation(i));
		}
	}
}
BENCHMARK(BM_DynamicCrtpPolymorphism_Clean);

static void BM_TypeErasurePolymorphism(benchmark::State& state) {
	DerivedA obj1;
	DerivedB obj2;
	Pack packa(&obj1, &obj2);
	uint32_t result = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			bool use_a = i & 1;
			benchmark::DoNotOptimize(result += use_a ? packa.operationa(i) : packa.operationb(i));
		}
	}
}
BENCHMARK(BM_TypeErasurePolymorphism);

static void BM_TypeErasurePolymorphism_Clean(benchmark::State& state) {
	DerivedA obj1;
	DerivedB obj2;
	Pack packa(&obj1, &obj2);
	uint32_t result = 0;

	for (auto _ : state) {
		for (int i = 0; i < kIterations; ++i) {
			benchmark::DoNotOptimize(result += packa.operationa(i));
		}
	}
}
BENCHMARK(BM_TypeErasurePolymorphism_Clean);

BENCHMARK_MAIN();

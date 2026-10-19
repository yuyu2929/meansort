# MeanSort

**An adaptive-partition sorting library with a statistical pivot, built for large numeric data.**
Header-only C++17, zero dependencies, built-in multithreading.

[中文文档](README.md) | **English**

---

## Why MeanSort

| Feature | MeanSort | std::sort |
|---|---|---|
| Built-in parallel sorting | **Yes** (`parallel_sort`) | No (needs `std::execution::par` + TBB backend) |
| Heavy-duplicate data | **Fastest** (3-way partition, equal range skipped wholesale) | Average |
| Sorted / reversed input | **O(n) detect / O(n) reverse** | Fast |
| Worst-case guarantee | O(n log n) (introsort fallback) | O(n log n) |
| Dependencies | **None, single header** | Standard library |
| C++ standard | C++17 | — |

Measured on a **4 vCPU GitHub Actions runner** (`ubuntu-latest`, g++ 13, `-O3`,
`long long`, N = 2,000,000, milliseconds). Re-run on every push; see
[docs/benchmark.md](docs/benchmark.md) for the full old-vs-new A/B, the method,
and the known limitations.

| Distribution | std::sort | MeanSort serial | MeanSort parallel (4 cores) |
|---|---|---|---|
| Uniform random | 133.1 | 177.3 | **76.9** |
| Exponential (heavy tail) | 136.6 | 177.4 | **74.2** |
| Log-normal (heavy tail) | 134.5 | 174.7 | **87.1** |
| Few outliers | 80.8 | 73.0 | **45.9** |
| Many duplicates (100 distinct) | 60.5 | 45.3 | **32.8** |
| Already sorted | 20.5 | **1.3** | 4.1 |
| Reversed | 14.8 | **1.1** | 4.2 |
| Geometric (adversarial) | 42.7 | **22.6** | **11.3** |

Serial is still ~33% slower than `std::sort` on uniform random data — adaptive
partitioning bought only ~4%, and that is reported as-is. The parallel version is
where the win is (1.73x on 4 cores for uniform, 1.84x for heavy-tailed). Duplicates,
outliers, sorted/reversed and adversarial inputs are ahead even single-threaded.
Heavy-tailed distributions remain the known weak spot, caused by the mean-based
pivot under right-skewed data rather than by the partitioning strategy.

## Quick start

```cpp
#include "meansort.hpp"

std::vector<long long> v = /* your data */;

meansort::sort(v);                              // single thread
meansort::parallel_sort(v);                     // multi thread, auto hardware_concurrency
meansort::parallel_sort(v, /*max_threads=*/8);  // explicit thread count
```

```bash
g++ -O3 -std=c++17 -pthread your_code.cpp
```

## Scope

- `T` must be an **arithmetic type** — the pivot is computed from a statistic.
  For strings or custom structs use `std::sort` / pdqsort. Wrong types are
  rejected at compile time by `static_assert`.
- Not stable.
- **NaN is well defined**: treated as greater than every finite value and moved to
  the end; NaN samples never enter the pivot. (`std::sort` on NaN input is UB —
  this is a deliberate difference.)
- Serial uniform-random sorting is ~1.3x slower than `std::sort`. You win on
  duplicates, sorted inputs, outliers, and parallel large-N workloads.

## Build & test

```bash
cmake -B build -DMEANSORT_BUILD_TESTS=ON -DMEANSORT_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/meansort_bench 2000000
```

## Docs

- [docs/design.md](docs/design.md) — why the pivot is a trimmed mean, where the
  thresholds came from, which alternatives were rejected, and which claims were
  walked back after measurement.
- [docs/benchmark.md](docs/benchmark.md) — method, old-vs-new A/B, known gaps.

## License

Dual-licensed: **AGPL-3.0** for open-source use, or a **commercial license** for
closed-source products. Open an [issue](https://github.com/yuyu2929/meansort/issues) to get in touch.

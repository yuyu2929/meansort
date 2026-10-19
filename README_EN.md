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

Measured on a **2-core GitHub Actions runner** (`ubuntu-latest`, g++ 13, `-O3`,
`long long`, N = 2,000,000, milliseconds). Reproducible from CI logs:

| Distribution | std::sort | MeanSort serial | MeanSort parallel (2 cores) |
|---|---|---|---|
| Uniform random | 147.7 | 176.4 | **114.1** |
| Exponential (heavy tail) | 133.6 | 176.3 | 171.6 |
| Log-normal (heavy tail) | 149.0 | 177.2 | **119.5** |
| Few outliers | 80.5 | 72.1 | **55.5** |
| Many duplicates (100 distinct) | 60.5 | 46.6 | **34.9** |
| Already sorted | 20.5 | **1.3** | 3.3 |
| Reversed | 14.9 | **1.1** | 4.2 |
| Geometric (adversarial) | 41.3 | **23.0** | **19.0** |

Serial is ~19% slower than `std::sort` on uniform random data (it used to be 45%
before adaptive partitioning), and wins on duplicates, outliers, sorted/reversed
and adversarial inputs. On 2 cores the parallel version already overtakes
`std::sort`. Heavy-tailed distributions are the known weak spot — see the roadmap.

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
- Serial uniform-random sorting is ~1.2x slower than `std::sort`. You win on
  duplicates, sorted inputs, outliers, and parallel large-N workloads.

## Build & test

```bash
cmake -B build -DMEANSORT_BUILD_TESTS=ON -DMEANSORT_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/meansort_bench 2000000
```

## License

Dual-licensed: **AGPL-3.0** for open-source use, or a **commercial license** for
closed-source products. Open an [issue](https://github.com/yuyu2929/meansort/issues) to get in touch.

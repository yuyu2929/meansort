# MeanSort

**A parallel sorting library with a statistical pivot, built for enterprise-scale numeric data.**
Header-only C++17, zero dependencies. 中文文档见下方 / Chinese docs below.

---

## Why MeanSort

| Feature | MeanSort | std::sort |
|---|---|---|
| Built-in parallel sorting | **Yes, >2x speedup** | No |
| Heavy-duplicate data | **Fastest** (3-way partition) | Average |
| Sorted / reversed input | **O(n) detect / O(n) reverse** | Fast |
| Heavy-tail robustness | **Strong** (trimmed-mean pivot) | Average |
| Worst-case guarantee | O(n log n) (introsort fallback) | O(n log n) |
| Dependencies | **None, single header** | Standard library |

Measured (g++ -O3, Linux x64, `long long`, N = 2,000,000, time in ms):

| Distribution | std::sort | MeanSort serial | MeanSort parallel |
|---|---|---|---|
| Uniform random | **116.2** | 168.6 | **52.6** |
| Exponential (heavy tail) | **116.5** | 165.1 | **50.9** |
| Many duplicates (100 distinct) | 53.1 | **38.5** | **40.9** |
| Already sorted | 18.3 | **0.8** | 15.1 |
| Reversed | 13.6 | **1.4** | 16.1 |
| Geometric (adversarial) | 38.4 | **25.5** | **26.6** |

At N = 8,000,000 (uniform): std::sort 515.7 ms vs **MeanSort parallel 211.2 ms** (2.4x).

## Quick start

```cpp
#include "meansort.hpp"

std::vector<int> v = ...;
meansort::sort(v);            // single thread
meansort::parallel_sort(v);   // multi thread, auto hardware_concurrency
```

```bash
g++ -O3 -std=c++17 -pthread your_code.cpp
```

## Scope

- `T` must be an **arithmetic type** — the pivot is computed as a statistical mean.
  For strings or custom structs use `std::sort` / pdqsort.
- Not stable.
- Serial uniform-random sorting is ~1.4x slower than `std::sort` — the price of the
  3-way partition + statistical pivot. You win on duplicates, sorted inputs, and
  parallel large-N workloads.

## Build & test

```bash
cmake -B build -DMEANSORT_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

## License

Dual-licensed: **AGPL-3.0** for open-source use, or a **commercial license** for
closed-source products. Contact `<your-email@example.com>`.

---

## 中文说明(完整版见根目录 README)

企业级数值数据并行排序库:采样截尾均值枢轴 + 三路划分 + 内省兜底 + 固定线程池并行。
单头文件、零依赖、C++17。适用:大规模数值排序、重复元素多的数据、有序数据、需并行加速的场景。
不适用:`std::string`/自定义结构体、需要稳定排序的场景。

# MeanSort

[![CI](https://github.com/yuyu2929/meansort/actions/workflows/ci.yml/badge.svg)](https://github.com/yuyu2929/meansort/actions/workflows/ci.yml)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)

**统计枢轴自适应排序库** —— 面向大数据量的 C++17 header-only 数值排序库。
采样截尾均值选枢轴，按重复度自动切换两路/三路划分，内省兜底保证最坏情况，内置多线程并行。

**简体中文** | [English](README_EN.md)

---

## 为什么选 MeanSort

| 特性 | MeanSort | std::sort |
|---|---|---|
| 多线程并行 | **内置，无需改编译参数** | 无（需 `std::execution::par` + TBB 后端） |
| 重复元素极多的数据 | **最快**（三路划分，等值区整层排除） | 一般 |
| 已排序 / 逆序输入 | **O(n) 检测 / O(n) 原地翻转** | 快 |
| 最坏情况保证 | O(n log n)（introsort 兜底） | O(n log n) |
| 依赖 | **零依赖，单头文件** | 标准库 |
| C++ 标准 | C++17 | — |

## 实测数据

下表是 **GitHub Actions 的 4 vCPU runner** 上跑出来的（`ubuntu-latest`，g++ 13，`-O3`，`long long`，N = 2,000,000），
每次 push 都会重跑，数字可在 [Actions 日志](https://github.com/yuyu2929/meansort/actions) 里核对。
完整的新旧算法 A/B、测量方法与已知局限见 **[docs/benchmark.md](docs/benchmark.md)**。

| 数据分布 | std::sort | MeanSort 串行 | MeanSort 并行（4 核） |
|---|---|---|---|
| 均匀随机 | 133.1 ms | 177.3 ms | **76.9 ms** |
| 指数分布（重尾） | 136.6 ms | 177.4 ms | **74.2 ms** |
| 对数正态（重尾） | 134.5 ms | 174.7 ms | **87.1 ms** |
| 少量离群点 | 80.8 ms | 73.0 ms | **45.9 ms** |
| 大量重复（仅 100 种值） | 60.5 ms | 45.3 ms | **32.8 ms** |
| 已排序 | 20.5 ms | **1.3 ms** | 4.1 ms |
| 逆序 | 14.8 ms | **1.1 ms** | 4.2 ms |
| 等比数列（最坏构造） | 42.7 ms | **22.6 ms** | **11.3 ms** |

**怎么读这张表：**

- **单线程均匀随机仍比 `std::sort` 慢约 33%**（177 vs 133）。这是事实，不做粉饰；
  自适应划分相对 2.0 只带来约 4% 的改善，没有追平——原因分析在 docs/benchmark.md。
- **并行是真正的优势**：4 核上均匀随机 1.73 倍、重尾 1.84 倍。
- 重复值、离群点、有序、逆序、最坏构造输入，串行就已经领先；有序/逆序快一个数量级。
- 重尾分布是当前**已知弱项**——根因是均值枢轴在右偏分布下被长尾拉偏，不是划分策略的问题。
- CI runner 的核数与负载会变，**只比较同一次运行内的相对关系**，不要跨 run 比绝对值。

## 快速上手

```cpp
#include "meansort.hpp"

std::vector<long long> v = /* 你的数据 */;

meansort::sort(v);                              // 单线程
meansort::parallel_sort(v);                     // 多线程(自动取硬件线程数)
meansort::parallel_sort(v, /*max_threads=*/8);  // 指定线程数
```

编译：

```bash
g++ -O3 -std=c++17 -pthread your_code.cpp
```

MSVC 用户请开启 `/O2 /std:c++17`。

## 适用边界（请务必阅读）

- `T` 必须是**算术类型**（`int`、`long long`、`float`、`double` 等）——枢轴由统计量计算，
  这是本算法的立身之本，也意味着**不能**排 `std::string` 或自定义 struct。
  （传错类型会在编译期由 `static_assert` 拦住，不会变成运行期怪结果。）
- **非稳定排序**。需要稳定性请用 `std::stable_sort`。
- **浮点的 NaN 有明确定义**：本库把 NaN 视为“最大的值”，全部排到末尾，且不参与枢轴计算。
  （注意 `std::sort` 对含 NaN 的区间是未定义行为，这是本库与之的有意差异。）
- 单线程均匀随机数据比 `std::sort` 慢约 **1.3 倍**——这是自适应划分之后的实际差距。
  重复元素、有序数据、最坏构造输入串行即可领先；并行大数据量场景优势明显。
  **如果你排的是小规模、自定义类型、或必须稳定的数据，用 `std::sort` / `std::stable_sort`。**

## 集成方式

### CMake（FetchContent）

```cmake
include(FetchContent)
FetchContent_Declare(meansort
  GIT_REPOSITORY https://github.com/yuyu2929/meansort.git
  GIT_TAG main)
FetchContent_MakeAvailable(meansort)

target_link_libraries(your_target PRIVATE meansort::meansort)
```

### CMake（安装后 find_package）

```bash
cmake -B build && cmake --build build && cmake --install build --prefix /your/prefix
```

```cmake
find_package(meansort CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE meansort::meansort)
```

### 或者直接拷贝

整个库就是 `include/meansort.hpp` 一个文件，拷进你的项目即可。

## 技术设计

1. **截尾均值枢轴**：16 个等距采样先排序，去掉两端各 4 个极端值，对中间 8 个求均值。
   采样起点随机化，抵御针对确定性枢轴的构造性攻击。
2. **自适应划分**：采样时顺带统计中间段的“相邻相等对数”，作为重复度的廉价探针。
   低重复度数据走两路 Hoare 划分（每元素约 1 次比较），高重复度数据走三路划分
   （等值区整层排除）。**这是本库单线程性能的关键**：对任何数据都用三路划分，
   等于让每个元素最坏比较两次。
3. **内省兜底**：递归深度超过 `2·log₂(n)` 切堆排序，最坏情况硬保证 O(n log n)。
4. **有序/逆序检测**：每层分区前两次早退扫描（随机数据仅多 2~4 次比较），
   有序 O(n) 直达、逆序原地翻转。
5. **并行模型**：顶层串行切成约 `2×线程数` 个按规模配比的均衡块，至多 `max_threads`
   个线程通过原子游标抢块（主线程也参与），线程数不会过订阅；线程创建失败自动降级。
6. **NaN 全序**：所有比较走统一的 `less<T>()`（有限值 < NaN，NaN 彼此相等），
   保证三路划分的不变式在浮点特殊值下依然成立。

每个不显然的选择（为什么是均值枢轴、阈值 24 怎么来的、线程数为什么不敢超核数、
哪些方案被否掉了、踩过哪些坑），都记在 **[docs/design.md](docs/design.md)**。

## 基准复现

```bash
cmake -B build -DMEANSORT_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/meansort_bench 2000000      # 参数是元素个数
```

CI 里每次 push 都会跑一遍，日志可查。

## 路线图

- [ ] **重尾分布枢轴**：指数/对数正态下均值枢轴会被长尾拉偏，切分不平衡（当前唯一已知性能短板）
- [ ] 线程池复用：当前每次调用创建线程，高频调用场景可再省一笔
- [ ] 支持自定义比较器（枢轴退化为中位数采样）
- [ ] NUMA 感知的块分配
- [ ] 与 GNU parallel mode / TBB 的横向基准

## 许可证

MeanSort 采用**双许可证**：

- **AGPL-3.0**：开源项目、内部工具、愿意开源衍生代码的产品免费使用；
- **商业许可证**：闭源商用、SaaS 嵌入、无法接受 AGPL 传染条款的企业，请通过
  [GitHub Issues](https://github.com/yuyu2929/meansort/issues) 联系获取授权。

## 致谢

三路划分（荷兰国旗问题）源自 Dijkstra；内省排序由 Musser 提出；
Bentley-McIlroy 的 fat partition 与工程化三路划分思想启发了本设计。

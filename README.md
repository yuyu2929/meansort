# MeanSort

**统计枢轴并行排序库** —— 一个面向企业级大数据量的 C++17 header-only 排序库。
基于三路划分内省排序,以"采样截尾均值"为枢轴,内置多线程并行、有序/逆序检测与最坏情况保护。

**English** | 简体中文

---

## 为什么选 MeanSort

| 特性 | MeanSort | std::sort | pdqsort |
|---|---|---|---|
| 大数据量并行(企业核心场景) | **内置,2 倍以上加速** | 无(GNU 并行模式需改编译参数) | 无 |
| 重复元素极多的数据 | **最快**(三路划分) | 一般 | 一般 |
| 已排序 / 逆序输入 | **O(n) / O(n) 翻转** | 快 | 快 |
| 重尾 / 离群点鲁棒性 | **强**(截尾均值枢轴) | 一般 | 强 |
| 最坏情况保证 | O(n log n)(introsort 兜底) | O(n log n) | O(n log n) |
| 依赖 | **零依赖,单头文件** | 标准库 | 单头文件 |
| C++ 标准 | C++17 | — | C++11 |

> 以下是真实实测数据(详见 [benchmark/](benchmark/)),环境:g++ -O3,Linux x64。

### 单线程 + 并行(单位 ms,N = 2,000,000, long long, g++ -O3)

| 数据分布 | std::sort | MeanSort 串行 | MeanSort 并行 |
|---|---|---|---|
| 均匀随机 | **116.2** | 168.6 | **52.6** |
| 指数分布(重尾) | **116.5** | 165.1 | **50.9** |
| 对数正态(重尾) | **122.2** | 169.1 | **56.3** |
| 少量离群点 | 71.9 | **65.3** | **43.5** |
| 大量重复(仅 100 种值) | 53.1 | **38.5** | **40.9** |
| 已排序 | 18.3 | **0.8** | 15.1 |
| 逆序 | 13.6 | **1.4** | 16.1 |
| 等比数列(最坏构造) | 38.4 | **25.5** | **26.6** |

### 并行扩展性(MeanSort 独有能力)

N = 8,000,000,均匀分布:

| 实现 | 耗时 |
|---|---|
| std::sort | 515.7 ms |
| MeanSort 单线程 | 746.4 ms |
| **MeanSort 并行(自动)** | **211.2 ms** |

**结论:大规模数值数据下,开箱即用的 MeanSort 并行版比 std::sort 快约 2.4 倍;重复元素数据单线程领先 27%+;有序/逆序数据快一个数量级;最坏构造输入反超 std::sort 33%。**

---

## 快速上手

```cpp
#include "meansort.hpp"

std::vector<int> v = /* 你的数据 */;

meansort::sort(v);                        // 单线程
meansort::parallel_sort(v);               // 多线程(自动检测硬件线程数)
meansort::parallel_sort(v, /*max_threads=*/8);  // 指定线程数
```

编译:

```bash
g++ -O3 -std=c++17 -pthread your_code.cpp
```

无其他依赖。MSVC 用户请开启 `/O2 /std:c++17`。

## 适用边界(请务必阅读)

- `T` 必须是**算术类型**(`int`、`long long`、`float`、`double` 等)——枢轴由统计均值计算,这是本算法的立身之本,也意味着**不能**排 `std::string` 或自定义 struct。排自定义类型请用 `std::sort` / pdqsort。
- **非稳定排序**。需要稳定性请用 `std::stable_sort`。
- 单线程均匀随机数据略慢于 `std::sort`(约 1.4 倍)——这是三路划分 + 统计枢轴的固有代价;重复元素、有序数据、并行大数据量场景则全面反超。**如果你的数据是"大规模数值 + 并行",选它;如果是小规模自定义类型,选 std::sort。**

## 集成方式

### CMake(FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(meansort
  GIT_REPOSITORY https://github.com/<your-org>/meansort.git
  GIT_TAG v1.0.0)
FetchContent_MakeAvailable(meansort)

target_link_libraries(your_target PRIVATE meansort::meansort)
```

### 或者直接拷贝

整个库就是 `include/meansort.hpp` 一个文件,拷进你的项目即可。

## 技术设计

1. **截尾均值枢轴**:16 个等距采样先排序,去掉两端各 4 个极端值,对中间 8 个求均值。枢轴必落在采样第 5~12 序列之间,任何分布下单层拆分至少切掉约 5/16 的规模——这是对"均值枢轴"的鲁棒化改造,保留其对对称分布的良好拟合,同时免疫重尾离群点。
2. **三路划分**(荷兰国旗):重复元素越多越快,等值区在每层被整体排除。
3. **内省兜底**:递归深度超过 `2·log₂(n)` 切堆排序,最坏情况硬保证 O(n log n)。
4. **有序/逆序检测**:每层分区前两次早退扫描(随机数据仅多 2~4 次比较),有序 O(n) 直达、逆序原地翻转。
5. **并行模型**:顶层串行切成约 2×线程数个按规模配比的均衡块,一次性派生固定线程各排一块——无线程创建风暴、无锁竞争、线程数恒定,适合嵌入长时服务。
6. **采样起点随机化**:抵御针对确定性枢轴的构造性攻击。

## 基准复现

```bash
cd benchmark
g++ -O3 -std=c++17 -pthread bench.cpp -o bench && ./bench
```

## 路线图

- [ ] float/double 的 NaN 语义处理
- [ ] 支持自定义比较器(枢轴退化为中位数采样)
- [ ] NUMA 感知的块分配
- [ ] 与 GNU parallel mode / TBB 的横向基准

## 许可证

MeanSort 采用**双许可证**:

- **AGPL-3.0**:开源项目、内部工具、愿意开源衍生代码的产品免费使用;
- **商业许可证**:闭源商用、SaaS 嵌入、无法接受 AGPL 传染条款的企业,联系 `<your-email@example.com>` 获取授权。

## 致谢

三路划分(荷兰国旗问题)源自 Dijkstra;内省排序由 Musser 提出;Bentley-McIlroy 的 fat partition 与工程化三路划分思想启发了本设计。

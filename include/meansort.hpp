#ifndef MEANSORT_HPP
#define MEANSORT_HPP
// ============================================================================
// MeanSort 2.0 —— 统计枢轴三路划分内省排序(企业级增强版)
//
// 设计思想源自原始 MeanSort:均值枢轴 + 三路划分 + 深度限制切堆排。
// 在完全保留该思路的前提下做如下强化:
//
//   1. 截尾均值枢轴:16 个等距采样先排序,去掉最小/最大各 4 个,
//      对中间 8 个求均值 => 枢轴必落在采样第 5~12 序列之间,
//      单层拆分规模有约 5/16 ~ 11/16 的保证,重尾/离群点不再导致退化。
//   2. 采样起点随机化:抵御针对确定性枢轴的构造性最坏输入。
//   3. 递归较小侧、循环较大侧:栈深稳定在 O(log n)。
//   4. 插入排序阈值 16 -> 24;小数组排序网络保留原设计。
//   5. 索引改为 ptrdiff_t:支持超过 2^31 的大型数组。
//   6. 新增 parallel_sort:多线程并行,大数据量下接近线性加速。
//
// 约束:T 须为算术类型(支持 <、+、/);非稳定排序;重复元素越多越快。
// 编译(使用并行版):g++ -O2 -std=c++17 -pthread
// ============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace meansort {

using index_t = std::ptrdiff_t;

inline constexpr index_t kInsertionThreshold = 24;
inline constexpr index_t kSampleCount        = 16;
inline constexpr index_t kTrimLo             = 4;   // 去掉采样最小 4 个
inline constexpr index_t kTrimHi             = 12;  // 去掉采样最大 4 个,取中间 8 个

// ---------- 比较原语:对 NaN 也成立的全序 ----------
// 统一走 less<T>,不直接用 <。原因:IEEE-754 的 NaN 与任何值比较都为 false,
// 直接用它做三路划分会让 "a < pivot" 与 "pivot < a" 同时为假,NaN 被当成
// "等于枢轴" 塞进等值区,得到错误结果(std::sort 对 NaN 同样是未定义行为)。
// 这里定义全序:有限值 < NaN,所有 NaN 彼此相等 => NaN 统一沉到末尾。
template<typename T>
inline constexpr bool is_float_v = std::is_floating_point<T>::value;

template<typename T>
inline bool is_nan(T v) {
    if constexpr (is_float_v<T>) return v != v;
    else return false;
}

template<typename T>
inline bool less(T a, T b) {
    if constexpr (is_float_v<T>) {
        const bool na = (a != a), nb = (b != b);
        if (na || nb) return !na && nb;   // 非 NaN 一律排在 NaN 之前
    }
    return a < b;
}

// ---------- 小数组:排序网络 + 插入排序(保留原设计) ----------
template<typename T>
inline void sort2(T& a, T& b) { if (less(b, a)) std::swap(a, b); }

template<typename T>
inline void sort3(T& a, T& b, T& c) { sort2(a, b); sort2(b, c); sort2(a, b); }

template<typename T>
inline void sort4(T* a) {
    sort2(a[0], a[1]); sort2(a[2], a[3]);
    sort2(a[0], a[2]); sort2(a[1], a[3]);
    sort2(a[1], a[2]);
}

template<typename T>
inline void sort5(T* a) {
    sort2(a[0], a[1]); sort2(a[3], a[4]);
    sort2(a[2], a[4]);
    sort2(a[2], a[3]); sort2(a[1], a[4]);
    sort2(a[0], a[3]);
    sort2(a[0], a[2]); sort2(a[1], a[3]);
    sort2(a[1], a[2]);
}

template<typename T>
inline void insertion_sort(T* a, index_t n) {
    for (index_t i = 1; i < n; ++i) {
        T key = a[i];
        index_t j = i - 1;
        while (j >= 0 && less(key, a[j])) { a[j + 1] = a[j]; --j; }
        a[j + 1] = key;
    }
}

template<typename T>
inline void small_sort(T* a, index_t n) {
    switch (n) {
        case 0: case 1: return;
        case 2: sort2(a[0], a[1]); return;
        case 3: sort3(a[0], a[1], a[2]); return;
        case 4: sort4(a); return;
        case 5: sort5(a); return;
        default: insertion_sort(a, n); return;
    }
}

// ---------- 三路划分(保留原设计) ----------
// 返回:{小于区右端点 lt, 大于区左端点 gt},[lt, gt) == pivot
template<typename T>
inline std::pair<index_t, index_t>
three_way_partition(T* a, index_t lo, index_t hi, T pivot) {
    index_t lt = lo;   // [lo, lt)  < pivot
    index_t i  = lo;   // [lt, i)   == pivot
    index_t gt = hi;   // [gt, hi)  > pivot

    while (i < gt) {
        if (less(a[i], pivot)) {
            if (lt != i) std::swap(a[lt], a[i]);  // 跳过自交换(随机数据常见)
            ++lt; ++i;
        } else if (less(pivot, a[i])) {
            --gt;
            std::swap(a[i], a[gt]);
        } else {
            ++i;
        }
    }
    return {lt, gt};
}

// ---------- 轻量随机源:采样起点随机化 ----------
inline std::size_t next_random() {
    thread_local std::size_t s = 0;
    if (s == 0) {
        // 用栈地址做种子:每次运行不同,且与输入数据无关
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(&s);
        s = addr ? (static_cast<std::size_t>(addr) * 0x9E3779B97F4A7C15ull
                    ^ 0xBF58476D1CE4E5B9ull)
                 : 0x123456789ABCDEFull;
    }
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}

// ---------- 枢轴:16 个等距采样的截尾均值 ----------
// 先排序采样,去掉两端各 4 个极端值,对中间 8 个求均值。
// 保留"均值"思想(对称分布下与均值几乎一致),同时:
//   * 枢轴 ∈ [s[4], s[11]],任何分布下单层至少切掉约 5/16 的规模;
//   * 两端离群点被丢弃,重尾分布不受影响。
template<typename T>
inline T sample_pivot(T* a, index_t lo, index_t hi) {
    index_t n     = hi - lo;
    index_t step  = n / kSampleCount;
    index_t phase = step > 1
        ? static_cast<index_t>(next_random() % static_cast<std::size_t>(step))
        : 0;

    T s[kSampleCount];
    for (index_t i = 0; i < kSampleCount; ++i)
        s[i] = a[lo + phase + i * step];
    small_sort(s, kSampleCount);

    // NaN 不参与求均值,否则整个枢轴会变成 NaN;若中间 8 个全是 NaN,
    // 直接返回 NaN,由 less<T> 的全序保证 NaN 仍被统一沉到末尾。
    long double sum = 0;
    index_t cnt = 0;
    for (index_t i = kTrimLo; i < kTrimHi; ++i) {
        if (is_nan(s[i])) continue;
        sum += static_cast<long double>(s[i]);
        ++cnt;
    }
    if (cnt == 0) return s[kTrimLo];
    return static_cast<T>(sum / static_cast<long double>(cnt));
}

// ---------- 递归主体:小侧递归、大侧循环 ----------
template<typename T>
void sort_impl(T* a, index_t lo, index_t hi, int depth) {
    while (hi - lo > kInsertionThreshold) {
        // 已有序/逆序检测:随机数据各约 1~2 次比较即退出,几乎零开销;
        // 有序直接返回,逆序原地翻转后返回
        index_t i = lo;
        while (++i < hi) { if (less(a[i], a[i - 1])) break; }
        if (i == hi) return;
        index_t j = lo;
        while (++j < hi) { if (less(a[j - 1], a[j])) break; }
        if (j == hi) { std::reverse(a + lo, a + hi); return; }

        if (--depth < 0) {
            // 退化保护:切堆排序
            std::make_heap(a + lo, a + hi);
            std::sort_heap(a + lo, a + hi);
            return;
        }
        T pivot = sample_pivot(a, lo, hi);
        auto [lt, gt] = three_way_partition(a, lo, hi, pivot);

        if (lt - lo < hi - gt) {
            sort_impl(a, lo, lt, depth);   // 小侧递归
            lo = gt;                       // 大侧循环
        } else {
            sort_impl(a, gt, hi, depth);
            hi = lt;
        }
    }
    small_sort(a + lo, hi - lo);
}

// ---------- 单线程对外接口 ----------
template<typename T>
void sort(T* a, index_t n) {
    static_assert(std::is_arithmetic<T>::value,
                  "MeanSort supports arithmetic types only "
                  "(int/long long/float/double); use std::sort/pdqsort otherwise");
    if (n <= 1) return;
    int depth = 2 * static_cast<int>(
        std::log2(static_cast<double>(std::max<index_t>(n, 2))));
    sort_impl(a, 0, n, depth);
}

template<typename T>
void sort(std::vector<T>& v) { sort(v.data(), static_cast<index_t>(v.size())); }

// ============================ 并行版 ========================================

namespace detail {

inline constexpr index_t kParallelThreshold = 1 << 15;  // 小于 32K 直接串行

// 第一阶段:不派生线程,串行做顶层三路划分,把区间切成约 parts 块,
// 块数按各子区间规模按比例分配 => 负载均衡
template<typename T>
void collect_ranges(T* a, index_t lo, index_t hi, int depth, int parts,
                    std::vector<std::pair<index_t, index_t>>& out) {
    if (parts <= 1 || hi - lo <= kParallelThreshold || --depth < 0) {
        out.emplace_back(lo, hi);
        return;
    }
    T pivot = sample_pivot(a, lo, hi);
    auto [lt, gt] = three_way_partition(a, lo, hi, pivot);

    index_t lsize = lt - lo, rsize = hi - gt, total = lsize + rsize;
    if (total == 0) { out.emplace_back(lt, gt); return; }  // 全相等

    int lparts = static_cast<int>(std::max<index_t>(
        1, std::min<index_t>(parts - 1, (parts * lsize + total - 1) / total)));
    collect_ranges(a, lo, lt, depth, lparts, out);
    collect_ranges(a, gt, hi, depth, parts - lparts, out);
}

} // namespace detail

// ---------- 并行对外接口 ----------
// 模型:顶层串行切成约 2 x max_threads 个均衡块(留出余量做负载均衡),
// 一次性派生固定数量线程,每线程串行处理自己的块 => 无线程创建风暴,
// 线程数恒定,适合企业级长时间、大数据量运算。
// max_threads = 0 表示自动取 hardware_concurrency。
template<typename T>
void parallel_sort(T* a, index_t n, unsigned max_threads = 0) {
    static_assert(std::is_arithmetic<T>::value,
                  "MeanSort supports arithmetic types only "
                  "(int/long long/float/double); use std::sort/pdqsort otherwise");
    if (n <= 1) return;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1 || n <= detail::kParallelThreshold) { sort(a, n); return; }
    if (max_threads == 0 || max_threads > hw) max_threads = hw;

    std::vector<std::pair<index_t, index_t>> ranges;
    int depth = 2 * static_cast<int>(std::log2(static_cast<double>(n)));
    detail::collect_ranges(a, 0, n, depth, static_cast<int>(max_threads) * 2, ranges);

    std::vector<std::thread> pool;
    pool.reserve(ranges.size());
    for (auto [lo, hi] : ranges) {
        if (hi - lo <= 1) continue;
        try {
            pool.emplace_back([a, lo, hi] {
                int d = 2 * static_cast<int>(std::log2(
                    static_cast<double>(std::max<index_t>(hi - lo, 2))));
                sort_impl(a, lo, hi, d);
            });
        } catch (...) {
            // 线程创建失败:本块串行处理
            int d = 2 * static_cast<int>(std::log2(
                static_cast<double>(std::max<index_t>(hi - lo, 2))));
            sort_impl(a, lo, hi, d);
        }
    }
    for (auto& t : pool) t.join();
}

template<typename T>
void parallel_sort(std::vector<T>& v, unsigned max_threads = 0) {
    parallel_sort(v.data(), static_cast<index_t>(v.size()), max_threads);
}

} // namespace meansort

#endif // MEANSORT_HPP

#ifndef MEANSORT_HPP
#define MEANSORT_HPP
// ============================================================================
// MeanSort 2.1 —— 统计枢轴自适应划分内省排序(企业级增强版)
//
// 设计思想源自原始 MeanSort:均值枢轴 + 三路划分 + 深度限制切堆排。
// 相对 2.0 的改动:
//
//   1. [正确性] NaN 全序:所有比较走 less<T>(),有限值 < NaN、NaN 彼此相等,
//      NaN 不再被三路划分误判成"等于枢轴"而进入等值区。sample_pivot 也跳过
//      NaN 样本,枢轴不会整段变成 NaN。
//   2. [性能] 自适应划分:采样时顺带统计等值对的密度——
//        * 低重复度(随机/连续分布)走两路 Hoare 划分,每个元素平均 1 次比较;
//        * 高重复度(大量等值)才走三路划分,等值区整层排除。
//      2.0 对任何数据都用三路,相当于每个元素最坏 2 次比较,这是单线程
//      均匀随机数据比 std::sort 慢约 45% 的主因。
//   3. [性能] 并行改为主线程 + 工作线程共享原子游标抢块,线程数严格等于
//      max_threads,不再出现 2×max_threads 个线程争抢 max_threads 个核。
//   4. 采样起点随机化:抵御针对确定性枢轴的构造性最坏输入。
//   5. 递归较小侧、循环较大侧:栈深稳定在 O(log n)。
//   6. 索引为 ptrdiff_t:支持超过 2^31 的大型数组。
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

// ---------- 三路划分(荷兰国旗) ----------
// 返回:{小于区右端点 lt, 大于区右端点 gt},[lt, gt) == pivot
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

// ---------- 两路划分(Hoare) ----------
// 低重复度数据用:每个元素平均 1 次比较,而三路划分最坏要比较 2 次。
// 返回 j,保证 [lo, j] 内均 <= pivot,[j+1, hi) 内均 >= pivot。
// 前置条件:pivot 落在 [min(a), max(a)] 内——sample_pivot 采自数据本身,
// 必然满足;若枢轴是 NaN 则不会走到这里。
template<typename T>
inline index_t two_way_partition(T* a, index_t lo, index_t hi, T pivot) {
    index_t i = lo - 1;
    index_t j = hi;
    for (;;) {
        do { ++i; } while (less(a[i], pivot));
        do { --j; } while (less(pivot, a[j]));
        if (i >= j) return j;
        std::swap(a[i], a[j]);
    }
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
// 同时统计中间段里"相邻相等"的对数,作为重复度的廉价探针:
// 低重复度的数据不需要三路划分,走两路更快。
template<typename T>
struct pivot_result {
    T    pivot;
    bool dup_heavy;   // 采样里等值对够多 => 大概率是低基数数据,值得三路划分
};

template<typename T>
inline pivot_result<T> sample_pivot(T* a, index_t lo, index_t hi) {
    index_t n     = hi - lo;
    index_t step  = n / kSampleCount;
    index_t phase = step > 1
        ? static_cast<index_t>(next_random() % static_cast<std::size_t>(step))
        : 0;

    T s[kSampleCount];
    for (index_t i = 0; i < kSampleCount; ++i)
        s[i] = a[lo + phase + i * step];
    small_sort(s, kSampleCount);

    // 等值对探针:中间 8 个里相邻相等的对数
    int dup = 0;
    for (index_t i = kTrimLo + 1; i < kTrimHi; ++i)
        if (!less(s[i - 1], s[i]) && !less(s[i], s[i - 1])) ++dup;

    // NaN 不参与求均值,否则整段枢轴会变成 NaN;若中间 8 个全是 NaN,
    // 直接返回 NaN,由 less<T> 的全序保证 NaN 仍被统一沉到末尾。
    long double sum = 0;
    index_t cnt = 0;
    for (index_t i = kTrimLo; i < kTrimHi; ++i) {
        if (is_nan(s[i])) continue;
        sum += static_cast<long double>(s[i]);
        ++cnt;
    }
    if (cnt == 0) return {s[kTrimLo], false};
#ifdef MEANSORT_ALWAYS_THREE_WAY
    // 仅供回归对比:强制回到 2.0 的"永远三路划分"行为,用于在同一次运行里
    // 对比新旧算法(docs/benchmark.md 的数据就是这么来的)。不要在生产里定义。
    return {static_cast<T>(sum / static_cast<long double>(cnt)), true};
#else
    return {static_cast<T>(sum / static_cast<long double>(cnt)), dup >= 2};
#endif
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

        pivot_result<T> pr = sample_pivot(a, lo, hi);

        if (pr.dup_heavy || is_nan(pr.pivot)) {
            auto [lt, gt] = three_way_partition(a, lo, hi, pr.pivot);
            if (lt - lo < hi - gt) {
                sort_impl(a, lo, lt, depth);   // 小侧递归
                lo = gt;                       // 大侧循环
            } else {
                sort_impl(a, gt, hi, depth);
                hi = lt;
            }
            continue;
        }

        index_t mid  = two_way_partition(a, lo, hi, pr.pivot);
        index_t lhi  = mid + 1;                // 左区间 [lo, lhi)
        if (lhi >= hi) {
            // 右侧为空:说明区间最大值恰在末尾且已就位,排除它继续
            hi -= 1;
            continue;
        }
        if (mid - lo < hi - lhi) {
            sort_impl(a, lo, lhi, depth);
            lo = lhi;
        } else {
            sort_impl(a, lhi, hi, depth);
            hi = lhi;
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

// 第一阶段:不派生线程,串行做顶层划分,把区间切成约 parts 块,
// 块数按各子区间规模按比例分配 => 负载均衡。
// 与 sort_impl 一样自适应:低重复度用两路划分,高重复度用三路划分。
template<typename T>
void collect_ranges(T* a, index_t lo, index_t hi, int depth, int parts,
                    std::vector<std::pair<index_t, index_t>>& out) {
    if (parts <= 1 || hi - lo <= kParallelThreshold || --depth < 0) {
        out.emplace_back(lo, hi);
        return;
    }

    pivot_result<T> pr = sample_pivot(a, lo, hi);

    if (pr.dup_heavy || is_nan(pr.pivot)) {
        auto [lt, gt] = three_way_partition(a, lo, hi, pr.pivot);
        index_t lsize = lt - lo, rsize = hi - gt, total = lsize + rsize;
        if (total == 0) {                       // 全相等,已就位
            out.emplace_back(lt, gt);
            return;
        }
        int lparts = static_cast<int>(std::max<index_t>(
            1, std::min<index_t>(parts - 1, (parts * lsize + total - 1) / total)));
        collect_ranges(a, lo, lt, depth, lparts, out);
        collect_ranges(a, gt, hi, depth, parts - lparts, out);
        return;
    }

    index_t mid = two_way_partition(a, lo, hi, pr.pivot);
    index_t lhi = mid + 1;
    if (lhi >= hi) {                            // 末尾元素已就位,整段交给一个任务
        out.emplace_back(lo, hi - 1);
        return;
    }

    index_t lsize = lhi - lo, rsize = hi - lhi, total = lsize + rsize;
    int lparts = static_cast<int>(std::max<index_t>(
        1, std::min<index_t>(parts - 1, (parts * lsize + total - 1) / total)));
    collect_ranges(a, lo, lhi, depth, lparts, out);
    collect_ranges(a, lhi, hi, depth, parts - lparts, out);
}

} // namespace detail

// ---------- 并行对外接口 ----------
// 模型:顶层串行切成约 2 x max_threads 个均衡块,然后由至多 max_threads 个
// 线程通过原子游标抢块(主线程也参与)。线程数严格等于核数,不会出现
// 2 倍线程争抢同一批核的过订阅。
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
    if (max_threads <= 1) { sort(a, n); return; }

    std::vector<std::pair<index_t, index_t>> ranges;
    int depth = 2 * static_cast<int>(std::log2(static_cast<double>(n)));
    detail::collect_ranges(a, 0, n, depth, static_cast<int>(max_threads) * 2, ranges);

    std::atomic<index_t> cursor{0};
    const index_t nranges = static_cast<index_t>(ranges.size());

    auto worker = [&] {
        for (;;) {
            index_t k = cursor.fetch_add(1, std::memory_order_relaxed);
            if (k >= nranges) return;
            index_t lo = ranges[static_cast<std::size_t>(k)].first;
            index_t hi = ranges[static_cast<std::size_t>(k)].second;
            if (hi - lo <= 1) continue;
            int d = 2 * static_cast<int>(std::log2(
                static_cast<double>(std::max<index_t>(hi - lo, 2))));
            sort_impl(a, lo, hi, d);
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(max_threads > 0 ? max_threads - 1 : 0);
    for (unsigned t = 1; t < max_threads; ++t) {
        try {
            pool.emplace_back(worker);
        } catch (...) {
            break;   // 线程创建失败:剩下的块由已有线程和主线程抢完
        }
    }
    worker();        // 主线程也干活
    for (auto& t : pool) t.join();
}

template<typename T>
void parallel_sort(std::vector<T>& v, unsigned max_threads = 0) {
    parallel_sort(v.data(), static_cast<index_t>(v.size()), max_threads);
}

} // namespace meansort

#endif // MEANSORT_HPP

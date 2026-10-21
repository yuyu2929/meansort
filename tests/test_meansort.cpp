// MeanSort 正确性测试套件
// 编译: g++ -O2 -std=c++17 -pthread test_meansort.cpp -o test && ./test
#include "../include/meansort.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;      // 通过的检查项计数（CI 会把它写进 job summary）

template<typename SortFn>
void check(const char* name, std::vector<long long> v, SortFn sort_fn) {
    auto expect = v;
    std::sort(expect.begin(), expect.end());
    sort_fn(v);
    if (v != expect) {
        std::cerr << "[FAIL] " << name << "\n";
        ++failures;
    } else {
        ++checks;
        std::cout << "[ OK ] " << name << "\n";
    }
}

template<typename SortFn>
void run_suite(const char* tag, SortFn sort_fn) {
    std::mt19937_64 rng(12345);

    // 边界规模
    check((std::string(tag) + " 空数组").c_str(), {}, sort_fn);
    check((std::string(tag) + " 单元素").c_str(), {42}, sort_fn);
    check((std::string(tag) + " 两元素").c_str(), {2, 1}, sort_fn);
    for (int n : {3, 4, 5, 6, 16, 17, 23, 24, 25, 255, 256, 257, 4096}) {
        std::vector<long long> v(n);
        for (auto& x : v) x = (long long)rng();
        check((std::string(tag) + " 随机 n=" + std::to_string(n)).c_str(), v, sort_fn);
    }
    // 特殊模式
    {
        std::vector<long long> v(10000, 7);
        check((std::string(tag) + " 全相同").c_str(), v, sort_fn);
        v.clear(); for (int i = 0; i < 10000; ++i) v.push_back(i);
        check((std::string(tag) + " 已排序").c_str(), v, sort_fn);
        v.clear(); for (int i = 0; i < 10000; ++i) v.push_back(10000 - i);
        check((std::string(tag) + " 逆序").c_str(), v, sort_fn);
        v.clear(); for (int i = 0; i < 10000; ++i) v.push_back(1LL << (i % 62));
        check((std::string(tag) + " 等比数列(最坏构造)").c_str(), v, sort_fn);
        v.clear(); for (int i = 0; i < 50000; ++i) v.push_back(rng() % 10);
        check((std::string(tag) + " 大量重复").c_str(), v, sort_fn);
        v.clear(); for (int i = 0; i < 50000; ++i)
            v.push_back(rng() % 100 == 0 ? (long long)rng() : (long long)(rng() % 1000));
        check((std::string(tag) + " 离群点").c_str(), v, sort_fn);
        // 含负数与极值
        v = {-9223372036854775807LL - 1, 9223372036854775807LL, -1, 0, 1,
             -9223372036854775807LL - 1, 9223372036854775807LL};
        check((std::string(tag) + " int64 极值").c_str(), v, sort_fn);
    }
    // 随机模糊:多轮、任意规模、任意值域
    for (int t = 0; t < 300; ++t) {
        int n = (int)(rng() % 3000);
        std::vector<long long> v(n);
        long long span = 1LL << (rng() % 63);
        for (auto& x : v) x = (long long)(rng() % span) - (long long)(span / 2);
        check((std::string(tag) + " 模糊 #" + std::to_string(t)).c_str(), v, sort_fn);
    }
}

// fat partition 的独立验证(随机 pivot + 重复值域)
void partition_fuzz() {
    std::mt19937 rng(777);
    for (int t = 0; t < 200000; ++t) {
        int n = 1 + (int)(rng() % 60);
        std::vector<int> v(n);
        int span = 1 + (int)(rng() % 8);
        for (auto& x : v) x = (int)(rng() % span);
        int pivot = (int)(rng() % span);
        auto save = v;
        auto [lt, gt] = meansort::three_way_partition(v.data(), 0, n, pivot);
        bool ok = true;
        for (int k = 0; k < lt; ++k) if (!(v[k] < pivot)) ok = false;
        for (int k = lt; k < gt; ++k) if (v[k] != pivot) ok = false;
        for (int k = gt; k < n; ++k) if (!(pivot < v[k])) ok = false;
        std::sort(save.begin(), save.end());
        std::sort(v.begin(), v.end());
        if (save != v) ok = false;
        if (!ok) { std::cerr << "[FAIL] partition fuzz #" << t << "\n"; ++failures; return; }
    }
    ++checks;
    std::cout << "[ OK ] partition fuzz (200000 轮)\n";
}

// double 类型
void test_double() {
    std::mt19937 rng(99);
    std::exponential_distribution<double> d(1.0);
    std::vector<double> v(50000);
    for (auto& x : v) x = d(rng) * 1e6;
    auto expect = v;
    std::sort(expect.begin(), expect.end());
    meansort::parallel_sort(v);
    if (v != expect) { std::cerr << "[FAIL] double\n"; ++failures; }
    else { ++checks; std::cout << "[ OK ] double 重尾分布\n"; }
}

// NaN 全序验证:有限值必须升序且全部排在 NaN 之前,NaN 数量不变。
// 注意不能拿 std::sort 当对照——std::sort 遇到 NaN 会破坏严格弱序,行为未定义。
void test_nan() {
    std::mt19937_64 rng(2024);
    std::vector<double> v;
    v.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        if (i % 37 == 0) v.push_back(std::nan(""));
        else v.push_back((double)(rng() % 1000) - 500.0);
    }

    std::vector<double> a = v, b = v;
    meansort::sort(a);
    meansort::parallel_sort(b);

    size_t nan_in = 0;
    for (double x : v) if (std::isnan(x)) ++nan_in;

    auto verify = [&](const std::vector<double>& s, const char* tag) {
        bool ok = (s.size() == v.size());
        bool first = true, seen_nan = false;
        double prev = 0.0;
        size_t nan_out = 0;
        for (double x : s) {
            if (std::isnan(x)) { seen_nan = true; ++nan_out; continue; }
            if (seen_nan) { ok = false; break; }        // NaN 之后还有有限值 => 失败
            if (!first && x < prev) { ok = false; break; }
            prev = x; first = false;
        }
        if (nan_out != nan_in) ok = false;
        if (!ok) { std::cerr << "[FAIL] NaN " << tag << "\n"; ++failures; }
        else { ++checks; std::cout << "[ OK ] NaN " << tag << "\n"; }
    };
    verify(a, "串行");
    verify(b, "并行");
}

int main() {
    run_suite("串行", [](std::vector<long long>& v) { meansort::sort(v); });
    run_suite("并行", [](std::vector<long long>& v) { meansort::parallel_sort(v); });
    partition_fuzz();
    test_double();
    test_nan();
    if (failures) { std::cerr << failures << " 项失败\n"; return 1; }
    std::cout << "\n全部通过: " << checks << " 项检查\n";
    std::cout << "CHECKS=" << checks << "\n";   // 给 CI 抓取的稳定标记（ASCII）
    return 0;
}

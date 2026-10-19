// MeanSort 基准测试
// 编译: g++ -O3 -std=c++17 -pthread bench.cpp -o bench && ./bench
// 对照:std::sort / MeanSort 串行 / MeanSort 并行
#include "../include/meansort.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

template<typename F>
double timeit(F f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main(int argc, char** argv) {
    long long N = argc > 1 ? std::atoll(argv[1]) : 2000000;
    std::mt19937 rng(42);
    std::exponential_distribution<double> exp1(1.0);
    std::lognormal_distribution<double> ln(0.0, 3.0);

    std::cout << "N = " << N << "  硬件线程数: " << std::thread::hardware_concurrency() << "\n\n";
    printf("%-24s %12s %12s %12s\n", "数据分布", "std::sort", "MeanSort串行", "MeanSort并行");

    auto bench = [&](const char* name, auto gen) {
        std::vector<long long> v(N); for (auto& x : v) x = gen();
        auto orig = v;                         // 保留原始输入(保留有序/逆序等模式)
        std::sort(orig.begin(), orig.end());
        auto& expect = orig;                   // 正确性对照
        bool ok_all = true;

        double t_std, t_ser, t_par;
        { auto w = v; t_std = timeit([&] { std::sort(w.begin(), w.end()); }); ok_all &= (w == expect); }
        { auto w = v; t_ser = timeit([&] { meansort::sort(w); }); ok_all &= (w == expect); }
        { auto w = v; t_par = timeit([&] { meansort::parallel_sort(w); }); ok_all &= (w == expect); }

        printf("%-24s %10.1fms %10.1fms %10.1fms  %s\n",
               name, t_std, t_ser, t_par, ok_all ? "" : "(正确性 FAIL!)");
    };

    bench("均匀分布",        [&] { return (long long)(rng()); });
    bench("指数分布(重尾)",  [&] { return (long long)(exp1(rng) * 1e12); });
    bench("对数正态(重尾)",  [&] { return (long long)(ln(rng) * 1e9); });
    bench("少量离群点",      [&] { return rng() % 100 == 0 ? (long long)rng() : (long long)(rng() % 1000); });
    bench("大量重复(100种)", [&] { return (long long)(rng() % 100); });
    bench("已排序",          [i = 0LL]() mutable { return i++; });
    bench("逆序",            [i = N]() mutable { return i--; });
    bench("等比数列(最坏)",  [i = 0LL]() mutable { return 1LL << (i++ % 62); });
    return 0;
}

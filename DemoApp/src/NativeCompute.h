//=============================================================================
// NativeCompute.h
//
// 宿主应用**自己**的平台相关代码。
//
// 迁移重点：
//   和不可修改的第三方 DLL 不同，这部分代码是我们自己的，迁移到 ARM64EC 时
//   既可以“原样保留”（让它以 x64 兼容方式参与编译），也可以替换为 ARM64
//   原生实现以获得性能。本文件集中了三类典型的 x64 依赖：
//
//     1. SSE2 内在函数（intrinsics）      —— ARM64EC 下由 softintrin 软件模拟
//     2. __cpuid / __rdtsc 等 x64 专有内在函数 —— ARM64EC 下语义不同或不可用
//     3. MASM 汇编（FastMath.asm）        —— ARM64EC 完全不支持，必须改写
//=============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

//-----------------------------------------------------------------------------
// 由 FastMath.asm 提供的 x64 手写汇编函数。
//
// 迁移阻断点：
//   ml64.exe（MASM x64 汇编器）**不能**为 ARM64EC 生成目标文件。
//   迁移时必须二选一：
//     (a) 用 C/C++ 或 ARM64 内在函数重写；
//     (b) 用 armasm64.exe 重写为 ARM64 汇编。
//-----------------------------------------------------------------------------
extern "C" uint64_t FastMath_SumU32(const uint32_t* data, size_t count);

namespace NativeCompute
{
    // 返回 CPU 品牌字符串（通过 CPUID 叶 0x80000002..0x80000004 读取）。
    std::wstring GetCpuBrandString();

    // 返回 CPUID 探测到的指令集支持情况摘要。
    std::wstring GetCpuFeatureSummary();

    // 读取时间戳计数器。
    uint64_t ReadTimestampCounter();

    // 使用应用自身的 SSE2 实现做亮度调整（与第三方 DLL 的实现算法一致，
    // 用于验证“自己的 SIMD 代码”和“第三方 SIMD 代码”结果是否一致）。
    void AdjustBrightnessLocal(uint8_t* pixels, size_t count, float gain);

    // 生成确定性的测试像素数据，保证迁移前后输入完全相同。
    void FillTestPattern(uint8_t* pixels, size_t count, uint32_t seed);
}

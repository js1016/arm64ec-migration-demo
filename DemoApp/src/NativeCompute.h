//=============================================================================
// NativeCompute.h
//
// 宿主应用**自己**的平台相关代码。
//
// 迁移重点：
//   和不可修改的第三方 DLL 不同，这部分代码是我们自己的，因此迁移到 ARM64EC
//   时应当尽量改成 ARM64 原生实现，才能真正拿到性能收益。
//   本文件集中了三类典型的 x64 依赖，第二阶段已全部完成迁移：
//
//     1. SSE2 内在函数
//          迁移前：#if defined(_M_X64) 包住的 SSE2 代码。
//          陷阱  ：ARM64EC 下 _M_X64 **仍然被定义**，这段代码会被选中，
//                  并通过 softintrin.lib 以软件方式模拟 SSE2 —— 能跑但很慢。
//          迁移后：条件判断改为先检查 _M_ARM64EC / _M_ARM64，走 NEON 实现。
//
//     2. __cpuid / __rdtsc 等 x64 专有内在函数
//          迁移后：改用架构中立的 Win32 API
//                  （注册表 ProcessorNameString、IsProcessorFeaturePresent、
//                   QueryPerformanceCounter）。
//
//     3. MASM 汇编（FastMath.asm）
//          ml64.exe 无法为 ARM64EC 生成目标文件，属于硬性阻断。
//          迁移后：新增 FastMathArm64.cpp，用 NEON 内在函数提供等价实现，
//                  两个文件按平台在工程里互斥编译。
//=============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

//-----------------------------------------------------------------------------
// 以 64 位精度累加一组 32 位无符号整数。
//
// 同一个符号由两份按平台互斥编译的实现提供：
//     x64            -> FastMath.asm        （MASM 手写汇编，保持原样）
//     ARM64 / ARM64EC -> FastMathArm64.cpp  （NEON 内在函数，新增）
//
// 调用方代码无需感知差异，这正是「用平台专有实现替换阻断点」的标准做法。
//-----------------------------------------------------------------------------
extern "C" uint64_t FastMath_SumU32(const uint32_t* data, size_t count);

namespace NativeCompute
{
    // 返回 CPU 品牌字符串（通过 CPUID 叶 0x80000002..0x80000004 读取）。
    std::wstring GetCpuBrandString();

    // 返回 CPU 指令集 / 扩展的支持情况摘要。
    std::wstring GetCpuFeatureSummary();

    // 读取高精度性能计数器（架构中立，取代原先的 __rdtsc）。
    uint64_t ReadPerformanceCounter();

    // 性能计数器每秒的滴答数，用于把计数差值换算成时间。
    uint64_t GetPerformanceFrequency();

    // 使用应用自身的 SIMD 实现做亮度调整（与第三方 DLL 的实现算法一致，
    // 用于验证“自己的 SIMD 代码”和“第三方 SIMD 代码”结果是否一致）。
    // x64 走 SSE2，ARM64/ARM64EC 走 NEON，两者结果逐字节相同。
    void AdjustBrightnessLocal(uint8_t* pixels, size_t count, float gain);

    // 返回当前构建实际启用的 SIMD 实现名称（NEON / SSE2 / 标量）。
    std::wstring GetActiveSimdPath();

    // 生成确定性的测试像素数据，保证迁移前后输入完全相同。
    void FillTestPattern(uint8_t* pixels, size_t count, uint32_t seed);
}

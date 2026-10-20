//=============================================================================
// FastMathArm64.cpp
//
// FastMath_SumU32() 的 ARM64 / ARM64EC 实现。
//
// 【迁移背景】
//   x64 版本的同名函数写在 FastMath.asm 里，由 MASM（ml64.exe）汇编。
//   但 ml64.exe **只能产出 x64 目标文件，无法为 ARM64EC 生成代码**，
//   这是整个迁移中最“硬”的一处阻断点。
//
//   可选的三种处理方式：
//     (a) 用 C/C++ 重写 —— 最省事，编译器通常能自动向量化；
//     (b) 用 ARM64 内在函数（NEON）重写 —— 兼顾可读性与性能；   ← 本文件采用
//     (c) 用 armasm64.exe 写 ARM64 汇编 —— 只有在确实需要逐指令
//         控制时才值得，而且若要被 x64 代码直接调用还需处理 entry thunk。
//
//   工程配置上：
//     * FastMath.asm        在 ARM64EC 平台被 ExcludedFromBuild 排除；
//     * FastMathArm64.cpp   在 x64 平台被 ExcludedFromBuild 排除。
//   两者对外提供完全相同的符号与语义，调用方代码一个字都不用改。
//=============================================================================

#include "NativeCompute.h"

//-----------------------------------------------------------------------------
// 双保险：即使工程配置里的平台排除失效，这个条件编译也能保证不会和
// FastMath.asm 产生重复符号（在 x64 下本文件会编译成一个空的翻译单元）。
//-----------------------------------------------------------------------------
#if defined(_M_ARM64) || defined(_M_ARM64EC)

#include <arm64_neon.h>

//-----------------------------------------------------------------------------
// uint64_t FastMath_SumU32(const uint32_t* data, size_t count)
//
// 把 count 个 32 位无符号整数以 64 位精度累加。
// 与 x64 汇编版本逐位等价（包括溢出行为），因此「结果一致性对比」功能
// 在两种架构上会给出完全相同的数值。
//-----------------------------------------------------------------------------
extern "C" uint64_t FastMath_SumU32(const uint32_t* data, size_t count)
{
    if (data == nullptr || count == 0)
    {
        return 0;
    }

    // 用两个 64 位通道做累加，避免 32 位累加溢出
    uint64x2_t acc0 = vdupq_n_u64(0);
    uint64x2_t acc1 = vdupq_n_u64(0);

    size_t i = 0;

    // 主循环：每轮处理 8 个 uint32
    for (; i + 8 <= count; i += 8)
    {
        const uint32x4_t v0 = vld1q_u32(data + i);
        const uint32x4_t v1 = vld1q_u32(data + i + 4);

        // vpaddlq_u32：把相邻的两个 32 位无符号数两两相加并扩展为 64 位
        acc0 = vaddq_u64(acc0, vpaddlq_u32(v0));
        acc1 = vaddq_u64(acc1, vpaddlq_u32(v1));
    }

    const uint64x2_t acc = vaddq_u64(acc0, acc1);
    uint64_t sum = vgetq_lane_u64(acc, 0) + vgetq_lane_u64(acc, 1);

    // 尾部标量处理
    for (; i < count; ++i)
    {
        sum += data[i];
    }

    return sum;
}

#endif // defined(_M_ARM64) || defined(_M_ARM64EC)

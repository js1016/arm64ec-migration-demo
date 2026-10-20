//=============================================================================
// NativeCompute.cpp —— 宿主应用自身的平台相关代码实现
//=============================================================================

#include "NativeCompute.h"

#include <windows.h>
#include <intrin.h>     // __cpuid / __rdtsc
#include <emmintrin.h>  // SSE2
#include <array>

namespace NativeCompute
{

//-----------------------------------------------------------------------------
// CPU 品牌字符串
//
// 迁移观察点：
//   __cpuid 是 x64 专有内在函数。在 ARM64EC 下它虽然可以通过模拟层调用到，
//   但返回的是模拟器合成的信息，而不是真实 ARM64 CPU 的标识；
//   并且在纯 ARM64 原生代码中根本不存在该内在函数。
//   因此所有依赖 CPUID 做“功能探测 / 授权校验 / 性能分支”的代码，
//   在迁移时都必须重新设计。
//-----------------------------------------------------------------------------
std::wstring GetCpuBrandString()
{
    std::array<int, 4> regs = {};

    // 先查询扩展功能的最大叶编号
    __cpuid(regs.data(), 0x80000000);
    const unsigned maxExtendedLeaf = static_cast<unsigned>(regs[0]);

    if (maxExtendedLeaf < 0x80000004u)
    {
        return L"<CPU 不支持品牌字符串查询>";
    }

    // 0x80000002 ~ 0x80000004 三个叶各返回 16 字节 ASCII
    char brand[49] = {};
    for (unsigned leaf = 0; leaf < 3; ++leaf)
    {
        __cpuid(regs.data(), static_cast<int>(0x80000002u + leaf));
        memcpy(brand + leaf * 16, regs.data(), 16);
    }

    wchar_t wide[64] = {};
    size_t  converted = 0;
    mbstowcs_s(&converted, wide, _countof(wide), brand, _TRUNCATE);

    // 去掉 Intel/AMD 常见的前导空格
    std::wstring result(wide);
    const size_t firstNonSpace = result.find_first_not_of(L' ');
    return firstNonSpace == std::wstring::npos ? result : result.substr(firstNonSpace);
}

//-----------------------------------------------------------------------------
// 指令集特性摘要
//-----------------------------------------------------------------------------
std::wstring GetCpuFeatureSummary()
{
    std::array<int, 4> regs = {};
    __cpuid(regs.data(), 1);

    const int edx = regs[3];
    const int ecx = regs[2];

    std::wstring result;
    auto append = [&result](const wchar_t* name, bool supported)
    {
        if (supported)
        {
            if (!result.empty()) { result += L" "; }
            result += name;
        }
    };

    append(L"SSE",    (edx & (1 << 25)) != 0);
    append(L"SSE2",   (edx & (1 << 26)) != 0);
    append(L"SSE3",   (ecx & (1 << 0))  != 0);
    append(L"SSSE3",  (ecx & (1 << 9))  != 0);
    append(L"SSE4.1", (ecx & (1 << 19)) != 0);
    append(L"SSE4.2", (ecx & (1 << 20)) != 0);
    append(L"AVX",    (ecx & (1 << 28)) != 0);

    return result.empty() ? L"<无>" : result;
}

//-----------------------------------------------------------------------------
// 时间戳计数器
//
// 迁移观察点：
//   __rdtsc 在 ARM64EC 下会被映射到 ARM64 的通用计时寄存器，频率与语义
//   都和 x86 的 TSC 不同。任何“把 TSC 差值当成 CPU 周期数”的计算都需要
//   改用 QueryPerformanceCounter。
//-----------------------------------------------------------------------------
uint64_t ReadTimestampCounter()
{
    return __rdtsc();
}

//-----------------------------------------------------------------------------
// 应用自身的 SSE2 亮度调整实现。
//
// 迁移观察点：
//   这段代码被 `#if defined(_M_X64)` 包裹。注意：ARM64EC 下 _M_X64 **仍然
//   被定义**，所以这个分支依然会被选中，并通过 softintrin.lib 以软件方式
//   模拟 SSE2 指令 —— 能跑通，但性能远不如 NEON。
//   正确的迁移做法是把条件改为：
//       #if defined(_M_ARM64) || defined(_M_ARM64EC)   -> 使用 NEON
//       #elif defined(_M_X64)                          -> 使用 SSE2
//-----------------------------------------------------------------------------
void AdjustBrightnessLocal(uint8_t* pixels, size_t count, float gain)
{
    if (pixels == nullptr || count == 0)
    {
        return;
    }

    if (gain < 0.0f) { gain = 0.0f; }
    if (gain > 4.0f) { gain = 4.0f; }

    const uint16_t fixedGain = static_cast<uint16_t>(gain * 256.0f + 0.5f);

    size_t i = 0;

#if defined(_M_X64)
    // ---- SSE2 向量路径：一次处理 16 个像素 ----
    const __m128i vGain = _mm_set1_epi16(static_cast<short>(fixedGain));
    const __m128i vZero = _mm_setzero_si128();

    for (; i + 16 <= count; i += 16)
    {
        __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + i));

        __m128i lo = _mm_unpacklo_epi8(src, vZero);
        __m128i hi = _mm_unpackhi_epi8(src, vZero);

        lo = _mm_mulhi_epu16(_mm_slli_epi16(lo, 8), vGain);
        hi = _mm_mulhi_epu16(_mm_slli_epi16(hi, 8), vGain);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + i),
                         _mm_packus_epi16(lo, hi));
    }
#endif

    // ---- 标量尾部（或非 x64 平台的完整路径）----
    for (; i < count; ++i)
    {
        const uint32_t v = ((static_cast<uint32_t>(pixels[i]) << 8) * fixedGain) >> 16;
        pixels[i] = static_cast<uint8_t>(v > 255u ? 255u : v);
    }
}

//-----------------------------------------------------------------------------
// 确定性测试图案生成（线性同余伪随机数，跨架构结果完全一致）
//-----------------------------------------------------------------------------
void FillTestPattern(uint8_t* pixels, size_t count, uint32_t seed)
{
    if (pixels == nullptr)
    {
        return;
    }

    uint32_t state = seed != 0 ? seed : 1u;
    for (size_t i = 0; i < count; ++i)
    {
        state = state * 1664525u + 1013904223u;   // Numerical Recipes LCG
        pixels[i] = static_cast<uint8_t>((state >> 16) & 0xFFu);
    }
}

} // namespace NativeCompute

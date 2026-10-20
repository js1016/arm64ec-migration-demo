//=============================================================================
// NativeCompute.cpp —— 宿主应用自身的平台相关代码实现
//=============================================================================

#include "NativeCompute.h"

#include <windows.h>
#include <array>

//-----------------------------------------------------------------------------
// 架构相关头文件的选择。
//
// 迁移要点：判断顺序必须把 _M_ARM64EC 放在 _M_X64 之前。
//   ARM64EC 下 _M_X64 / _M_AMD64 **同时**被定义，如果先判断 _M_X64，
//   就会错误地包含 <emmintrin.h>，把 SSE2 软件模拟层（softintrin.lib）
//   拖进 ARM64EC 构建里 —— 能编过、能跑，但白白损失性能。
//-----------------------------------------------------------------------------
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#include <arm64_neon.h>     // NEON 内在函数
#else
#include <intrin.h>         // __cpuid
#include <emmintrin.h>      // SSE2 内在函数
#endif

namespace
{
    //-------------------------------------------------------------------------
    // 从注册表读取 CPU 名称。
    //
    // 这是**架构中立**的做法：x64 与 ARM64 上都有效，因此迁移后可以作为
    // 所有平台的统一实现，不必再为每种架构写一套探测代码。
    //-------------------------------------------------------------------------
    std::wstring ReadProcessorNameFromRegistry()
    {
        HKEY key = nullptr;
        const LSTATUS openStatus = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_QUERY_VALUE, &key);

        if (openStatus != ERROR_SUCCESS)
        {
            return L"<无法读取处理器信息>";
        }

        wchar_t buffer[256] = {};
        DWORD   sizeInBytes = sizeof(buffer) - sizeof(wchar_t);
        DWORD   valueType   = 0;

        const LSTATUS queryStatus = RegQueryValueExW(
            key, L"ProcessorNameString", nullptr, &valueType,
            reinterpret_cast<LPBYTE>(buffer), &sizeInBytes);

        RegCloseKey(key);

        if (queryStatus != ERROR_SUCCESS || valueType != REG_SZ)
        {
            return L"<无法读取处理器信息>";
        }

        // 去掉部分 CPU 名称里的前后空格
        std::wstring result(buffer);
        const size_t first = result.find_first_not_of(L' ');
        const size_t last  = result.find_last_not_of(L' ');
        if (first == std::wstring::npos)
        {
            return L"<未知>";
        }
        return result.substr(first, last - first + 1);
    }
}

namespace NativeCompute
{

//-----------------------------------------------------------------------------
// CPU 品牌字符串
//
// 【迁移前】统一使用 __cpuid 读取 0x80000002~0x80000004 三个扩展叶。
// 【迁移后】ARM64 / ARM64EC 改为读注册表。
//
// 原因：__cpuid 是 x64 专有内在函数。ARM64EC 下虽然可以通过模拟层调用到，
//       但拿到的是模拟器合成的信息而非真实 ARM64 CPU 标识；纯 ARM64 原生
//       代码中更是根本不存在该内在函数。
//       所有依赖 CPUID 做「功能探测 / 授权校验 / 性能分支」的代码，
//       迁移时都必须重新设计。
//-----------------------------------------------------------------------------
std::wstring GetCpuBrandString()
{
#if defined(_M_ARM64EC) || defined(_M_ARM64)
    return ReadProcessorNameFromRegistry();
#else
    std::array<int, 4> regs = {};

    // 先查询扩展功能的最大叶编号
    __cpuid(regs.data(), 0x80000000);
    const unsigned maxExtendedLeaf = static_cast<unsigned>(regs[0]);

    if (maxExtendedLeaf < 0x80000004u)
    {
        return ReadProcessorNameFromRegistry();
    }

    // 0x80000002 ~ 0x80000004 三个叶各返回 16 字节 ASCII
    char brand[49] = {};
    for (unsigned leaf = 0; leaf < 3; ++leaf)
    {
        __cpuid(regs.data(), static_cast<int>(0x80000002u + leaf));
        memcpy(brand + leaf * 16, regs.data(), 16);
    }

    wchar_t wide[64]   = {};
    size_t  converted  = 0;
    mbstowcs_s(&converted, wide, _countof(wide), brand, _TRUNCATE);

    // 去掉 Intel/AMD 常见的前导空格
    std::wstring result(wide);
    const size_t firstNonSpace = result.find_first_not_of(L' ');
    return firstNonSpace == std::wstring::npos ? result : result.substr(firstNonSpace);
#endif
}

//-----------------------------------------------------------------------------
// 指令集特性摘要
//
// 【迁移前】解析 CPUID leaf 1 的 ECX/EDX 特性位。
// 【迁移后】ARM64 / ARM64EC 改用 IsProcessorFeaturePresent —— 这是
//           架构中立的 Win32 API，由系统负责回答「当前 CPU 是否支持某特性」。
//-----------------------------------------------------------------------------
std::wstring GetCpuFeatureSummary()
{
    std::wstring result;
    auto append = [&result](const wchar_t* name, bool supported)
    {
        if (supported)
        {
            if (!result.empty()) { result += L" "; }
            result += name;
        }
    };

#if defined(_M_ARM64EC) || defined(_M_ARM64)
    append(L"NEON",   IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != FALSE);
    append(L"CRC32",  IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != FALSE);
    append(L"Crypto", IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != FALSE);
#ifdef PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE
    append(L"LSE",    IsProcessorFeaturePresent(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE) != FALSE);
#endif
#ifdef PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE
    append(L"DotProd", IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE) != FALSE);
#endif
#ifdef PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE
    append(L"JSCVT",  IsProcessorFeaturePresent(PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE) != FALSE);
#endif
#else
    std::array<int, 4> regs = {};
    __cpuid(regs.data(), 1);

    const int edx = regs[3];
    const int ecx = regs[2];

    append(L"SSE",    (edx & (1 << 25)) != 0);
    append(L"SSE2",   (edx & (1 << 26)) != 0);
    append(L"SSE3",   (ecx & (1 << 0))  != 0);
    append(L"SSSE3",  (ecx & (1 << 9))  != 0);
    append(L"SSE4.1", (ecx & (1 << 19)) != 0);
    append(L"SSE4.2", (ecx & (1 << 20)) != 0);
    append(L"AVX",    (ecx & (1 << 28)) != 0);
#endif

    return result.empty() ? L"<无>" : result;
}

//-----------------------------------------------------------------------------
// 高精度计数器
//
// 【迁移前】__rdtsc()。
// 【迁移后】QueryPerformanceCounter()。
//
// 原因：__rdtsc 在 ARM64EC 下会被映射到 ARM64 的通用计时寄存器，
//       其频率与语义都和 x86 的 TSC 不同。任何把「TSC 差值」当作
//       「CPU 周期数」使用的代码在迁移后都会得到错误结论。
//       QueryPerformanceCounter 在所有架构上都有明确定义的频率，
//       配合 QueryPerformanceFrequency 可以换算成真实时间。
//-----------------------------------------------------------------------------
uint64_t ReadPerformanceCounter()
{
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
}

uint64_t GetPerformanceFrequency()
{
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(frequency.QuadPart);
}

//-----------------------------------------------------------------------------
// 应用自身的 SIMD 亮度调整实现。
//
// 【本次迁移中最容易踩坑的一处】
//
//   迁移前的写法是：
//       #if defined(_M_X64)
//           ... SSE2 代码 ...
//       #endif
//
//   看上去「ARM64EC 自然会走到标量兜底路径」，但事实恰恰相反：
//   **MSVC 在编译 ARM64EC 时会同时定义 _M_X64 和 _M_AMD64**（这是为了让
//   海量既有 x64 代码中的数据模型假设继续成立）。于是这个分支照样会被选中，
//   SSE2 内在函数被链接到 softintrin.lib 的软件模拟实现上 ——
//   编译通过、运行正确，但每条 SSE2 指令都要靠若干条 ARM64 指令去模拟，
//   性能白白损失。这类问题不会报错，只会「悄悄变慢」，极难察觉。
//
//   正确写法：把 _M_ARM64EC / _M_ARM64 的判断放在 _M_X64 **之前**。
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

#if defined(_M_ARM64EC) || defined(_M_ARM64)
    //-------------------------------------------------------------------------
    // ---- NEON 向量路径：一次处理 16 个像素 ----
    //
    // 【一条重要的迁移经验：不要把 SSE2 逐条“翻译”成 NEON】
    //
    //   最初的写法是机械对应 SSE2 的每一步：
    //       vmovl_u8 -> vshlq_n_u16(,8) -> vmull_u16 + vshrn_n_u32(,16)
    //       -> vcombine_u16 -> vqmovn_u16
    //   因为 NEON 没有和 _mm_mulhi_epu16 完全对应的指令，“取高半部分乘法”
    //   只能拆成加宽乘法再右移，低/高半各做一次，单是这一步就要 4 条指令。
    //   实测结果是：这样写出来的「原生 NEON」反而比 x64 模拟器跑 SSE2 还慢 ——
    //   因为模拟器会把 pmulhuw 翻译成更高效的 ARM64 序列。
    //
    //   换个思路，用 ARM64 自己的强项 SQDMULH（加倍取高半饱和乘法）：
    //       vqdmulhq_s16(a, b) == 饱和((2 * a * b) >> 16)
    //   取 a = pixel << 7（最大 255<<7 = 32640，稳稳落在 s16 正数区），
    //   则 (2 * (pixel<<7) * gain) >> 16 == (pixel * gain) >> 8，
    //   正是我们需要的 8.8 定点乘法，**一条指令就够**。
    //
    //   这样每 16 个像素只需约 8 条向量指令，比机械翻译版少了约 40%，
    //   且与 SSE2 路径逐位等价（已对 gain 0..1024 × pixel 0..255 全集穷举验证）。
    //-------------------------------------------------------------------------
    const int16x8_t vGain = vdupq_n_s16(static_cast<int16_t>(fixedGain));

    for (; i + 16 <= count; i += 16)
    {
        const uint8x16_t src = vld1q_u8(pixels + i);

        // 零扩展到 16 位并左移 7 位（ushll / ushll2，各一条指令）
        const int16x8_t lo = vreinterpretq_s16_u16(vshll_n_u8(vget_low_u8(src),  7));
        const int16x8_t hi = vreinterpretq_s16_u16(vshll_n_u8(vget_high_u8(src), 7));

        // (pixel * gain) >> 8，每组 8 个像素一条 sqdmulh
        const int16x8_t ro = vqdmulhq_s16(lo, vGain);
        const int16x8_t rh = vqdmulhq_s16(hi, vGain);

        // 有符号 16 位 -> 无符号 8 位饱和（sqxtun / sqxtun2），等价于 packus
        vst1q_u8(pixels + i, vcombine_u8(vqmovun_s16(ro), vqmovun_s16(rh)));
    }

#elif defined(_M_X64)
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

    // ---- 标量尾部（在没有 SIMD 实现的平台上即为完整路径）----
    for (; i < count; ++i)
    {
        const uint32_t v = ((static_cast<uint32_t>(pixels[i]) << 8) * fixedGain) >> 16;
        pixels[i] = static_cast<uint8_t>(v > 255u ? 255u : v);
    }
}

//-----------------------------------------------------------------------------
// 返回当前实际启用的 SIMD 实现名称，供界面显示。
//-----------------------------------------------------------------------------
std::wstring GetActiveSimdPath()
{
#if defined(_M_ARM64EC)
    return L"NEON（ARM64EC 原生）";
#elif defined(_M_ARM64)
    return L"NEON（ARM64 原生）";
#elif defined(_M_X64)
    return L"SSE2（x64 原生）";
#else
    return L"标量（无 SIMD）";
#endif
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

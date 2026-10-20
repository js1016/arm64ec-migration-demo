//=============================================================================
// FastImage.cpp
//
// “FastImage SDK” 的实现（模拟第三方厂商的闭源源码）。
//
// 本工程在整个示例中**永远保持 x64（AMD64）目标平台不变**：
// 它代表那些你无法修改、厂商也不打算重新编译为 ARM64 的二进制依赖。
// 迁移到 ARM64EC 时，宿主应用会继续链接此处产出的 x64 导入库，
// 并在运行时加载这个 x64 DLL。
//
// 实现中刻意使用了 SSE2 内在函数（intrinsics），以体现“这是一份深度绑定
// x64 指令集的代码”。
//=============================================================================

#include "FastImage.h"

#include <windows.h>
#include <emmintrin.h>  // SSE2 内在函数
#include <new>

//-----------------------------------------------------------------------------
// 编译期守卫：本 DLL 只允许以 x64（且非 ARM64EC）方式编译。
// 如果有人误把它加入 ARM64EC 配置，这里会立刻给出明确的编译错误，
// 避免“第三方库被悄悄重编译”从而让演示失去意义。
//-----------------------------------------------------------------------------
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#error "FastImage 模拟的是仅提供 x64 版本的第三方库，不得以 ARM64/ARM64EC 方式编译。"
#endif
#if !defined(_M_X64)
#error "FastImage 必须以 x64 (AMD64) 平台编译。"
#endif

//-----------------------------------------------------------------------------
// 内部常量
//-----------------------------------------------------------------------------
namespace
{
    // 用于校验句柄合法性的魔数（'FIMG'）
    constexpr uint32_t kContextMagic = 0x464D4947u;

    // 进度回调的粒度：每处理这么多像素上报一次进度
    constexpr size_t kProgressChunk = 64 * 1024;
}

//-----------------------------------------------------------------------------
// 内部上下文结构：对调用方不可见
//-----------------------------------------------------------------------------
struct FI_CONTEXT_
{
    uint32_t             magic;           // 句柄有效性校验
    FI_PROGRESS_CALLBACK callback;        // 宿主注册的进度回调
    void*                userContext;     // 回调透传的用户数据
    uint64_t             processedPixels; // 累计处理像素数
    CRITICAL_SECTION     lock;            // 简单的线程保护
};

namespace
{
    // 判断句柄是否由本 SDK 创建且尚未销毁。
    inline bool IsValidContext(FI_HANDLE handle)
    {
        return handle != nullptr && handle->magic == kContextMagic;
    }

    //-------------------------------------------------------------------------
    // SSE2 亮度调整核心循环。
    //
    // 算法：
    //   1. 把 8 位像素零扩展到 16 位通道；
    //   2. 左移 8 位得到 pixel * 256；
    //   3. 与 8.8 定点增益（gain * 256）做无符号高位乘法 _mm_mulhi_epu16，
    //      即 (pixel * 256 * gain * 256) >> 16 == pixel * gain；
    //   4. 用 _mm_packus_epi16 做饱和打包回 8 位（自动钳制到 0..255）。
    //
    // 一次处理 16 个像素。
    //-------------------------------------------------------------------------
    void AdjustBrightnessSSE2(uint8_t* pixels, size_t count, uint16_t fixedGain)
    {
        const __m128i vGain = _mm_set1_epi16(static_cast<short>(fixedGain));
        const __m128i vZero = _mm_setzero_si128();

        size_t i = 0;
        for (; i + 16 <= count; i += 16)
        {
            __m128i src = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels + i));

            // 拆分成低 8 像素与高 8 像素，分别零扩展为 16 位
            __m128i lo = _mm_unpacklo_epi8(src, vZero);
            __m128i hi = _mm_unpackhi_epi8(src, vZero);

            // pixel * 256，再做 8.8 定点无符号高位乘法
            lo = _mm_mulhi_epu16(_mm_slli_epi16(lo, 8), vGain);
            hi = _mm_mulhi_epu16(_mm_slli_epi16(hi, 8), vGain);

            // 饱和打包回 8 位
            _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels + i),
                             _mm_packus_epi16(lo, hi));
        }

        // 处理尾部不足 16 个的像素，使用与 SIMD 路径完全一致的定点运算，
        // 保证结果逐字节一致。
        for (; i < count; ++i)
        {
            const uint32_t v = ((static_cast<uint32_t>(pixels[i]) << 8) * fixedGain) >> 16;
            pixels[i] = static_cast<uint8_t>(v > 255u ? 255u : v);
        }
    }
}

//=============================================================================
// 导出函数实现
//=============================================================================

extern "C" FI_API void FI_CALL FI_GetVersion(FI_VERSION* outVersion)
{
    if (outVersion == nullptr)
    {
        return;
    }

    outVersion->major = 3;
    outVersion->minor = 2;
    outVersion->patch = 1;

    // 这里的架构字符串在编译期就被固化为 "x64 (AMD64)"。
    // 迁移到 ARM64EC 之后宿主读到的仍然是这个值 —— 因为 DLL 根本没有重编译。
    wcscpy_s(outVersion->buildArch, L"x64 (AMD64)");

    // __DATE__ 是窄字符串，转换为宽字符后输出
    size_t converted = 0;
    mbstowcs_s(&converted,
               outVersion->buildDate,
               _countof(outVersion->buildDate),
               __DATE__,
               _TRUNCATE);
}

extern "C" FI_API const wchar_t* FI_CALL FI_GetModuleArchitecture(void)
{
    return L"x64 (AMD64)";
}

extern "C" FI_API FI_RESULT FI_CALL FI_Create(FI_HANDLE* outHandle)
{
    if (outHandle == nullptr)
    {
        return FI_E_INVALIDARG;
    }
    *outHandle = nullptr;

    FI_CONTEXT_* ctx = new (std::nothrow) FI_CONTEXT_();
    if (ctx == nullptr)
    {
        return FI_E_OUTOFMEMORY;
    }

    ctx->magic           = kContextMagic;
    ctx->callback        = nullptr;
    ctx->userContext     = nullptr;
    ctx->processedPixels = 0;
    InitializeCriticalSection(&ctx->lock);

    *outHandle = ctx;
    return FI_OK;
}

extern "C" FI_API void FI_CALL FI_Destroy(FI_HANDLE handle)
{
    if (!IsValidContext(handle))
    {
        return;
    }

    handle->magic = 0;  // 防止重复释放
    DeleteCriticalSection(&handle->lock);
    delete handle;
}

extern "C" FI_API FI_RESULT FI_CALL FI_SetProgressCallback(FI_HANDLE            handle,
                                                           FI_PROGRESS_CALLBACK callback,
                                                           void*                userContext)
{
    if (!IsValidContext(handle))
    {
        return FI_E_INVALIDHANDLE;
    }

    EnterCriticalSection(&handle->lock);
    handle->callback    = callback;
    handle->userContext = userContext;
    LeaveCriticalSection(&handle->lock);

    return FI_OK;
}

extern "C" FI_API FI_RESULT FI_CALL FI_AdjustBrightness(FI_HANDLE handle,
                                                        uint8_t*  pixels,
                                                        size_t    pixelCount,
                                                        float     gain)
{
    if (!IsValidContext(handle))
    {
        return FI_E_INVALIDHANDLE;
    }
    if (pixels == nullptr || pixelCount == 0)
    {
        return FI_E_INVALIDARG;
    }

    // 钳制增益范围，避免定点换算溢出
    if (gain < 0.0f) { gain = 0.0f; }
    if (gain > 4.0f) { gain = 4.0f; }

    const uint16_t fixedGain = static_cast<uint16_t>(gain * 256.0f + 0.5f);

    EnterCriticalSection(&handle->lock);
    FI_PROGRESS_CALLBACK callback    = handle->callback;
    void*                userContext = handle->userContext;
    LeaveCriticalSection(&handle->lock);

    // 分块处理，以便周期性地回调宿主报告进度。
    // 注意：回调是在 DLL（x64 代码）内部发起、跨回宿主程序的调用。
    size_t offset = 0;
    while (offset < pixelCount)
    {
        const size_t remaining = pixelCount - offset;
        const size_t chunk     = remaining < kProgressChunk ? remaining : kProgressChunk;

        AdjustBrightnessSSE2(pixels + offset, chunk, fixedGain);
        offset += chunk;

        if (callback != nullptr)
        {
            const uint32_t percent =
                static_cast<uint32_t>((static_cast<uint64_t>(offset) * 100ull) / pixelCount);
            callback(percent, userContext);
        }
    }

    EnterCriticalSection(&handle->lock);
    handle->processedPixels += pixelCount;
    LeaveCriticalSection(&handle->lock);

    return FI_OK;
}

extern "C" FI_API uint32_t FI_CALL FI_ComputeChecksum(const void* data, size_t sizeInBytes)
{
    if (data == nullptr || sizeInBytes == 0)
    {
        return 0;
    }

    // FNV-1a 32 位，纯整数运算，跨架构结果一致
    const uint8_t* p    = static_cast<const uint8_t*>(data);
    uint32_t       hash = 2166136261u;

    for (size_t i = 0; i < sizeInBytes; ++i)
    {
        hash ^= p[i];
        hash *= 16777619u;
    }

    return hash;
}

extern "C" FI_API uint64_t FI_CALL FI_GetProcessedPixelCount(FI_HANDLE handle)
{
    if (!IsValidContext(handle))
    {
        return 0;
    }

    EnterCriticalSection(&handle->lock);
    const uint64_t value = handle->processedPixels;
    LeaveCriticalSection(&handle->lock);

    return value;
}

//-----------------------------------------------------------------------------
// DLL 入口点
//-----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE /*module*/, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

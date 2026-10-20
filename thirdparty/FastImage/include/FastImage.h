//=============================================================================
// FastImage.h
//
// “FastImage SDK” 公共头文件（模拟第三方厂商提供的二进制组件）。
//
// 重要背景说明（与 ARM64EC 迁移直接相关）：
//   本 SDK 在本示例中扮演“第三方闭源库”的角色。假设厂商只提供 x64（AMD64）
//   版本的 FastImage.dll / FastImage.lib，并且**没有**发布 ARM64 原生版本的
//   计划。宿主应用后续迁移到 ARM64EC 之后，仍然必须能够直接链接并调用这个
//   x64 DLL —— 这正是 ARM64EC（Emulation Compatible）ABI 的核心价值：
//   同一个进程内允许 ARM64EC 原生代码与 x64 模拟代码混合执行。
//
// 该头文件是纯 C 风格接口（extern "C" + 普通数据类型），这是第三方二进制
// 组件的常见做法，也最有利于跨编译器 / 跨架构互操作。
//=============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// 导入 / 导出宏
//   构建 DLL 时由工程定义 FASTIMAGE_EXPORTS；宿主应用不定义该宏，
//   因此会解析为 __declspec(dllimport)，从导入库 FastImage.lib 解析符号。
//-----------------------------------------------------------------------------
#ifdef FASTIMAGE_EXPORTS
#define FI_API __declspec(dllexport)
#else
#define FI_API __declspec(dllimport)
#endif

//-----------------------------------------------------------------------------
// 调用约定
//   x64 与 ARM64EC 都只有一种标准调用约定，__cdecl 在这两种架构上都会被
//   忽略，但显式写出来可以让接口声明的意图更清晰。
//-----------------------------------------------------------------------------
#define FI_CALL __cdecl

//-----------------------------------------------------------------------------
// 返回码
//-----------------------------------------------------------------------------
typedef enum FI_RESULT
{
    FI_OK                = 0,   // 成功
    FI_E_INVALIDARG      = -1,  // 参数非法
    FI_E_OUTOFMEMORY     = -2,  // 内存不足
    FI_E_INVALIDHANDLE   = -3   // 句柄无效（已销毁或非本 SDK 创建）
} FI_RESULT;

//-----------------------------------------------------------------------------
// 不透明句柄：隐藏厂商内部数据结构
//-----------------------------------------------------------------------------
typedef struct FI_CONTEXT_* FI_HANDLE;

//-----------------------------------------------------------------------------
// 版本信息
//-----------------------------------------------------------------------------
typedef struct FI_VERSION
{
    uint32_t major;             // 主版本号
    uint32_t minor;             // 次版本号
    uint32_t patch;             // 修订号
    wchar_t  buildArch[32];     // DLL 自身的编译目标架构（例如 L"x64 (AMD64)"）
    wchar_t  buildDate[32];     // DLL 的编译日期
} FI_VERSION;

//-----------------------------------------------------------------------------
// 进度回调原型
//
// 迁移观察点：
//   这是一个**由 DLL 回调宿主程序**的函数指针。当宿主迁移到 ARM64EC 之后，
//   该回调函数本身是 ARM64EC 原生代码，而调用方（FastImage.dll）是 x64
//   模拟代码。跨越这条边界时，Windows 会自动插入“entry thunk”完成寄存器 /
//   栈帧的转换。开发者无需手写任何胶水代码，但必须理解这里存在性能开销。
//-----------------------------------------------------------------------------
typedef void (FI_CALL* FI_PROGRESS_CALLBACK)(uint32_t percent, void* userContext);

//-----------------------------------------------------------------------------
// API
//-----------------------------------------------------------------------------

// 获取 SDK 版本与编译架构信息。
FI_API void FI_CALL FI_GetVersion(FI_VERSION* outVersion);

// 返回 DLL 自身编译时的目标架构字符串（常量，调用方不得释放）。
// 迁移后在 ARM64EC 进程中调用，它依然会返回 "x64 (AMD64)"，
// 这就直观证明了 x64 DLL 被原样加载并执行。
FI_API const wchar_t* FI_CALL FI_GetModuleArchitecture(void);

// 创建 / 销毁处理上下文。
FI_API FI_RESULT FI_CALL FI_Create(FI_HANDLE* outHandle);
FI_API void      FI_CALL FI_Destroy(FI_HANDLE handle);

// 注册进度回调（传 NULL 可取消注册）。
FI_API FI_RESULT FI_CALL FI_SetProgressCallback(FI_HANDLE            handle,
                                                FI_PROGRESS_CALLBACK callback,
                                                void*                userContext);

// 对 8 位灰度像素缓冲区做亮度调整（就地修改）。
//   pixels     : 像素缓冲区首地址
//   pixelCount : 像素个数（字节数）
//   gain       : 亮度增益，有效范围 [0.0, 4.0]，超出会被内部钳制
// 内部使用 SSE2 指令集加速 —— 这是典型的“x64 专有实现”，
// 也是厂商不愿意移植到 ARM64 的原因之一。
FI_API FI_RESULT FI_CALL FI_AdjustBrightness(FI_HANDLE handle,
                                             uint8_t*  pixels,
                                             size_t    pixelCount,
                                             float     gain);

// 计算缓冲区的 FNV-1a 32 位校验和。
// 结果与架构无关，便于迁移前后比对“同样的输入是否得到同样的输出”。
FI_API uint32_t FI_CALL FI_ComputeChecksum(const void* data, size_t sizeInBytes);

// 返回本上下文累计处理过的像素总数，用于验证跨 ABI 调用的状态保持正确。
FI_API uint64_t FI_CALL FI_GetProcessedPixelCount(FI_HANDLE handle);

#ifdef __cplusplus
} // extern "C"
#endif

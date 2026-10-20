//=============================================================================
// SysInfo.h
//
// 运行环境与模块架构探测。
//
// 这是整个演示的“观测窗口”：迁移到 ARM64EC 之后，同一份界面会显示出
//   - 宿主进程   : ARM64EC（混合 PE，PE 头仍标记为 AMD64）
//   - FastImage  : 纯 x64（AMD64），由系统模拟器执行
// 从而直观证明两种代码在同一进程中共存。
//=============================================================================

#pragma once

#include <windows.h>
#include <string>

namespace SysInfo
{
    //-------------------------------------------------------------------------
    // 返回“编译期”目标架构名称。
    //
    // 关键迁移陷阱：
    //   在 ARM64EC 下，编译器**同时**定义了 _M_X64 / _M_AMD64 和 _M_ARM64EC。
    //   这是为了让大量既有的 x64 代码（数据类型假设、结构体布局等）能够直接
    //   复用。但也意味着所有形如 `#if defined(_M_X64)` 的分支在 ARM64EC 下
    //   依然成立。因此判断顺序必须先检查 _M_ARM64EC，否则会误判为 x64。
    //-------------------------------------------------------------------------
    std::wstring GetCompileTimeArchitecture();

    // 返回本机（物理 CPU）架构名称，例如 L"x64 (AMD64)" / L"ARM64"。
    std::wstring GetNativeMachineName();

    // 返回当前进程的“模拟架构”名称。
    // 在 x64 机器上运行 x64 程序时为 L"x64 (AMD64) 原生"；
    // 在 ARM64 机器上运行 x64 程序时会显示为“x64 模拟”。
    std::wstring GetProcessMachineName();

    // 把 PE 的 IMAGE_FILE_MACHINE_* 常量转为可读名称。
    std::wstring MachineTypeToString(USHORT machine);

    // 本机 CPU 是否为 ARM64。用于判断某个 x64 模块究竟是原生执行还是模拟执行。
    bool IsNativeArm64();

    //-------------------------------------------------------------------------
    // 读取已加载模块 PE 头中的 Machine 字段。
    //
    // 注意：ARM64EC 可执行文件的 Machine 字段**依然是 IMAGE_FILE_MACHINE_AMD64
    // (0x8664)**，这样现有的 x64 加载器与工具链才能接受它。真正区分 ARM64EC
    // 的办法是检查加载配置目录里的 CHPE 元数据指针（见 IsHybridModule）。
    //-------------------------------------------------------------------------
    USHORT GetModuleMachineType(HMODULE module);

    // 判断模块是否为混合（CHPE / ARM64EC / ARM64X）映像。
    bool IsHybridModule(HMODULE module);

    // 返回对某个已加载模块的一行描述，形如：
    //   "DemoApp.exe            PE Machine = AMD64        类型 = 纯 x64 原生"
    std::wstring DescribeModule(HMODULE module);

    // 返回模块的完整文件路径。
    std::wstring GetModulePath(HMODULE module);

    //-------------------------------------------------------------------------
    // 函数级别的代码类型判定（ARM64EC 专有能力）
    //
    // ARM64EC 进程里，模块粒度的信息已经不够用了：同一个进程内既有 ARM64EC
    // 原生代码，也有 x64 模拟代码，甚至同一个 DLL 里都可能两者并存（ARM64X）。
    // Windows 11 的 ntdll 导出了 RtlIsEcCode，可以判断**任意一个代码地址**
    // 属于哪一类。
    //
    // 本程序用它来直观展示：
    //   * 自己的回调函数      -> ARM64EC 原生代码
    //   * 第三方 DLL 的导出函数 -> x64 模拟代码
    // 两者位于同一进程，由系统自动生成的 thunk 完成相互调用。
    //-------------------------------------------------------------------------
    // 注意：x64 版 Windows 的 ntdll 同样导出 RtlIsEcCode，只不过它对任何地址
    // 都返回 FALSE（x64 机器上根本不存在 EC 代码）。因此 Unsupported 只会在
    // 极老的系统上出现，真正区分两类代码的对比要在 ARM64 设备上才看得到。
    enum class CodeKind
    {
        Unsupported,            // 系统未导出 RtlIsEcCode，无法判定
        EmulationCompatible,    // ARM64EC 代码，由 CPU 直接执行
        NotEmulationCompatible  // 非 EC 代码；在 ARM64 上即为需要模拟的 x64 代码
    };

    // 判定某个代码地址的类型。
    CodeKind ClassifyCodeAddress(const void* address);

    // 生成一行可读描述，形如：
    //   "  本程序的进度回调            0x00007FF6... ARM64EC 原生代码"
    std::wstring DescribeCodeAddress(const wchar_t* label, const void* address);
}

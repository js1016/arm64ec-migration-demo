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
}

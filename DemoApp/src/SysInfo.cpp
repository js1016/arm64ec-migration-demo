//=============================================================================
// SysInfo.cpp —— 运行环境与模块架构探测的实现
//=============================================================================

#include "SysInfo.h"
#include "TextUtil.h"

#include <winnt.h>
#include <vector>

namespace
{
    //-------------------------------------------------------------------------
    // IMAGE_LOAD_CONFIG_DIRECTORY64 中 CHPEMetadataPointer 字段的偏移。
    //
    // 这里刻意不直接引用 SDK 结构体字段，因为该字段在不同版本的 Windows SDK
    // 里改过名字（HybridMetadataPointer -> CHPEMetadataPointer）。
    // 使用文档化的固定偏移可以让代码在各版本 SDK 上都能编译。
    //-------------------------------------------------------------------------
    constexpr DWORD kOffsetOfCHPEMetadataPointer = 0xC8;

    // 安全地取得模块的 NT 头指针；失败返回 nullptr。
    const IMAGE_NT_HEADERS64* GetNtHeaders(HMODULE module)
    {
        if (module == nullptr)
        {
            return nullptr;
        }

        const auto* base = reinterpret_cast<const BYTE*>(module);
        const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return nullptr;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return nullptr;
        }

        return nt;
    }
}

namespace SysInfo
{

std::wstring GetCompileTimeArchitecture()
{
    //-------------------------------------------------------------------------
    // 判断顺序非常重要！_M_ARM64EC 必须放在 _M_X64 之前。
    // 因为 MSVC 在编译 ARM64EC 目标时会同时定义 _M_X64 / _M_AMD64。
    //-------------------------------------------------------------------------
#if defined(_M_ARM64EC)
    return L"ARM64EC（同时定义了 _M_X64 与 _M_ARM64EC）";
#elif defined(_M_ARM64)
    return L"ARM64（原生）";
#elif defined(_M_X64)
    return L"x64 / AMD64（_M_X64）";
#elif defined(_M_IX86)
    return L"x86（32 位）";
#else
    return L"未知架构";
#endif
}

std::wstring MachineTypeToString(USHORT machine)
{
    switch (machine)
    {
    case IMAGE_FILE_MACHINE_I386:   return L"x86 (I386)";
    case IMAGE_FILE_MACHINE_AMD64:  return L"x64 (AMD64)";
    case IMAGE_FILE_MACHINE_ARM64:  return L"ARM64";
    case IMAGE_FILE_MACHINE_ARMNT:  return L"ARM32 (ARMNT)";
    case IMAGE_FILE_MACHINE_UNKNOWN:return L"未知/中性";
    default:
    {
        wchar_t buffer[64] = {};
        swprintf_s(buffer, L"0x%04X", machine);
        return buffer;
    }
    }
}

std::wstring GetNativeMachineName()
{
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine  = IMAGE_FILE_MACHINE_UNKNOWN;

    // IsWow64Process2 从 Windows 10 1709 起提供，是判断“本机架构”最准确的 API。
    // 相比 GetNativeSystemInfo，它能正确区分 ARM64 上的 x64 模拟。
    if (IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
    {
        return MachineTypeToString(nativeMachine);
    }

    // 退化方案
    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64: return L"x64 (AMD64)";
    case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
    case PROCESSOR_ARCHITECTURE_INTEL: return L"x86 (I386)";
    default:                           return L"未知";
    }
}

std::wstring GetProcessMachineName()
{
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine  = IMAGE_FILE_MACHINE_UNKNOWN;

    if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
    {
        return L"未知";
    }

    // processMachine == IMAGE_FILE_MACHINE_UNKNOWN 表示“进程不是 WOW64 进程”，
    // 也就是说它以本机架构直接运行。
    if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN)
    {
        return MachineTypeToString(nativeMachine) + L"（原生执行）";
    }

    return MachineTypeToString(processMachine) + L"（在 " +
           MachineTypeToString(nativeMachine) + L" 上模拟执行）";
}

bool IsNativeArm64()
{
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine  = IMAGE_FILE_MACHINE_UNKNOWN;

    if (IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
    {
        return nativeMachine == IMAGE_FILE_MACHINE_ARM64;
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
}

USHORT GetModuleMachineType(HMODULE module)
{
    const IMAGE_NT_HEADERS64* nt = GetNtHeaders(module);
    return nt != nullptr ? nt->FileHeader.Machine
                         : static_cast<USHORT>(IMAGE_FILE_MACHINE_UNKNOWN);
}

bool IsHybridModule(HMODULE module)
{
    const IMAGE_NT_HEADERS64* nt = GetNtHeaders(module);
    if (nt == nullptr)
    {
        return false;
    }

    // 取出“加载配置目录”（Load Config Directory）
    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];

    if (dir.VirtualAddress == 0 || dir.Size == 0)
    {
        return false;
    }

    const auto*  base      = reinterpret_cast<const BYTE*>(module);
    const BYTE*  config    = base + dir.VirtualAddress;
    const DWORD  configSize = *reinterpret_cast<const DWORD*>(config); // 首字段是 Size

    // 结构体必须足够大，才包含 CHPEMetadataPointer 字段
    if (configSize < kOffsetOfCHPEMetadataPointer + sizeof(ULONGLONG))
    {
        return false;
    }

    const ULONGLONG chpeMetadata =
        *reinterpret_cast<const ULONGLONG*>(config + kOffsetOfCHPEMetadataPointer);

    // 非零即表示这是一个带 CHPE 元数据的混合映像（ARM64EC / ARM64X）
    return chpeMetadata != 0;
}

CodeKind ClassifyCodeAddress(const void* address)
{
    //-------------------------------------------------------------------------
    // RtlIsEcCode 只存在于 ARM64 版 Windows 11 的 ntdll 中，因此必须动态解析，
    // 否则程序在 x64 Windows 上会因为找不到导入符号而无法启动。
    //-------------------------------------------------------------------------
    using PFN_RtlIsEcCode = BOOLEAN(WINAPI*)(ULONG64);

    static const PFN_RtlIsEcCode isEcCode = []() -> PFN_RtlIsEcCode
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return nullptr;
        }
        // 先转成 void* 再转函数指针，避免 C4191 警告
        return reinterpret_cast<PFN_RtlIsEcCode>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlIsEcCode")));
    }();

    if (isEcCode == nullptr || address == nullptr)
    {
        return CodeKind::Unsupported;
    }

    return isEcCode(reinterpret_cast<ULONG64>(address)) != FALSE
         ? CodeKind::EmulationCompatible
         : CodeKind::NotEmulationCompatible;
}

std::wstring DescribeCodeAddress(const wchar_t* label, const void* address)
{
    const CodeKind kind = ClassifyCodeAddress(address);

    // “非 EC 代码”意味着什么，取决于本机 CPU：
    //   在 ARM64 上 —— 它是 x64 代码，必须由模拟器执行；
    //   在 x64  上 —— 它就是普通的 x64 原生代码，没有任何模拟发生。
    const bool nativeIsArm64 = IsNativeArm64();

    const wchar_t* kindText = nullptr;
    switch (kind)
    {
    case CodeKind::EmulationCompatible:
        kindText = L"ARM64EC 代码（ARM64 指令，CPU 直接执行）";
        break;
    case CodeKind::NotEmulationCompatible:
        kindText = nativeIsArm64 ? L"x64 代码（由模拟器执行）"
                                 : L"x64 代码（本机原生执行）";
        break;
    default:
        kindText = L"无法判定（系统未导出 ntdll!RtlIsEcCode）";
        break;
    }

    wchar_t line[512] = {};
    swprintf_s(line, L"  %s 0x%016llX  %s",
               TextUtil::PadRight(label, 30).c_str(),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)),
               kindText);

    return line;
}

std::wstring GetModulePath(HMODULE module)
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
        {
            return L"<未知>";
        }
        if (length < buffer.size() - 1)
        {
            return std::wstring(buffer.data(), length);
        }

        buffer.resize(buffer.size() * 2); // 路径被截断，扩容后重试
    }
}

std::wstring DescribeModule(HMODULE module)
{
    if (module == nullptr)
    {
        return L"<模块未加载>";
    }

    const std::wstring path    = GetModulePath(module);
    const size_t       slash   = path.find_last_of(L'\\');
    const std::wstring name    = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    const USHORT       machine = GetModuleMachineType(module);
    const bool         hybrid  = IsHybridModule(module);

    // 实际执行方式取决于“模块 PE 架构”与“本机 CPU 架构”的组合，
    // 因此这里必须结合运行期探测结果来描述，不能写死。
    const bool nativeIsArm64 = IsNativeArm64();

    std::wstring kind;
    if (machine == IMAGE_FILE_MACHINE_AMD64)
    {
        if (hybrid)
        {
            // ARM64EC / ARM64X：PE 头声明为 AMD64，但含 CHPE 元数据
            kind = L"混合映像 ARM64EC/ARM64X（含 CHPE 元数据，以 ARM64 原生指令执行）";
        }
        else
        {
            kind = nativeIsArm64 ? L"纯 x64 映像（由 x64 模拟器执行）"
                                 : L"纯 x64 映像（原生执行）";
        }
    }
    else if (machine == IMAGE_FILE_MACHINE_ARM64)
    {
        kind = L"纯 ARM64 原生映像";
    }
    else
    {
        kind = L"其他架构";
    }

    wchar_t line[512] = {};
    swprintf_s(line,
               L"  %s PE Machine = %s %s",
               TextUtil::PadRight(name, 24).c_str(),
               TextUtil::PadRight(MachineTypeToString(machine), 13).c_str(),
               kind.c_str());

    return line;
}

} // namespace SysInfo

//=============================================================================
// MainWindow.cpp —— 主窗口实现
//
// 本文件演示一个典型的、为 64 位 Windows 开发的传统 Win32 GUI 应用：
//   * 手工注册窗口类、创建窗口、跑消息循环；
//   * 用子控件（Button / Edit / Static）搭建界面；
//   * 静态链接第三方 x64 SDK 的导入库，并直接调用其导出函数；
//   * 向第三方 SDK 注册回调函数。
//=============================================================================

#include "MainWindow.h"
#include "SysInfo.h"
#include "NativeCompute.h"
#include "TextUtil.h"
#include "../res/Resource.h"

#include "FastImage.h"          // 第三方 SDK 头文件（通过附加包含目录找到）

#include <commctrl.h>
#include <strsafe.h>
#include <vector>
#include <array>

#pragma comment(lib, "comctl32.lib")

namespace
{
    // 窗口类名
    constexpr wchar_t kWindowClassName[] = L"Arm64EcDemo.MainWindow";

    // 逻辑像素下的界面度量（实际使用时会按 DPI 缩放）
    constexpr int kMargin       = 12;
    constexpr int kButtonWidth  = 168;
    constexpr int kButtonHeight = 34;
    constexpr int kButtonGap    = 8;
    constexpr int kButtonCols   = 3;
    constexpr int kButtonRows   = 2;
    constexpr int kStatusHeight = 22;

    // 测试缓冲区大小：4 MiB 像素
    constexpr size_t kTestPixelCount = 4u * 1024u * 1024u;

    // 固定随机种子，保证迁移前后输入数据完全一致
    constexpr uint32_t kTestSeed = 0x5EEDC0DEu;

    // 亮度增益
    constexpr float kTestGain = 1.35f;

    // 按钮定义表（顺序即界面上的排列顺序）
    struct ButtonDef
    {
        int           id;
        const wchar_t* label;
    };

    const ButtonDef kButtons[] =
    {
        { IDM_ACTION_ENVIRONMENT, L"1. 检测运行环境"   },
        { IDM_ACTION_MODULES,     L"2. 查看模块架构"   },
        { IDM_ACTION_SDKCALL,     L"3. 调用第三方 SDK" },
        { IDM_ACTION_COMPARE,     L"4. 结果一致性对比" },
        { IDM_ACTION_BENCHMARK,   L"5. 性能基准测试"   },
        { IDM_ACTION_CLEARLOG,    L"6. 清空日志"       },
    };

    constexpr wchar_t kSeparator[] =
        L"--------------------------------------------------------------------------------";

    // 把 FI_RESULT 转成可读文本
    const wchar_t* FiResultToString(FI_RESULT result)
    {
        switch (result)
        {
        case FI_OK:              return L"FI_OK";
        case FI_E_INVALIDARG:    return L"FI_E_INVALIDARG";
        case FI_E_OUTOFMEMORY:   return L"FI_E_OUTOFMEMORY";
        case FI_E_INVALIDHANDLE: return L"FI_E_INVALIDHANDLE";
        default:                 return L"<未知返回码>";
        }
    }
}

//=============================================================================
// 窗口类注册与创建
//=============================================================================

bool MainWindow::RegisterWindowClass(HINSTANCE instance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWindow::WndProcStatic;
    wc.cbWndExtra    = sizeof(MainWindow*);   // 预留空间保存 this 指针
    wc.hInstance     = instance;
    wc.hIcon         = LoadIconW(instance, MAKEINTRESOURCEW(IDI_DEMOAPP));
    wc.hIconSm       = wc.hIcon;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = kWindowClassName;

    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create(HINSTANCE instance, int showCommand)
{
    m_instance = instance;

    // 标题里直接写出编译架构，迁移前后对比一目了然
    const std::wstring title =
        L"ARM64EC 迁移示例 —— 当前构建：" + SysInfo::GetCompileTimeArchitecture();

    m_hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1040, 680,
        nullptr, nullptr, instance,
        this);                      // 通过 CREATESTRUCT 把 this 传给 WM_NCCREATE

    if (m_hwnd == nullptr)
    {
        return false;
    }

    ShowWindow(m_hwnd, showCommand);
    UpdateWindow(m_hwnd);
    return true;
}

//=============================================================================
// 消息分发
//=============================================================================

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;

    if (message == WM_NCCREATE)
    {
        // 创建期把 this 指针存进窗口的额外数据区
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, 0));
    }

    if (self != nullptr)
    {
        return self->WndProc(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::WndProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        return OnCreate() ? 0 : -1;

    case WM_SIZE:
        OnSize();
        return 0;

    case WM_GETMINMAXINFO:
    {
        // 限制最小尺寸，避免控件被挤压
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(640);
        mmi->ptMinTrackSize.y = Scale(460);
        return 0;
    }

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        // 让只读的日志编辑框与状态文本使用窗口背景色
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    case WM_DESTROY:
        DestroyFonts();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, message, wParam, lParam);
}

//=============================================================================
// 初始化
//=============================================================================

bool MainWindow::OnCreate()
{
    m_dpi = GetDpiForWindow(m_hwnd);
    CreateFonts(m_dpi);

    // 创建 6 个功能按钮
    for (const ButtonDef& def : kButtons)
    {
        HWND button = CreateWindowExW(
            0, L"BUTTON", def.label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0,
            m_hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(def.id)),
            m_instance, nullptr);

        if (button == nullptr)
        {
            return false;
        }
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    }

    // 日志输出框：多行、只读、带垂直滚动条
    m_log = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
        0, 0, 0, 0,
        m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_EDIT)),
        m_instance, nullptr);

    if (m_log == nullptr)
    {
        return false;
    }

    SendMessageW(m_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_logFont), TRUE);
    // 解除编辑框默认的文本长度限制，避免日志被截断
    SendMessageW(m_log, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

    // 底部状态文本
    m_status = CreateWindowExW(
        0, L"STATIC", L"就绪",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS,
        0, 0, 0, 0,
        m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_TEXT)),
        m_instance, nullptr);

    if (m_status == nullptr)
    {
        return false;
    }
    SendMessageW(m_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);

    // 启动时自动打印一次环境信息
    LogLine(L"=== ARM64EC 迁移示例应用 ===");
    LogLine(L"本程序当前是一个为 64 位 Windows 构建的传统 Win32 GUI 应用，");
    LogLine(L"并静态链接了第三方 x64 组件 FastImage SDK。");
    LogLine(L"");
    ActionShowEnvironment();

    return true;
}

void MainWindow::CreateFonts(UINT dpi)
{
    DestroyFonts();

    // 9pt 界面字体
    const int uiHeight = -MulDiv(9, static_cast<int>(dpi), 72);
    m_uiFont = CreateFontW(uiHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Segoe UI");

    // 10pt 等宽日志字体
    const int logHeight = -MulDiv(10, static_cast<int>(dpi), 72);
    m_logFont = CreateFontW(logHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                            L"Consolas");
}

void MainWindow::DestroyFonts()
{
    if (m_uiFont != nullptr)
    {
        DeleteObject(m_uiFont);
        m_uiFont = nullptr;
    }
    if (m_logFont != nullptr)
    {
        DeleteObject(m_logFont);
        m_logFont = nullptr;
    }
}

int MainWindow::Scale(int value) const
{
    return MulDiv(value, static_cast<int>(m_dpi), 96);
}

//=============================================================================
// 布局
//=============================================================================

void MainWindow::OnSize()
{
    RECT client = {};
    GetClientRect(m_hwnd, &client);

    const int margin    = Scale(kMargin);
    const int gap       = Scale(kButtonGap);
    const int btnHeight = Scale(kButtonHeight);
    const int statusH   = Scale(kStatusHeight);
    const int width     = client.right - client.left;
    const int height    = client.bottom - client.top;

    // 按钮宽度随窗口自适应，但不小于设计宽度的 60%
    int btnWidth = (width - 2 * margin - (kButtonCols - 1) * gap) / kButtonCols;
    const int minBtnWidth = Scale(kButtonWidth) * 6 / 10;
    if (btnWidth < minBtnWidth)
    {
        btnWidth = minBtnWidth;
    }

    // 逐个摆放按钮（2 行 3 列）
    for (int i = 0; i < static_cast<int>(_countof(kButtons)); ++i)
    {
        const int row = i / kButtonCols;
        const int col = i % kButtonCols;

        HWND button = GetDlgItem(m_hwnd, kButtons[i].id);
        if (button != nullptr)
        {
            SetWindowPos(button, nullptr,
                         margin + col * (btnWidth + gap),
                         margin + row * (btnHeight + gap),
                         btnWidth, btnHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    const int logTop = margin + kButtonRows * btnHeight + (kButtonRows - 1) * gap + gap;
    const int logHeight = height - logTop - statusH - margin - gap;

    if (m_log != nullptr && logHeight > 0)
    {
        SetWindowPos(m_log, nullptr,
                     margin, logTop,
                     width - 2 * margin, logHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_status != nullptr)
    {
        SetWindowPos(m_status, nullptr,
                     margin, height - statusH - margin / 2,
                     width - 2 * margin, statusH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::OnDpiChanged(UINT newDpi, const RECT* suggested)
{
    m_dpi = newDpi;
    CreateFonts(m_dpi);

    // 字体换了之后要重新下发给所有子控件
    for (const ButtonDef& def : kButtons)
    {
        HWND button = GetDlgItem(m_hwnd, def.id);
        if (button != nullptr)
        {
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
        }
    }
    if (m_log != nullptr)
    {
        SendMessageW(m_log, WM_SETFONT, reinterpret_cast<WPARAM>(m_logFont), TRUE);
    }
    if (m_status != nullptr)
    {
        SendMessageW(m_status, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    }

    if (suggested != nullptr)
    {
        SetWindowPos(m_hwnd, nullptr,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    OnSize();
}

//=============================================================================
// 命令处理
//=============================================================================

void MainWindow::OnCommand(int commandId)
{
    switch (commandId)
    {
    case IDM_ACTION_ENVIRONMENT: ActionShowEnvironment();   break;
    case IDM_ACTION_MODULES:     ActionShowModules();       break;
    case IDM_ACTION_SDKCALL:     ActionCallThirdPartySdk(); break;
    case IDM_ACTION_COMPARE:     ActionCompareResults();    break;
    case IDM_ACTION_BENCHMARK:   ActionRunBenchmark();      break;
    case IDM_ACTION_CLEARLOG:    ActionClearLog();          break;

    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;

    case IDM_HELP_ABOUT:
        DialogBoxParamW(m_instance, MAKEINTRESOURCEW(IDD_ABOUTBOX), m_hwnd,
                        &MainWindow::AboutDlgProc, 0);
        break;

    default:
        break;
    }
}

//=============================================================================
// 日志输出
//=============================================================================

void MainWindow::LogLine(const std::wstring& text)
{
    if (m_log == nullptr)
    {
        return;
    }

    // Edit 控件要求使用 CRLF 换行
    std::wstring line = text;
    line += L"\r\n";

    const int length = GetWindowTextLengthW(m_log);
    SendMessageW(m_log, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(m_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(m_log, EM_SCROLLCARET, 0, 0);
}

void MainWindow::LogFormat(const wchar_t* format, ...)
{
    wchar_t buffer[2048] = {};

    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, _countof(buffer), format, args);
    va_end(args);

    LogLine(buffer);
}

void MainWindow::SetStatus(const std::wstring& text)
{
    if (m_status != nullptr)
    {
        SetWindowTextW(m_status, text.c_str());
        // 让状态文本立刻刷新，避免长耗时操作期间界面看起来卡住
        UpdateWindow(m_status);
    }
}

//=============================================================================
// 功能 1：运行环境
//=============================================================================

void MainWindow::ActionShowEnvironment()
{
    SetStatus(L"正在检测运行环境...");

    LogLine(kSeparator);
    LogLine(L"[1] 运行环境");
    LogLine(kSeparator);

    LogFormat(L"  编译期目标架构 : %s", SysInfo::GetCompileTimeArchitecture().c_str());
    LogFormat(L"  当前进程架构   : %s", SysInfo::GetProcessMachineName().c_str());
    LogFormat(L"  本机 CPU 架构  : %s", SysInfo::GetNativeMachineName().c_str());

    // 指针宽度 —— ARM64EC 与 x64 一致，都是 64 位，这正是二者 ABI 兼容的基础
    LogFormat(L"  指针宽度       : %zu 位", sizeof(void*) * 8);

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    LogFormat(L"  逻辑处理器数   : %u", si.dwNumberOfProcessors);
    LogFormat(L"  页面大小       : %u 字节", si.dwPageSize);

    LogFormat(L"  CPU 型号       : %s", NativeCompute::GetCpuBrandString().c_str());
    LogFormat(L"  指令集特性     : %s", NativeCompute::GetCpuFeatureSummary().c_str());

    LogLine(L"");
    SetStatus(L"环境检测完成");
}

//=============================================================================
// 功能 2：模块架构
//=============================================================================

void MainWindow::ActionShowModules()
{
    SetStatus(L"正在读取模块 PE 头...");

    LogLine(kSeparator);
    LogLine(L"[2] 已加载模块的 PE 架构");
    LogLine(kSeparator);
    LogLine(L"  提示：ARM64EC 可执行文件的 PE Machine 字段依然是 AMD64，");
    LogLine(L"        需要通过加载配置目录中的 CHPE 元数据才能识别出它是混合映像。");
    LogLine(L"");

    // 宿主程序自身
    LogLine(SysInfo::DescribeModule(GetModuleHandleW(nullptr)));

    // 第三方 SDK —— 因为是隐式链接（导入库），进程启动时就已加载
    HMODULE fastImage = GetModuleHandleW(L"FastImage.dll");
    if (fastImage != nullptr)
    {
        LogLine(SysInfo::DescribeModule(fastImage));
        LogFormat(L"      路径：%s", SysInfo::GetModulePath(fastImage).c_str());
    }
    else
    {
        LogLine(L"  FastImage.dll            <未找到，请确认它与 exe 在同一目录>");
    }

    // 几个系统 DLL 作为对照
    for (const wchar_t* name : { L"ntdll.dll", L"kernel32.dll", L"user32.dll" })
    {
        HMODULE module = GetModuleHandleW(name);
        if (module != nullptr)
        {
            LogLine(SysInfo::DescribeModule(module));
        }
    }

    LogLine(L"");
    SetStatus(L"模块架构读取完成");
}

//=============================================================================
// 功能 3：调用第三方 x64 SDK
//=============================================================================

void MainWindow::OnSdkProgress(unsigned int percent, void* userContext)
{
    //-------------------------------------------------------------------------
    // 这是由 FastImage.dll 内部发起的回调。
    //
    // 迁移观察点：
    //   迁移到 ARM64EC 之后，本函数编译成 ARM64 指令，而调用方仍是 x64 代码。
    //   Windows 会为这个函数自动生成 entry thunk，调用时由模拟器完成
    //   “x64 调用约定 -> ARM64EC 调用约定”的转换。源码层面无需任何改动。
    //-------------------------------------------------------------------------
    auto* self = static_cast<MainWindow*>(userContext);
    if (self == nullptr)
    {
        return;
    }

    // 每跨过 25% 才打一条日志，避免刷屏
    if (percent >= self->m_lastReportedPercent + 25 || percent >= 100)
    {
        self->m_lastReportedPercent = percent - (percent % 25);
        self->LogFormat(L"      [DLL 回调] 处理进度 %u%%", percent);
    }
}

void MainWindow::ActionCallThirdPartySdk()
{
    SetStatus(L"正在调用第三方 SDK...");

    LogLine(kSeparator);
    LogLine(L"[3] 调用第三方 x64 组件 FastImage SDK");
    LogLine(kSeparator);

    // --- 版本信息 ---
    FI_VERSION version = {};
    FI_GetVersion(&version);
    LogFormat(L"  SDK 版本       : %u.%u.%u", version.major, version.minor, version.patch);
    LogFormat(L"  SDK 编译架构   : %s", version.buildArch);
    LogFormat(L"  SDK 编译日期   : %s", version.buildDate);
    LogFormat(L"  模块架构自述   : %s", FI_GetModuleArchitecture());
    LogLine(L"");

    // --- 创建上下文 ---
    FI_HANDLE handle = nullptr;
    FI_RESULT result = FI_Create(&handle);
    if (result != FI_OK)
    {
        LogFormat(L"  [错误] FI_Create 失败：%s", FiResultToString(result));
        SetStatus(L"调用失败");
        return;
    }
    LogLine(L"  FI_Create                 -> FI_OK");

    // --- 注册回调：DLL 将跨 ABI 边界回调本程序 ---
    result = FI_SetProgressCallback(handle, &MainWindow::OnSdkProgress, this);
    LogFormat(L"  FI_SetProgressCallback    -> %s", FiResultToString(result));

    // --- 准备测试数据 ---
    std::vector<uint8_t> pixels(kTestPixelCount);
    NativeCompute::FillTestPattern(pixels.data(), pixels.size(), kTestSeed);

    const uint32_t checksumBefore = FI_ComputeChecksum(pixels.data(), pixels.size());
    LogFormat(L"  处理前校验和              -> 0x%08X （%zu 个像素）",
              checksumBefore, pixels.size());

    // --- 真正的跨 ABI 调用 ---
    m_lastReportedPercent = 0;
    LARGE_INTEGER frequency = {}, start = {}, end = {};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    result = FI_AdjustBrightness(handle, pixels.data(), pixels.size(), kTestGain);

    QueryPerformanceCounter(&end);

    LogFormat(L"  FI_AdjustBrightness       -> %s", FiResultToString(result));

    const double elapsedMs =
        static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
        static_cast<double>(frequency.QuadPart);

    const uint32_t checksumAfter = FI_ComputeChecksum(pixels.data(), pixels.size());
    LogFormat(L"  处理后校验和              -> 0x%08X", checksumAfter);
    LogFormat(L"  耗时                      -> %.2f 毫秒", elapsedMs);
    LogFormat(L"  累计处理像素数            -> %llu",
              static_cast<unsigned long long>(FI_GetProcessedPixelCount(handle)));

    FI_Destroy(handle);
    LogLine(L"  FI_Destroy                -> 已释放");

    LogLine(L"");
    LogLine(L"  结论：宿主程序成功调用了第三方 x64 DLL，并收到了它发起的回调。");
    LogLine(L"");

    SetStatus(L"第三方 SDK 调用完成");
}

//=============================================================================
// 功能 4：结果一致性对比
//=============================================================================

void MainWindow::ActionCompareResults()
{
    SetStatus(L"正在对比计算结果...");

    LogLine(kSeparator);
    LogLine(L"[4] 本程序 SIMD 实现  vs  第三方 SDK 实现");
    LogLine(kSeparator);

    // 两份完全相同的输入数据
    std::vector<uint8_t> bufferSdk(kTestPixelCount);
    std::vector<uint8_t> bufferLocal(kTestPixelCount);
    NativeCompute::FillTestPattern(bufferSdk.data(),   bufferSdk.size(),   kTestSeed);
    NativeCompute::FillTestPattern(bufferLocal.data(), bufferLocal.size(), kTestSeed);

    // --- 第三方 SDK 路径（关闭回调，避免日志刷屏）---
    FI_HANDLE handle = nullptr;
    if (FI_Create(&handle) != FI_OK)
    {
        LogLine(L"  [错误] FI_Create 失败");
        SetStatus(L"对比失败");
        return;
    }
    FI_AdjustBrightness(handle, bufferSdk.data(), bufferSdk.size(), kTestGain);
    FI_Destroy(handle);

    // --- 本程序自己的 SSE2 实现 ---
    NativeCompute::AdjustBrightnessLocal(bufferLocal.data(), bufferLocal.size(), kTestGain);

    const uint32_t checksumSdk   = FI_ComputeChecksum(bufferSdk.data(),   bufferSdk.size());
    const uint32_t checksumLocal = FI_ComputeChecksum(bufferLocal.data(), bufferLocal.size());
    const bool     identical     = (memcmp(bufferSdk.data(), bufferLocal.data(),
                                           bufferSdk.size()) == 0);

    LogFormat(L"  第三方 SDK 结果校验和     -> 0x%08X", checksumSdk);
    LogFormat(L"  本程序 SIMD 结果校验和    -> 0x%08X", checksumLocal);
    LogFormat(L"  逐字节比较                -> %s",
              identical ? L"完全一致 ✔" : L"存在差异 ✘");
    LogLine(L"");

    // --- 手写汇编函数验证 ---
    LogLine(L"  调用 MASM 手写汇编 FastMath_SumU32()：");

    std::array<uint32_t, 256> numbers = {};
    uint64_t expected = 0;
    for (size_t i = 0; i < numbers.size(); ++i)
    {
        numbers[i] = static_cast<uint32_t>(i) * 7u + 1u;
        expected  += numbers[i];
    }

    const uint64_t actual = FastMath_SumU32(numbers.data(), numbers.size());
    LogFormat(L"      汇编返回值 = %llu，C++ 期望值 = %llu  -> %s",
              static_cast<unsigned long long>(actual),
              static_cast<unsigned long long>(expected),
              (actual == expected) ? L"一致 ✔" : L"不一致 ✘");

    LogLine(L"");
    SetStatus(L"结果对比完成");
}

//=============================================================================
// 功能 5：性能基准
//=============================================================================

void MainWindow::ActionRunBenchmark()
{
    constexpr int kIterations = 20;

    SetStatus(L"正在运行性能基准测试，请稍候...");

    LogLine(kSeparator);
    LogLine(L"[5] 性能基准测试");
    LogLine(kSeparator);
    LogFormat(L"  数据量 %zu MiB × %d 轮", kTestPixelCount / (1024 * 1024), kIterations);
    LogLine(L"");

    std::vector<uint8_t> buffer(kTestPixelCount);

    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);

    auto measure = [&](const wchar_t* label, auto&& work)
    {
        // 预热一轮，排除首次缺页中断的影响
        NativeCompute::FillTestPattern(buffer.data(), buffer.size(), kTestSeed);
        work();

        LARGE_INTEGER start = {}, end = {};
        const uint64_t tscStart = NativeCompute::ReadTimestampCounter();
        QueryPerformanceCounter(&start);

        for (int i = 0; i < kIterations; ++i)
        {
            work();
        }

        QueryPerformanceCounter(&end);
        const uint64_t tscEnd = NativeCompute::ReadTimestampCounter();

        const double elapsedMs =
            static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
            static_cast<double>(frequency.QuadPart);

        const double throughput =
            (static_cast<double>(kTestPixelCount) * kIterations) /
            (elapsedMs / 1000.0) / (1024.0 * 1024.0);

        LogFormat(L"  %s 总耗时 %8.2f ms   单轮 %6.2f ms   吞吐 %8.1f MiB/s",
                  TextUtil::PadRight(label, 24).c_str(),
                  elapsedMs, elapsedMs / kIterations, throughput);
        LogFormat(L"  %s TSC 增量 = %llu",
                  TextUtil::PadRight(L"", 24).c_str(),
                  static_cast<unsigned long long>(tscEnd - tscStart));
    };

    // --- 第三方 x64 DLL 路径 ---
    FI_HANDLE handle = nullptr;
    if (FI_Create(&handle) == FI_OK)
    {
        measure(L"第三方 SDK (x64 DLL)", [&]()
        {
            FI_AdjustBrightness(handle, buffer.data(), buffer.size(), kTestGain);
        });
        FI_Destroy(handle);
    }

    // --- 本程序自己的实现 ---
    measure(L"本程序 SIMD 实现", [&]()
    {
        NativeCompute::AdjustBrightnessLocal(buffer.data(), buffer.size(), kTestGain);
    });

    LogLine(L"");
    if (SysInfo::IsNativeArm64())
    {
        LogLine(L"  说明：当前运行在 ARM64 机器上，第三方 SDK 走的是 x64 模拟路径，");
        LogLine(L"        两者的性能差异正是 ARM64EC 迁移需要权衡的地方。");
    }
    else
    {
        LogLine(L"  说明：当前运行在 x64 机器上，两条路径都是原生执行，性能应当接近。");
        LogLine(L"        迁移到 ARM64EC 并在 ARM64 设备上运行后，第三方 SDK 会走模拟路径，");
        LogLine(L"        而本程序的代码是 ARM64 原生执行，届时这里的差距会明显拉开。");
    }
    LogLine(L"");

    SetStatus(L"性能基准测试完成");
}

//=============================================================================
// 功能 6：清空日志
//=============================================================================

void MainWindow::ActionClearLog()
{
    if (m_log != nullptr)
    {
        SetWindowTextW(m_log, L"");
    }
    SetStatus(L"日志已清空");
}

//=============================================================================
// 关于对话框
//=============================================================================

INT_PTR CALLBACK MainWindow::AboutDlgProc(HWND dlg, UINT message, WPARAM wParam, LPARAM /*lParam*/)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // 把运行期探测到的架构信息填进对话框
        std::wstring info;
        info += L"编译期架构：" + SysInfo::GetCompileTimeArchitecture() + L"\r\n";
        info += L"进程架构：" + SysInfo::GetProcessMachineName() + L"\r\n";
        info += L"本机架构：" + SysInfo::GetNativeMachineName() + L"\r\n";
        info += L"第三方 SDK：" + std::wstring(FI_GetModuleArchitecture());

        SetDlgItemTextW(dlg, IDC_ABOUT_TEXT, info.c_str());
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dlg, LOWORD(wParam));
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

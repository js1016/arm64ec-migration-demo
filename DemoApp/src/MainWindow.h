//=============================================================================
// MainWindow.h
//
// 传统 Win32 GUI 主窗口（不依赖 MFC / ATL / WinUI，纯 Win32 API）。
//=============================================================================

#pragma once

#include <windows.h>
#include <string>

class MainWindow
{
public:
    MainWindow()  = default;
    ~MainWindow() = default;

    MainWindow(const MainWindow&)            = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // 注册窗口类。整个进程只需调用一次。
    static bool RegisterWindowClass(HINSTANCE instance);

    // 创建并显示主窗口。
    bool Create(HINSTANCE instance, int showCommand);

    HWND Handle() const { return m_hwnd; }

    // 向日志窗口追加一行文本（自动补换行）。
    void LogLine(const std::wstring& text);

    // 使用 printf 风格格式化后追加一行。
    void LogFormat(const wchar_t* format, ...);

private:
    //-- 消息处理 ------------------------------------------------------------
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    static INT_PTR CALLBACK AboutDlgProc(HWND dlg, UINT message, WPARAM wParam, LPARAM lParam);

    //-- 初始化与布局 --------------------------------------------------------
    bool OnCreate();
    void OnSize();
    void OnCommand(int commandId);
    void OnDpiChanged(UINT newDpi, const RECT* suggested);

    void CreateFonts(UINT dpi);
    void DestroyFonts();

    //-- 各个功能按钮对应的动作 ----------------------------------------------
    void ActionShowEnvironment();   // 显示编译期 / 运行期架构信息
    void ActionShowModules();       // 列出关键模块的 PE 架构
    void ActionCallThirdPartySdk(); // 调用第三方 x64 SDK
    void ActionCompareResults();    // 对比“自家 SIMD”与“第三方 SIMD”的结果
    void ActionRunBenchmark();      // 简单性能基准
    void ActionClearLog();

    //-- 第三方 SDK 的进度回调（由 DLL 内部调用，跨 ABI 边界回到本程序）------
    static void __cdecl OnSdkProgress(unsigned int percent, void* userContext);

    //-- 辅助 ----------------------------------------------------------------
    void SetStatus(const std::wstring& text);
    int  Scale(int value) const;    // 按当前 DPI 缩放逻辑像素

private:
    HINSTANCE m_instance   = nullptr;
    HWND      m_hwnd       = nullptr;
    HWND      m_log        = nullptr;   // 多行只读编辑框，用作日志输出
    HWND      m_status     = nullptr;   // 底部状态文本
    HFONT     m_uiFont     = nullptr;   // 界面字体
    HFONT     m_logFont    = nullptr;   // 日志等宽字体
    UINT      m_dpi        = 96;

    // 进度回调计数，用于控制日志输出频率
    unsigned int m_lastReportedPercent = 0;
};

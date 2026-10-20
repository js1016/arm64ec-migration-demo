//=============================================================================
// Main.cpp —— 程序入口
//
// 这是一个经典的 Win32 桌面程序入口：wWinMain -> 注册窗口类 -> 创建窗口
// -> 消息循环。整个流程与架构无关，迁移到 ARM64EC 时这部分代码**无需改动**。
//=============================================================================

#include <windows.h>
#include <commctrl.h>

#include "MainWindow.h"
#include "../res/Resource.h"

int APIENTRY wWinMain(_In_     HINSTANCE instance,
                      _In_opt_ HINSTANCE /*previousInstance*/,
                      _In_     LPWSTR    /*commandLine*/,
                      _In_     int       showCommand)
{
    // 启用视觉样式所需的通用控件初始化（配合 app.manifest 中的 comctl32 v6 依赖）
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    if (!MainWindow::RegisterWindowClass(instance))
    {
        MessageBoxW(nullptr, L"注册窗口类失败。", L"启动错误", MB_ICONERROR | MB_OK);
        return 1;
    }

    MainWindow window;
    if (!window.Create(instance, showCommand))
    {
        MessageBoxW(nullptr, L"创建主窗口失败。", L"启动错误", MB_ICONERROR | MB_OK);
        return 1;
    }

    HACCEL accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_MAINMENU));

    // 标准消息循环
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (accelerators != nullptr &&
            TranslateAcceleratorW(window.Handle(), accelerators, &message))
        {
            continue;
        }

        // 让 Tab / 方向键在子控件之间正常切换焦点
        if (IsDialogMessageW(window.Handle(), &message))
        {
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

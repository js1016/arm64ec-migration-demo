//=============================================================================
// Resource.h —— 资源与命令 ID 定义
//=============================================================================

#pragma once

//-- 资源 ---------------------------------------------------------------------
#define IDI_DEMOAPP                 101
#define IDR_MAINMENU                102
#define IDD_ABOUTBOX                103

//-- 字符串表 -----------------------------------------------------------------
#define IDS_APP_TITLE               201
#define IDS_WINDOW_CLASS            202

//-- 命令 ID（菜单项与按钮共用同一套 ID，便于统一在 WM_COMMAND 中处理）--------
#define IDM_ACTION_ENVIRONMENT      40001   // 检测运行环境
#define IDM_ACTION_MODULES          40002   // 查看模块架构
#define IDM_ACTION_SDKCALL          40003   // 调用第三方 SDK
#define IDM_ACTION_COMPARE          40004   // 结果一致性对比
#define IDM_ACTION_BENCHMARK        40005   // 性能基准
#define IDM_ACTION_CLEARLOG         40006   // 清空日志
#define IDM_FILE_EXIT               40007   // 退出
#define IDM_HELP_ABOUT              40008   // 关于

//-- 子控件 ID ----------------------------------------------------------------
#define IDC_LOG_EDIT                1001
#define IDC_STATUS_TEXT             1002
#define IDC_ABOUT_TEXT              1003

#ifndef IDC_STATIC
#define IDC_STATIC                  (-1)
#endif

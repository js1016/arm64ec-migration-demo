# ARM64EC 迁移示例

本仓库用一个**完整可运行**的 Windows 桌面应用，演示如何把一个传统的 x64 C++ 程序迁移到 **ARM64EC**，并且在迁移之后**继续使用只有 x64 版本的第三方 DLL**。

仓库按阶段推进，每个阶段单独提交，这样可以通过 `git diff` 清楚地看到「为了迁移到 ARM64EC，到底改了哪些地方」。

| 阶段 | 状态 | 内容 |
| --- | --- | --- |
| **第一阶段：x64 基线** | ✅ 当前提交 | 一个为 64 位 Windows 开发的传统 Win32 GUI 应用，静态链接并调用第三方 x64 DLL |
| 第二阶段：迁移到 ARM64EC | ⏳ 后续提交 | 增加 ARM64EC 平台配置，处理架构相关代码，第三方 x64 DLL 保持原样继续使用 |

---

## 1. 这个示例想说明什么

ARM64EC（**ARM64 E**mulation **C**ompatible）是 Windows 11 在 ARM64 设备上提供的一种 ABI。它最核心的价值是：

> **同一个进程里，允许 ARM64EC 原生代码与 x64 模拟代码混合执行。**

这意味着你可以把自己的主程序重新编译为 ARM64EC（获得原生性能），同时**无需等待第三方厂商**，继续加载和调用那些只提供 x64 版本的 DLL。系统会自动在两种代码之间插入转换层（thunk），源码层面几乎无感知。

本示例刻意构造了这样一个场景：

* `DemoApp.exe` —— 我们自己的应用，**将来要迁移**到 ARM64EC。
* `FastImage.dll` —— 模拟第三方闭源组件，**永远只有 x64 版本**，厂商没有移植计划。

迁移完成后，运行应用就能直接看到：主程序是 ARM64EC 混合映像，而 `FastImage.dll` 仍然是纯 x64 映像，两者在同一进程内协同工作，并且计算结果逐字节一致。

---

## 2. 项目构成

```
arm64ec-migration-demo/
├── ARM64ECDemo.sln              解决方案（当前仅 x64 平台）
├── build.ps1                    一键构建脚本
├── README.md                    本文件
│
├── thirdparty/
│   └── FastImage/               【模拟第三方 SDK —— 永远只编译 x64】
│       ├── include/
│       │   └── FastImage.h      公共 C 接口头文件
│       ├── src/
│       │   └── FastImage.cpp    SDK 实现（内部使用 SSE2 内在函数）
│       ├── FastImage.vcxproj    仅定义 Debug|x64 / Release|x64
│       ├── bin/x64/<配置>/      构建产物 FastImage.dll
│       └── lib/x64/<配置>/      构建产物 FastImage.lib（导入库）
│
├── DemoApp/                     【宿主应用 —— 将来迁移到 ARM64EC】
│   ├── DemoApp.vcxproj
│   ├── src/
│   │   ├── Main.cpp             wWinMain 入口与消息循环
│   │   ├── MainWindow.h/.cpp    主窗口、控件布局、各功能按钮
│   │   ├── SysInfo.h/.cpp       运行环境与模块 PE 架构探测
│   │   ├── NativeCompute.h/.cpp 应用自身的 x64 相关代码（SSE2 / CPUID / RDTSC）
│   │   ├── TextUtil.h/.cpp      日志中英混排对齐
│   │   └── FastMath.asm         x64 手写汇编（MASM）
│   └── res/
│       ├── DemoApp.rc           菜单、对话框、快捷键、版本信息
│       ├── Resource.h           资源与命令 ID
│       ├── DemoApp.manifest     comctl32 v6 + PerMonitorV2 DPI
│       └── DemoApp.ico          应用图标
│
└── build/<平台>/<配置>/         最终输出（DemoApp.exe + FastImage.dll）
```

### 2.1 第三方组件：FastImage SDK

扮演「你无法修改的二进制依赖」。它是一个纯 C 接口的图像处理库：

| 导出函数 | 作用 |
| --- | --- |
| `FI_GetVersion` | 返回版本号、**编译时固化的架构字符串**、编译日期 |
| `FI_GetModuleArchitecture` | 返回 `L"x64 (AMD64)"`，迁移后依然如此 |
| `FI_Create` / `FI_Destroy` | 创建 / 销毁不透明上下文句柄 |
| `FI_SetProgressCallback` | **注册回调函数**，由 DLL 反向调用宿主 |
| `FI_AdjustBrightness` | 用 SSE2 做 8 位灰度亮度调整 |
| `FI_ComputeChecksum` | FNV-1a 校验和（纯整数，跨架构结果一致） |
| `FI_GetProcessedPixelCount` | 读取上下文内部累计状态 |

实现里有一道**编译期守卫**，如果有人把它误配置成 ARM64/ARM64EC 会直接报错，确保演示中它始终是 x64：

```cpp
#if defined(_M_ARM64EC) || defined(_M_ARM64)
#error "FastImage 模拟的是仅提供 x64 版本的第三方库，不得以 ARM64/ARM64EC 方式编译。"
#endif
```

> 注意 `FI_SetProgressCallback`：它让 DLL 能够**回调**宿主程序。迁移后这条路径会变成「x64 模拟代码 → ARM64EC 原生代码」，由系统自动生成的 entry thunk 完成转换。这是 ARM64EC 中最值得关注的互操作场景之一。

### 2.2 宿主应用：DemoApp

一个**不依赖 MFC / ATL / WinUI 的纯 Win32 GUI 程序**：手工注册窗口类、创建窗口、跑消息循环，用 Button / Edit / Static 子控件搭界面，带菜单、快捷键、「关于」对话框和版本信息资源。

它通过**导入库隐式链接** `FastImage.lib`，因此进程启动时 `FastImage.dll` 就已被加载。

界面上有 6 个功能按钮：

| 按钮 | 作用 |
| --- | --- |
| 1. 检测运行环境 | 编译期架构、进程架构、本机 CPU 架构、指针宽度、CPU 型号与指令集 |
| 2. 查看模块架构 | 直接读取各模块 PE 头的 `Machine` 字段与 CHPE 元数据 |
| 3. 调用第三方 SDK | 完整走一遍 SDK 调用流程，并接收 DLL 发起的进度回调 |
| 4. 结果一致性对比 | 用相同输入分别跑「SDK 的 SIMD」与「本程序的 SIMD」，逐字节比对；同时验证 MASM 汇编函数 |
| 5. 性能基准测试 | 测量两条路径的耗时与吞吐（迁移后差异会明显体现） |
| 6. 清空日志 | —— |

---

## 3. 构建与运行

### 环境要求

* Windows 10 1709 或更高版本（需要 `IsWow64Process2`）
* Visual Studio 2022 / 2026，安装「**使用 C++ 的桌面开发**」工作负载
* Windows 10/11 SDK

### 构建

```powershell
.\build.ps1                              # Release | x64
.\build.ps1 -Configuration Debug         # Debug | x64
.\build.ps1 -Rebuild                     # 完全重建
```

或者直接用 Visual Studio 打开 `ARM64ECDemo.sln` 按 F5。

构建顺序由解决方案中的项目依赖保证：先 `FastImage`，后 `DemoApp`；`DemoApp` 的生成后事件会把 `FastImage.dll` 复制到输出目录。

### 运行

```powershell
.\build\x64\Release\DemoApp.exe
```

在 x64 机器上，「查看模块架构」会输出类似：

```
  DemoApp.exe              PE Machine = x64 (AMD64)   纯 x64 映像（原生执行）
  FastImage.dll            PE Machine = x64 (AMD64)   纯 x64 映像（原生执行）
  ntdll.dll                PE Machine = x64 (AMD64)   纯 x64 映像（原生执行）
```

迁移到 ARM64EC 并在 ARM64 设备上运行后，这里会变成：

```
  DemoApp.exe              PE Machine = x64 (AMD64)   混合映像 ARM64EC/ARM64X（以 ARM64 原生指令执行）
  FastImage.dll            PE Machine = x64 (AMD64)   纯 x64 映像（由 x64 模拟器执行）
  ntdll.dll                PE Machine = ARM64         纯 ARM64 原生映像
```

> 一个容易被误解的细节：**ARM64EC 可执行文件 PE 头里的 `Machine` 字段依然是 `AMD64` (0x8664)**，这样既有的 x64 加载器与工具链才会接受它。真正区分 ARM64EC 的方法是检查「加载配置目录」中的 CHPE 元数据指针 —— 本示例的 `SysInfo::IsHybridModule()` 就是这么做的。

---

## 4. 为第二阶段埋下的「迁移观察点」

当前这一版代码是干净、可正常工作的 x64 应用，但其中刻意包含了真实项目迁移时最常见的几类问题。下一阶段的提交会逐一处理它们：

| # | 位置 | 问题 | 预期的迁移处理 |
| --- | --- | --- | --- |
| 1 | `DemoApp.vcxproj`、`ARM64ECDemo.sln`、`build.ps1` | 平台被硬编码为 `x64` | 新增 `ARM64EC` 平台配置与解决方案映射 |
| 2 | `DemoApp/src/FastMath.asm` | **MASM 汇编**；`ml64.exe` 无法为 ARM64EC 生成目标文件 | 改用 C++ 重写，或用 `armasm64.exe` 写 ARM64 版本并按平台条件编译 |
| 3 | `NativeCompute.cpp` 中的 `#if defined(_M_X64)` | **ARM64EC 下 `_M_X64` / `_M_AMD64` 仍然被定义**，这个分支会被选中，SSE2 经 `softintrin` 软件模拟，能跑但慢 | 判断条件改为先检查 `_M_ARM64EC`，并提供 NEON 实现 |
| 4 | `NativeCompute.cpp` 中的 `__cpuid` | x64 专有内在函数，在 ARM64EC 下返回的是模拟器合成的信息 | 改用 `IsProcessorFeaturePresent` 等架构中立 API |
| 5 | `NativeCompute.cpp` 中的 `__rdtsc` | 语义与频率和 x86 TSC 不同 | 改用 `QueryPerformanceCounter` |
| 6 | `SysInfo::GetCompileTimeArchitecture()` | 架构判断顺序 | 已经按正确顺序写好（`_M_ARM64EC` 在 `_M_X64` 之前），可作为正面示范 |
| 7 | `DemoApp.vcxproj` 的 `FastImageLibDir` | 第三方库路径里硬编码 `x64` | **保持不变**：ARM64EC 就是要继续链接这个 x64 导入库 |
| 8 | 第三方 DLL 的复制 | 生成后事件从 `bin\x64\` 复制 | **保持不变**，但需要确保 ARM64EC 输出目录也能拿到这个 x64 DLL |
| 9 | `FI_SetProgressCallback` 回调 | 跨 ABI 的反向调用 | **源码无需改动**，由系统 entry thunk 自动处理（重点验证项） |
| 10 | `Main.cpp`、`MainWindow.cpp`、`DemoApp.rc`、`DemoApp.manifest` | 纯 Win32 API、资源、清单 | **完全无需改动**，与 CPU 架构无关 |

可以看到，真正需要动的地方集中在「构建配置」和「少量架构相关代码」上，**绝大部分业务代码和全部第三方依赖都原封不动** —— 这正是 ARM64EC 相比直接迁移到 ARM64 原生的最大优势。

---

## 5. 参考资料

* [Arm64EC —— 为 Windows 11 on Arm 构建和移植应用](https://learn.microsoft.com/windows/arm/arm64ec)
* [Arm64EC ABI 约定概述](https://learn.microsoft.com/windows/arm/arm64ec-abi)
* [在 Visual Studio 中使用 Arm64EC 生成项目](https://learn.microsoft.com/windows/arm/arm64ec-build)
* [Arm64X PE 文件](https://learn.microsoft.com/windows/arm/arm64x-pe)

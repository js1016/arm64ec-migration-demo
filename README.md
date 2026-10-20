# ARM64EC 迁移示例

本仓库用一个**完整可运行**的 Windows 桌面应用，演示如何把一个传统的 x64 C++ 程序迁移到 **ARM64EC**，并且在迁移之后**继续使用只有 x64 版本的第三方 DLL**。

仓库按阶段推进，每个阶段单独提交，这样可以通过 `git diff` 清楚地看到「为了迁移到 ARM64EC，到底改了哪些地方」。

| 阶段 | 状态 | 内容 |
| --- | --- | --- |
| 第一阶段：x64 基线 | ✅ 已完成 | 一个为 64 位 Windows 开发的传统 Win32 GUI 应用，静态链接并调用第三方 x64 DLL |
| **第二阶段：迁移到 ARM64EC** | ✅ 当前提交 | 增加 ARM64EC 平台配置，处理架构相关代码；第三方 x64 DLL **原样保留、不重新编译** |

迁移到底改了什么，看这一条就够了：

```
git diff <第一阶段提交> <第二阶段提交>
```

改动全部集中在**构建配置**和**三处架构相关代码**上，业务逻辑、Win32 界面代码、资源文件和第三方依赖**一个字都没动**。详见 [第 4 节：迁移改动清单](#4-迁移改动清单第二阶段)。

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
├── DemoApp/                     【宿主应用 —— 可构建为 x64 或 ARM64EC】
│   ├── DemoApp.vcxproj          同时定义 x64 与 ARM64EC 两个平台
│   ├── src/
│   │   ├── Main.cpp             wWinMain 入口与消息循环（架构无关）
│   │   ├── MainWindow.h/.cpp    主窗口、控件布局、各功能按钮
│   │   ├── SysInfo.h/.cpp       运行环境、模块 PE 架构、函数级 EC 代码判定
│   │   ├── NativeCompute.h/.cpp 架构相关代码（SSE2 / NEON 双路径）
│   │   ├── TextUtil.h/.cpp      日志中英混排对齐
│   │   ├── FastMath.asm         x64 手写汇编（MASM）—— ARM64EC 下排除
│   │   └── FastMathArm64.cpp    ARM64/ARM64EC 的 NEON 实现 —— x64 下排除
│   └── res/
│       ├── DemoApp.rc           菜单、对话框、快捷键、版本信息
│       ├── Resource.h           资源与命令 ID
│       ├── DemoApp.manifest     comctl32 v6 + PerMonitorV2 DPI
│       └── DemoApp.ico          应用图标
│
└── build/<平台>/<配置>/         最终输出（DemoApp.exe + FastImage.dll）
                                 注意 build/ARM64EC/ 里的 FastImage.dll 是 x64 的
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
| 1. 检测运行环境 | 编译期架构、进程架构、本机 CPU 架构、指针宽度、CPU 型号与指令集、当前启用的 SIMD 路径 |
| 2. 查看模块架构 | 读取各模块 PE 头的 `Machine` 字段与 CHPE 元数据；并用 `RtlIsEcCode` 做**函数级**代码类型判定 |
| 3. 调用第三方 SDK | 完整走一遍 SDK 调用流程，并接收 DLL 发起的进度回调 |
| 4. 结果一致性对比 | 用相同输入分别跑「SDK 的 SIMD」与「本程序的 SIMD」，逐字节比对；同时验证 `FastMath_SumU32` |
| 5. 性能基准测试 | 测量两条路径的耗时与吞吐（在 ARM64 设备上差异会明显体现） |
| 6. 清空日志 | —— |

迁移到 ARM64EC 后，第 2 项会显示出同一进程内两类代码的共存：

```
  本程序 · 进度回调              0x...  ARM64EC 代码（ARM64 指令，CPU 直接执行）
  本程序 · SIMD 实现             0x...  ARM64EC 代码（ARM64 指令，CPU 直接执行）
  第三方 · FI_AdjustBrightness   0x...  x64 代码（由模拟器执行）
```

---

## 3. 构建与运行

### 环境要求

* Windows 10 1709 或更高版本（需要 `IsWow64Process2`）
* Visual Studio 2022 / 2026，安装「**使用 C++ 的桌面开发**」工作负载
* Windows 10/11 SDK

### 构建

```powershell
.\build.ps1                                        # Release | x64
.\build.ps1 -Configuration Debug                   # Debug   | x64
.\build.ps1 -Platform ARM64EC                      # Release | ARM64EC
.\build.ps1 -Configuration Debug -Platform ARM64EC # Debug   | ARM64EC
.\build.ps1 -Rebuild                               # 完全重建
```

或者直接用 Visual Studio 打开 `ARM64ECDemo.sln`，在平台下拉框里选择 `x64` 或 `ARM64EC`。

构建顺序由解决方案中的项目依赖保证：先 `FastImage`，后 `DemoApp`；`DemoApp` 的生成后事件会把 `FastImage.dll` 复制到输出目录。

> **ARM64EC 需要的组件**：Visual Studio 安装器里的「**MSVC v1xx - ARM64/ARM64EC 生成工具**」。
> 如果缺失，表现为 `VC\Tools\MSVC\<版本>\lib\` 下没有 `arm64ec` 目录。
> ARM64EC 可以在 x64 机器上交叉编译，但**产物只能在 ARM64 版 Windows 11 上运行**。

### 运行与部署

```powershell
.\build\x64\Release\DemoApp.exe          # x64 机器
.\build\ARM64EC\Release\DemoApp.exe      # 仅限 ARM64 版 Windows 11
```

把 `build\ARM64EC\<配置>\` 整个目录（`DemoApp.exe` + `FastImage.dll`）拷到 ARM64 机器上即可直接运行，**目标机不需要安装任何运行库**。

这是因为本示例把 CRT 配置成了**静态链接**（`/MT`、`/MTd`），两个产物都只依赖系统 DLL：

```
DemoApp.exe    -> FastImage.dll, COMCTL32.dll, KERNEL32.dll, USER32.dll, GDI32.dll, ADVAPI32.dll
FastImage.dll  -> KERNEL32.dll
```

> **为什么不用默认的动态 CRT？**
>
> 如果用默认的 `/MD`、`/MDd`，把产物拷到干净机器上会报
> `找不到 VCRUNTIME140_1D.dll` 之类的错误：
>
> * 文件名里的 **`D` 表示调试版 CRT**，微软规定**不可再分发**，只有装了
>   Visual Studio 的机器才有 —— 所以 Debug 构建根本没法拷贝部署；
> * 即使是 Release，本示例也需要目标机**同时**装两套运行库：
>   ARM64 版（给 ARM64EC 的 `DemoApp.exe`）和 x64 版（给第三方的
>   `FastImage.dll`）。这恰好也是 ARM64EC 项目部署时容易忽略的一点 ——
>   **混合架构进程意味着混合架构的运行库依赖**。
>
> 若你的项目必须使用动态 CRT，就要在 ARM64 目标机上同时安装
> `vc_redist.arm64.exe` 与 `vc_redist.x64.exe`，并且只分发 Release 构建。
>
> 静态 CRT 的代价是 DemoApp 与 FastImage 各持有一份独立的 CRT 实例，
> 因此**不能跨 DLL 边界传递 CRT 对象**（在一边 `malloc`、另一边 `free`，
> 或互传 `FILE*`、`std::` 容器）。本示例的接口是按这个约束设计的。

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

## 4. 迁移改动清单（第二阶段）

### 4.0 总览

| # | 位置 | 性质 | 处理方式 |
| --- | --- | --- | --- |
| 1 | `ARM64ECDemo.sln` | 构建配置 | 新增 ARM64EC 解决方案配置，并把 FastImage 映射到它自己的 x64 配置 |
| 2 | `DemoApp.vcxproj` | 构建配置 | 新增 ARM64EC 平台；按平台互斥编译汇编/NEON 源文件 |
| 3 | `build.ps1` | 构建配置 | `-Platform` 增加 `ARM64EC` |
| 4 | `FastMath.asm` → `FastMathArm64.cpp` | **硬阻断** | MASM 无法为 ARM64EC 汇编，改用 NEON 内在函数重写 |
| 5 | `NativeCompute.cpp` 的 SIMD 分支 | **隐蔽陷阱** | `_M_ARM64EC` 必须判断在 `_M_X64` 之前，并补 NEON 实现（注意别机械翻译 SSE2，实机快 2.04 倍的关键） |
| 6 | `NativeCompute.cpp` 的 `__cpuid` | 语义不同 | 改读注册表 + `IsProcessorFeaturePresent` |
| 7 | `NativeCompute.cpp` 的 `__rdtsc` | 语义不同 | 改用 `QueryPerformanceCounter` |
| 8 | `SysInfo.cpp` | 新增能力 | 接入 `ntdll!RtlIsEcCode`，做函数级代码类型判定 |
| 9 | 第三方库路径 `lib\x64` | **刻意不改** | ARM64EC 就是要继续链接这个 x64 导入库 |
| 10 | 两个 `.vcxproj` 的 `RuntimeLibrary` | 部署问题 | 改为静态链接 CRT，避免目标机同时需要 ARM64 + x64 两套运行库 |
| 11 | `Main.cpp` / 界面代码 / `.rc` / `.manifest` | **无需改动** | 纯 Win32 API 与资源，与 CPU 架构无关 |

### 4.1 硬阻断：MASM 手写汇编

`ml64.exe` 只能产出 x64 目标文件，**无法**为 ARM64EC 汇编。这是迁移中最先撞上的墙。

处理方式是让两份实现按平台互斥编译，对外暴露同一个符号，调用方代码一行不改：

```xml
<!-- DemoApp.vcxproj -->
<ClCompile Include="src\FastMathArm64.cpp">
  <ExcludedFromBuild Condition="'$(Platform)'=='x64'">true</ExcludedFromBuild>
</ClCompile>

<MASM Include="src\FastMath.asm">
  <ExcludedFromBuild Condition="'$(Platform)'=='ARM64EC'">true</ExcludedFromBuild>
</MASM>
```

新增的 `FastMathArm64.cpp` 用 NEON 内在函数实现同样的语义。编译器甚至把 `vpaddlq_u32 + vaddq_u64` 融合成了单条 `uadalp` 指令：

```
FastMath_SumU32:
    ldp     q4,q5,[x9]
    uadalp  v6.2d,v4.4s
    uadalp  v7.2d,v5.4s
```

> 如果确实需要手写汇编，另一条路是用 `armasm64.exe` 写 ARM64 汇编；但只要该函数可能被 x64 代码调用，就还得自己处理 entry thunk，成本明显更高。

### 4.2 隐蔽陷阱：`_M_X64` 在 ARM64EC 下仍然被定义

这是整个迁移里**最值得记住的一条**。

```cpp
// ❌ 迁移前：看似「ARM64EC 会走标量兜底」，实际完全不是
#if defined(_M_X64)
    ... SSE2 代码 ...
#endif
```

MSVC 编译 ARM64EC 时会**同时定义 `_M_X64` 和 `_M_AMD64`**（为了让海量既有 x64 代码里的数据模型假设继续成立）。于是上面这个分支照样会被选中，SSE2 内在函数被链接到 `softintrin.lib` 的软件模拟实现上 —— **编译通过、运行结果也正确，只是每条 SSE2 指令都要用若干条 ARM64 指令去模拟**。不报错、不崩溃，只是悄悄变慢，极难察觉。

```cpp
// ✅ 迁移后：ARM64EC / ARM64 的判断必须放在 _M_X64 之前
#if defined(_M_ARM64EC) || defined(_M_ARM64)
    ... NEON 代码 ...
#elif defined(_M_X64)
    ... SSE2 代码 ...
#endif
```

同样的顺序要求也适用于 `#include`：先判断 `_M_ARM64EC` 才能避免把 `<emmintrin.h>` 拖进 ARM64EC 构建。

#### 延伸教训：不要把 SSE2 逐条「翻译」成 NEON

本示例在 ARM64 实机上跑出过一个很反直觉的结果：**第一版「原生 NEON」比 x64 模拟器跑 SSE2 还慢**。

原因在于最初的 NEON 版本是机械对应 SSE2 写的。NEON 没有与 `_mm_mulhi_epu16`（取乘积高半部分）完全对应的指令，于是「一条 SSE2」被拆成了「加宽乘法 + 右移窄化」，低/高半各做一次：

```cpp
// ❌ 机械翻译：单是这一步就要 4 条指令
lo = vcombine_u16(vshrn_n_u32(vmull_u16(vget_low_u16(lo),  vGain), 16),
                  vshrn_n_u32(vmull_u16(vget_high_u16(lo), vGain), 16));
```

换成 ARM64 自己擅长的 `SQDMULH`（加倍取高半饱和乘法）后，同样的运算**一条指令就够**：

```cpp
// ✅ vqdmulhq_s16(a,b) == 饱和((2*a*b) >> 16)
//    取 a = pixel << 7（最大 255<<7 = 32640，稳在 s16 正数区），
//    则 (2*(pixel<<7)*gain) >> 16 == (pixel*gain) >> 8，正是所需的 8.8 定点乘法
const int16x8_t lo = vreinterpretq_s16_u16(vshll_n_u8(vget_low_u8(src), 7));
const int16x8_t ro = vqdmulhq_s16(lo, vGain);
vst1q_u8(pixels + i, vcombine_u8(vqmovun_s16(ro), vqmovun_s16(rh)));
```

生成的主循环干净了很多，每 16 个像素约 8 条向量指令（原先 14 条以上）：

```
ldr     q5,[x9,x0]
ushll   v7.8h,v5.8b,#7
sqdmulh v7.8h,v7.8h,v6.8h
sqxtun  v7.8b,v7.8h
...
str     q7,[x9,x0]
```

ARM64 实机实测，这一次重写带来 **2.04 倍**加速，也让原生代码从「比模拟器慢 14 %」变成「比模拟器快 1.79 倍」：

| NEON 实现 | 单轮耗时 | 吞吐 |
| --- | --- | --- |
| 机械翻译自 SSE2 | 0.89 ms | 4493 MiB/s |
| 改用 `SQDMULH` | **0.44 ms** | **9175 MiB/s** |

> 两个版本逐位等价 —— 已对 `gain 0..1024 × pixel 0..255` 共 262,400 组全集穷举验证，零差异；程序里的「结果一致性对比」功能每次运行也会再校验一遍（实机校验和 `0x0288A91D`，与 x64 基线完全相同）。
>
> **教训**：x64 模拟器会把 SSE2 翻译成相当高效的 ARM64 序列。如果你只是把内在函数逐条换名字，很可能跑不过模拟器。要么按目标 ISA 的特点重写，要么干脆先别动 —— 让模拟器去跑。

### 4.3 语义不同：`__cpuid` 与 `__rdtsc`

| 内在函数 | 在 ARM64EC 下的问题 | 替代方案 |
| --- | --- | --- |
| `__cpuid` | 拿到的是模拟器合成的信息，不是真实 ARM64 CPU 标识 | 注册表 `HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString` + `IsProcessorFeaturePresent` |
| `__rdtsc` | 映射到 ARM64 通用计时寄存器，频率与语义都和 x86 TSC 不同 | `QueryPerformanceCounter` / `QueryPerformanceFrequency` |

这类代码的共同点是：**不会编译失败，只会悄悄给出错误答案**。凡是用 CPUID 做授权校验、功能探测或性能分支的代码，迁移时都必须重新设计。

### 4.4 刻意不改：第三方库路径

```xml
<!-- 这里的 x64 是故意写死的，绝不能改成 $(Platform) -->
<FastImageLibDir>$(FastImageRoot)lib\x64\$(Configuration)</FastImageLibDir>
<FastImageBinDir>$(FastImageRoot)bin\x64\$(Configuration)</FastImageBinDir>
```

ARM64EC 的全部价值就在这一行：**继续链接厂商的 x64 导入库、继续加载厂商的 x64 DLL**。生成后事件依旧把这个 x64 的 `FastImage.dll` 复制进 `build\ARM64EC\` 输出目录，这是预期行为而不是配置错误。

解决方案层面用配置映射保证第三方项目永远构建为 x64：

```
{FastImage}.Release|ARM64EC.ActiveCfg = Release|x64
{FastImage}.Release|ARM64EC.Build.0   = Release|x64
```

### 4.5 部署：混合架构进程意味着混合架构的运行库依赖

这一条在迁移文档里很少被提到，但实际把产物拷到 ARM64 测试机时一定会撞上。

ARM64EC 进程里同时跑着两种架构的代码，于是动态 CRT 的依赖也是两套：

| 模块 | 架构 | 需要的运行库 |
| --- | --- | --- |
| `DemoApp.exe` | ARM64EC | ARM64 版 VC++ 运行库（`vc_redist.arm64.exe`） |
| `FastImage.dll` | x64 | **x64 版** VC++ 运行库（`vc_redist.x64.exe`） |

而且 **Debug 构建无论如何都无法部署** —— `VCRUNTIME140_1D.dll` 这类调试版 CRT 不可再分发，只存在于装了 Visual Studio 的机器上。

本示例的处理方式是两个工程都改用静态 CRT：

```xml
<RuntimeLibrary>MultiThreaded</RuntimeLibrary>       <!-- Release，原为 MultiThreadedDLL -->
<RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>  <!-- Debug，原为 MultiThreadedDebugDLL -->
```

这样 Debug/Release 都能直接拷贝运行。代价与适用前提见上面[运行与部署](#运行与部署)一节。

### 4.6 新增能力：函数级代码类型判定

ARM64EC 进程里「模块是什么架构」已经不够用了 —— 同一进程内两类代码是**逐函数**混合的。Windows 11 的 `ntdll!RtlIsEcCode` 可以判定任意代码地址属于哪一类，本示例用它来直观展示共存关系（动态 `GetProcAddress` 解析，因此在 x64 上也能正常运行）。

> 注意：x64 版 Windows 的 ntdll **同样导出** `RtlIsEcCode`，只是对任何地址都返回 `FALSE`。所以真正的对比要在 ARM64 设备上才看得到。

### 4.7 完全没动的部分

`Main.cpp`、`MainWindow.cpp`（除了新增几行日志）、`TextUtil.*`、`DemoApp.rc`、`DemoApp.manifest`、`Resource.h`，以及**整个 `thirdparty/FastImage`** —— 窗口类注册、消息循环、控件布局、DPI 处理、菜单、对话框、版本资源全部与 CPU 架构无关。

这正是 ARM64EC 相比「直接迁移到 ARM64 原生」的最大优势：**你不必等第三方厂商，也不必重写业务代码**。

---

## 5. 迁移结果验证

### 5.1 构建与静态验证（x64 开发机交叉编译）

四种组合均构建通过：`x64|Debug`、`x64|Release`、`ARM64EC|Debug`、`ARM64EC|Release`。

**产物架构**（`dumpbin /headers`）：

```
DemoApp.exe     8664 machine (x64) (ARM64X)   <- ARM64EC 混合映像
FastImage.dll   8664 machine (x64)            <- 纯 x64，未重新编译
```

**CHPE 元数据**（`dumpbin /loadconfig`）确认是混合映像：

```
0000000140010120 Hybrid metadata pointer
               2 Hybrid code address range count
```

**ARM64EC 的 DemoApp 仍然导入 x64 的 FastImage.dll**（`dumpbin /imports`）：

```
FI_AdjustBrightness   FI_ComputeChecksum   FI_Create     FI_Destroy
FI_GetModuleArchitecture   FI_GetProcessedPixelCount   FI_GetVersion   FI_SetProgressCallback
```

**指令级验证**（`dumpbin /disasm` 统计）：

| 指令 | 出现次数 | 说明 |
| --- | --- | --- |
| `umull` / `uqxtn` / `shll` | 6 / 2 / 2 | NEON 亮度调整实现 |
| `uadalp` | 2 | `FastMath_SumU32` 的 NEON 实现 |
| `punpcklbw` / `pmulhuw` / `packuswb` | **0** | SSE2 已彻底不再参与 ARM64EC 构建 |
| `rdtsc` / `cpuid` | **0** | x64 专有内在函数已全部替换 |

**部署依赖**（`dumpbin /dependents`，8 个产物全部如此）：

```
DemoApp.exe    -> FastImage.dll COMCTL32.dll KERNEL32.dll USER32.dll GDI32.dll ADVAPI32.dll
FastImage.dll  -> KERNEL32.dll
```

没有任何 `VCRUNTIME*` / `MSVCP*` / `api-ms-win-crt-*` 依赖，拷到干净机器即可运行。

x64 构建的功能回归也全部通过：DLL 回调正常触发、两套 SIMD 实现结果逐字节一致、`FastMath_SumU32` 返回值正确，且处理后校验和与迁移前完全相同（`0x0288A91D`）。

### 5.2 实机验证（ARM64 Windows 11 / Cobalt 100）

把 `build\ARM64EC\Release\` 拷到 ARM64 机器直接运行，核心结论全部坐实：

```
编译期目标架构 : ARM64EC（同时定义了 _M_X64 与 _M_ARM64EC）
当前进程架构   : ARM64（原生执行）
CPU 型号       : Cobalt 100                      <- 注册表读取生效（替代 __cpuid）
指令集特性     : NEON CRC32 Crypto LSE DotProd JSCVT   <- IsProcessorFeaturePresent 生效
本程序 SIMD 路径: NEON（ARM64EC 原生）
```

**模块层面** —— 混合映像与纯 x64 映像在同一进程内共存：

```
DemoApp.exe     PE Machine = x64 (AMD64)   混合映像 ARM64EC/ARM64X（以 ARM64 原生指令执行）
FastImage.dll   PE Machine = x64 (AMD64)   纯 x64 映像（由 x64 模拟器执行）
ntdll.dll       PE Machine = x64 (AMD64)   混合映像 ARM64EC/ARM64X
```

**函数层面** —— `RtlIsEcCode` 把两类代码区分得一清二楚：

```
本程序 · 进度回调              ARM64EC 代码（ARM64 指令，CPU 直接执行）
本程序 · SIMD 实现             ARM64EC 代码（ARM64 指令，CPU 直接执行）
本程序 · FastMath_SumU32       ARM64EC 代码（ARM64 指令，CPU 直接执行）
第三方 · FI_AdjustBrightness   x64 代码（由模拟器执行）
```

**正确性** —— 校验和与 x64 基线完全一致（`0xAF3D81C5` → `0x0288A91D`），NEON 与 SSE2 实现逐字节相同，DLL 跨 ABI 边界发起的回调全部正常送达。

> ⚠ 一个容易误读的细节：`GetProcAddress(kernel32, "Sleep")` 得到的地址会被判定为「非 ARM64EC」。
> 这**不代表系统 DLL 被模拟执行** —— kernel32 本身是 ARM64X 混合映像，只是导出入口
> 是面向 x64 调用方的兼容入口，再转到 ARM64EC 实现。
> **判断模块是否原生要看 CHPE 元数据，不能用单个函数地址去推断**。程序里对这一点有专门提示。

### 5.3 性能：一组很能说明问题的实测数据

ARM64 实机（Cobalt 100）Release 实测，4 MiB × 20 轮，同一台机器、同一份输入：

| 路径 | 单轮耗时 | 吞吐 | 相对模拟 x64 |
| --- | --- | --- | --- |
| 第三方 SDK —— x64 SSE2，**模拟执行** | 0.78 ms | 5122 MiB/s | 基准 |
| 本程序 NEON —— **机械翻译自 SSE2** | 0.89 ms | 4493 MiB/s | **慢 14 %** ❌ |
| 本程序 NEON —— **改用 SQDMULH 重写** | **0.44 ms** | **9175 MiB/s** | **快 1.79 倍** ✅ |

这三行数字串起来，就是本示例最重要的一条结论：

1. **x64 模拟器很强，不要低估它。** Windows on ARM 会把热点 x64 代码 JIT 成 ARM64 并缓存。一份「照着 SSE2 结构逐条换名字」写出来的原生 NEON 代码，实测**跑不过模拟器**。
2. **真正的收益来自按目标 ISA 重写，而不是换个平台开关。** 把那 4 条「加宽乘法 + 右移窄化」换成 1 条 `SQDMULH` 之后（详见 [4.2 节](#延伸教训不要把-sse2-逐条翻译成-neon)），同样的算法快了 **2.04 倍**，这才反超模拟器 1.79 倍。
3. 顺带也证伪了一个常见的想当然：这个负载**并不是**纯访存带宽瓶颈 —— 如果是，单靠减少指令数不可能把吞吐从 4493 提到 9175 MiB/s。

> **Debug 构建的数字毫无意义。** `/Od` 下 SIMD 内在函数会退化成大量寄存器溢出，实测 Debug 版 NEON 比模拟 x64 慢近 3 倍。性能对比请一律使用 Release —— 程序里也加了对应提示。

**所以迁移 ARM64EC 到底值不值？**

值，但前提是你愿意为热点代码做适配。除了上面的向量吞吐，ARM64EC 在启动时间、内存占用、功耗上也有收益，这些不会体现在本测试里。

一句话：**迁移到 ARM64EC 不会自动变快；照搬 x86 写法的移植，很可能还不如让模拟器去跑。一切以实测为准。**

---

## 6. 参考资料

* [Arm64EC —— 为 Windows 11 on Arm 构建和移植应用](https://learn.microsoft.com/windows/arm/arm64ec)
* [Arm64EC ABI 约定概述](https://learn.microsoft.com/windows/arm/arm64ec-abi)
* [在 Visual Studio 中使用 Arm64EC 生成项目](https://learn.microsoft.com/windows/arm/arm64ec-build)
* [Arm64X PE 文件](https://learn.microsoft.com/windows/arm/arm64x-pe)

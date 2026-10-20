#Requires -Version 5.1
<#
.SYNOPSIS
    构建 ARM64EC 迁移示例（第一阶段：x64）。

.DESCRIPTION
    自动定位 Visual Studio 与 MSBuild，然后构建整个解决方案。
    构建顺序由解决方案中的项目依赖保证：先 FastImage（第三方 x64 DLL），
    后 DemoApp（宿主应用）。

    迁移提示：
      当前脚本把平台硬编码为 x64。迁移到 ARM64EC 时，需要把 -Platform
      参数的取值范围扩展为 x64 / ARM64EC，并注意第三方 DLL 始终只构建 x64。

.PARAMETER Configuration
    Debug 或 Release，默认 Release。

.PARAMETER Platform
    目标平台，当前阶段仅支持 x64。

.PARAMETER Rebuild
    指定时执行 Rebuild 而不是 Build。

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Configuration Debug -Rebuild
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'

$root     = Split-Path -Parent $MyInvocation.MyCommand.Definition
$solution = Join-Path $root 'ARM64ECDemo.sln'

# ---- 定位 MSBuild ----------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "未找到 vswhere.exe，请确认已安装 Visual Studio 2022 或更高版本。"
}

$msbuild = & $vswhere -latest -products * `
    -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

if (-not $msbuild) {
    throw "未找到 MSBuild.exe，请确认已安装『使用 C++ 的桌面开发』工作负载。"
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }

Write-Host ''
Write-Host '====================================================' -ForegroundColor Cyan
Write-Host ' ARM64EC 迁移示例 —— 构建' -ForegroundColor Cyan
Write-Host '====================================================' -ForegroundColor Cyan
Write-Host ("  MSBuild    : {0}" -f $msbuild)
Write-Host ("  解决方案   : {0}" -f $solution)
Write-Host ("  配置/平台  : {0}|{1}" -f $Configuration, $Platform)
Write-Host ("  目标       : {0}" -f $target)
Write-Host ''

& $msbuild $solution `
    "/t:$target" `
    "/p:Configuration=$Configuration" `
    "/p:Platform=$Platform" `
    /m `
    /nologo `
    /verbosity:minimal

if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host '构建失败。' -ForegroundColor Red
    exit $LASTEXITCODE
}

$outDir = Join-Path $root "build\$Platform\$Configuration"

Write-Host ''
Write-Host '构建成功。输出目录内容：' -ForegroundColor Green
Get-ChildItem $outDir -File |
    Where-Object { $_.Extension -in '.exe', '.dll' } |
    Select-Object Name, Length, LastWriteTime |
    Format-Table -AutoSize

Write-Host ("运行：{0}" -f (Join-Path $outDir 'DemoApp.exe')) -ForegroundColor Green
Write-Host ''

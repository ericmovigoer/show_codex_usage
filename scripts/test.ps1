param([switch]$Integration, [ValidateRange(0,86400)][int]$SoakSeconds = 0)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw '未找到 VS2022 C++ 编译工具。' }
$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $projectRoot 'tests\ModelTests.vcxproj') /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw '模型测试编译失败。' }
& (Join-Path $projectRoot 'tests\bin\x64\Release\ModelTests.exe')
if ($LASTEXITCODE -ne 0) { throw '模型测试失败。' }
if ($Integration -or $SoakSeconds -gt 0) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
    & python (Join-Path $projectRoot 'tests\gui_validation.py') --soak-seconds $SoakSeconds
    if ($LASTEXITCODE -ne 0) { throw '界面集成测试失败，详见 tests/runtime/validation.json。' }
    & python (Join-Path $projectRoot 'tests\gui_edge_cases.py')
    if ($LASTEXITCODE -ne 0) { throw '边界测试失败，详见 tests/runtime-extra/validation.json。' }
}

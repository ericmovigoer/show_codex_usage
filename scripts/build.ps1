param(
    [ValidateSet('Debug', 'Release', 'All')][string]$Configuration = 'Release',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw '未找到 VS2022，请安装 C++ 桌面开发和 MFC x64 组件。' }
$vsPath = & $vswhere -latest -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.ATLMFC -property installationPath
if (-not $vsPath) { throw '未找到带 MFC 的 VS2022。请在 Visual Studio Installer 中安装 MFC。' }
$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($config in $configs) {
    & $msbuild (Join-Path $projectRoot 'CodexUsage.sln') /m "/p:Configuration=$config" /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "$config 编译失败。" }
}
if ($Package) {
    $releaseExe = Join-Path $projectRoot 'bin\x64\Release\CodexUsage.exe'
    if (-not (Test-Path -LiteralPath $releaseExe)) { throw '请先编译 Release。' }
    $packageDir = Join-Path $projectRoot 'dist\CodexUsage-x64'
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
    Copy-Item -LiteralPath $releaseExe -Destination $packageDir
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\便携包说明.md') -Destination (Join-Path $packageDir 'README.md')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\LICENSE.json') -Destination (Join-Path $packageDir 'JSON-LICENSE.txt')
    "[Settings]`r`nRefreshIntervalSeconds=3`r`n" | Set-Content -LiteralPath (Join-Path $packageDir 'CodexUsage.ini') -Encoding ASCII
    Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath (Join-Path $projectRoot 'dist\CodexUsage-x64.zip') -Force
    Write-Host "已打包：$(Join-Path $projectRoot 'dist\CodexUsage-x64.zip')"
}

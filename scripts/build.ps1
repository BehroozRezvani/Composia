param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installer 'vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) { throw 'Install Visual Studio C++ desktop build tools and a Windows 10/11 SDK.' }
    $env:PATH = "$installer;$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    $env:VCPKG_ROOT = Join-Path $repoRoot 'external\vcpkg'
    if (!(Test-Path 'external/vcpkg/bootstrap-vcpkg.bat')) {
        & git submodule update --init --recursive
        if ($LASTEXITCODE) { throw 'Submodule initialization failed.' }
    }
    if (!(Test-Path 'external/vcpkg/vcpkg.exe')) {
        & .\external\vcpkg\bootstrap-vcpkg.bat -disableMetrics
        if ($LASTEXITCODE) { throw 'vcpkg bootstrap failed.' }
    }
    & cmake --preset $Preset
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw 'Build failed.' }
    if ($Test) {
        & ctest --preset $Preset
        if ($LASTEXITCODE) { throw 'Desktop smoke tests failed.' }
    }
} finally {
    Pop-Location
}

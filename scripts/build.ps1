param(
    [ValidateSet('debug', 'release', 'coverage')]
    [string]$Preset = 'debug',
    [switch]$Test,
    [ValidateSet('', 'core', 'desktop')]
    [string]$TestLabel = ''
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
        $buildDir = Join-Path $repoRoot "out/build/$Preset"
        $profiles = Join-Path $buildDir 'profiles'
        if ($Preset -eq 'coverage') { Remove-Item $profiles -Recurse -Force -ErrorAction SilentlyContinue }
        $testSuite = if ($TestLabel) { $TestLabel } else { 'all' }
        $resultsPath = Join-Path $buildDir "test-results-$testSuite.xml"
        $testArguments = @('--preset', $Preset, '--output-junit', $resultsPath)
        if ($TestLabel) { $testArguments += @('-L', $TestLabel) }
        & ctest @testArguments
        $testsFailed = $LASTEXITCODE
        if ($Preset -eq 'coverage') {
            # Line, function, and region coverage of the library's own sources, from every test run.
            $llvm = Split-Path (Get-Command clang-cl).Source -Parent
            $data = Join-Path $buildDir 'composia.profdata'
            & (Join-Path $llvm 'llvm-profdata.exe') merge -sparse (Get-ChildItem $profiles -Filter *.profraw).FullName -o $data
            if ($LASTEXITCODE) { throw 'Merging coverage profiles failed.' }
            $binaries = @(Get-ChildItem (Join-Path $buildDir 'tests') -Filter *.exe).FullName
            $objects = @($binaries[0]) + @($binaries | Select-Object -Skip 1 | ForEach-Object { '-object', $_ })
            $filter = @("-instr-profile=$data", '-ignore-filename-regex=[\\/](tests|external|out|Windows Kits|Microsoft Visual Studio)[\\/]')
            $llvmCov = Join-Path $llvm 'llvm-cov.exe'
            & $llvmCov show @objects @filter -format=html "-output-dir=$(Join-Path $buildDir 'coverage-html')"
            $summary = & $llvmCov report @objects @filter
            $summary | Set-Content -Encoding utf8 (Join-Path $buildDir 'coverage.txt')
            $summary
            # A source file no test links is absent from the report rather than shown as uncovered.
            $reported = $summary | ForEach-Object { ($_ -split '\s+')[0] }
            foreach ($file in Get-ChildItem src -Filter *.cpp) {
                if ($reported -notcontains "src\$($file.Name)") { Write-Warning "No test links src\$($file.Name); it is missing from the report." }
            }
            & $llvmCov report @objects @filter -show-functions (Get-ChildItem src, include -Recurse -File).FullName |
                Set-Content -Encoding utf8 (Join-Path $buildDir 'coverage-functions.txt')
            Write-Output "Coverage report: $(Join-Path $buildDir 'coverage-html\index.html')"
        }
        if ($testsFailed) { throw 'Tests failed.' }
    }
} finally {
    Pop-Location
}

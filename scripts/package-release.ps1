param(
    [string]$DebugBuild = "$PSScriptRoot/../out/build/debug",
    [string]$ReleaseBuild = "$PSScriptRoot/../out/build/release",
    [string]$Destination = "$PSScriptRoot/../out/dist"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$version = (Get-Content "$root/vcpkg.json" -Raw | ConvertFrom-Json).version
$stage = Join-Path ([IO.Path]::GetTempPath()) ("composia-release-" + [guid]::NewGuid())
$sdk = Join-Path $stage 'sdk'
$editor = Join-Path $stage 'editor'
New-Item -ItemType Directory -Path $sdk, $editor | Out-Null
try {
    foreach ($entry in @(@('Debug', $DebugBuild), @('Release', $ReleaseBuild))) {
        $config, $build = $entry
        $cache = Get-Content "$build/CMakeCache.txt"
        if ($cache -notcontains "CMAKE_BUILD_TYPE:STRING=$config" -or
            $cache -notcontains "CMAKE_PROJECT_VERSION:STATIC=$version") {
            throw "Expected Composia $version $config at $build"
        }
        & cmake --install $build --prefix $sdk
        if ($LASTEXITCODE) { throw "$config installation failed." }
    }
    foreach ($lib in 'composia', 'composiad', 'composia-controls', 'composia-controlsd') {
        if (!(Test-Path "$sdk/lib/$lib.lib")) { throw "Missing $lib.lib" }
    }
    Copy-Item "$ReleaseBuild/examples/composia-editor.exe", "$root/LICENSE" $editor
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $hashes = foreach ($kind in 'sdk', 'editor') {
        $name = "composia-$version-windows-x64-$kind.zip"
        $archive = Join-Path $stage $name
        [IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $stage $kind), $archive)
        Copy-Item $archive (Join-Path $Destination $name) -Force
        "$((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant())  $name"
    }
    $hashes | Set-Content -Encoding ascii "$Destination/SHA256SUMS"
} finally {
    # Only remove the unique staging directory allocated by this invocation.
    $resolved = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path $resolved -Parent) -eq ([IO.Path]::GetTempPath().TrimEnd('\'))) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

# Run in an x64 Visual Studio developer shell with bootstrapped vcpkg.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$build = "$root/out/port-consumer"
& cmake -S "$root/tests/consumer" -B $build -G 'Ninja Multi-Config' -DCMAKE_CXX_COMPILER=clang-cl `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_OVERLAY_PORTS=$root/ports" `
    "-DVCPKG_MANIFEST_DIR=$root/tests/port" -DVCPKG_MANIFEST_MODE=ON -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DVCPKG_HOST_TRIPLET=x64-windows-static -DEXPECT_CONTROLS=ON
if ($LASTEXITCODE) { throw 'Port configuration failed.' }
foreach ($config in 'Debug', 'Release') {
    & cmake --build $build --config $config
    if ($LASTEXITCODE) { throw "$config consumer build failed." }
    & "$build/$config/consumer.exe"
    if ($LASTEXITCODE) { throw "$config consumer failed." }
}

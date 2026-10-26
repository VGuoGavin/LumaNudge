#requires -Version 5.1
param(
    [string]$Zig = 'zig'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $Zig) {
    $zigExe = (Resolve-Path -LiteralPath $Zig).Path
} else {
    $zigExe = (Get-Command -Name $Zig -ErrorAction Stop).Source
}

$root = Split-Path -Parent $PSCommandPath
$source = Join-Path $root 'src\brightness_sync_native.cpp'
$launcher = Join-Path $root 'src\brightness_sync_launcher.c'
$resource = Join-Path $root 'src\brightness_sync_bundle.rc'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$libraries = @('-ldxva2', '-lwbemuuid', '-lole32', '-loleaut32',
               '-lshell32', '-lshlwapi', '-ladvapi32', '-lsecur32',
               '-luuid', '-luser32')

Push-Location $dist
try {
    Write-Host 'Building x64 native executable...'
    & $zigExe c++ $source -target x86_64-windows-gnu -std=c++17 -Os -s -municode `
        '-Wl,--subsystem,windows' -o 'BrightnessSync-x64.exe' @libraries
    if ($LASTEXITCODE -ne 0) { throw 'x64 compilation failed.' }

    Write-Host 'Building ARM64 native executable...'
    & $zigExe c++ $source -target aarch64-windows-gnu -std=c++17 -Os -s -municode `
        '-Wl,--subsystem,windows' -o 'BrightnessSync-arm64.exe' @libraries
    if ($LASTEXITCODE -ne 0) { throw 'ARM64 compilation failed.' }

    Copy-Item -LiteralPath $resource -Destination '.\brightness_sync_bundle.rc' -Force
    Write-Host 'Embedding both executables in the universal launcher...'
    & $zigExe rc '-:target' x86 'brightness_sync_bundle.rc' 'brightness_sync_bundle.obj'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }

    & $zigExe cc $launcher 'brightness_sync_bundle.obj' -target x86-windows-gnu `
        -Os -s -municode '-Wl,--subsystem,windows' `
        -o 'BrightnessSync-native-universal.exe' -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Universal launcher compilation failed.' }

    Get-Item 'BrightnessSync-x64.exe', 'BrightnessSync-arm64.exe',
             'BrightnessSync-native-universal.exe' |
        Select-Object Name, Length, LastWriteTime
} finally {
    Pop-Location
}

param([Parameter(Mandatory=$true)][string]$MirageRoot)
$ErrorActionPreference = 'Stop'
$taskBridge = Join-Path $MirageRoot 'AstroBoxBridge'
$taskOut = Join-Path $taskBridge 'target\release'
$env:CARGO_BUILD_JOBS = '4'
& (Join-Path $MirageRoot 'Scripts\build-companion-core.ps1')
if ($LASTEXITCODE -ne 0) { throw 'AstroBox companion build failed' }
Push-Location $taskBridge
try {
    # Ask Rust for the actual platform libraries, including windows-targets versions.
    $taskRustOutput = & cargo rustc --release --lib -- --print native-static-libs 2>&1
    $taskRustResult = $LASTEXITCODE
    $taskRustOutput | ForEach-Object { Write-Host $_ }
    if ($taskRustResult -ne 0) { throw 'Cannot determine AstroBox native link libraries' }
    $taskNativeLine = $taskRustOutput | ForEach-Object { "$_" } |
        Where-Object { $_ -match 'native-static-libs: ' } | Select-Object -Last 1
    if (-not $taskNativeLine) { throw 'Rust did not report native-static-libs' }
    $taskNativeLibs = ($taskNativeLine -split 'native-static-libs: ', 2)[1].Trim() -split '\s+'
    $taskSmoke = Join-Path $taskBridge 'target\smoke.exe'
    & cl /nologo /utf-8 /std:c++17 /EHsc /MD (Join-Path $PSScriptRoot 'astrobox-smoke.cpp') `
        "/I$(Join-Path $taskBridge 'include')" "/Fo$(Join-Path $taskOut 'smoke.obj')" "/Fe$taskSmoke" `
        /link "/LIBPATH:$taskOut" mirage_astrobox.lib @taskNativeLibs
    if ($LASTEXITCODE -ne 0) { throw 'Windows AstroBox smoke client link failed' }
    Write-Host "Windows companion and command-line bridge ready: $taskSmoke"
} finally { Pop-Location }

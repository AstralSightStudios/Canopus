# Windows Mirage control; Canopus compilation stays in the existing WSL2 distro.
# Examples: .\scripts\emu\mirage.ps1 doctor
#           .\scripts\emu\mirage.ps1 canopus doctor
#           .\scripts\emu\mirage.ps1 ctl profiles
param(
    [Parameter(Position=0)]
    [ValidateSet('doctor', 'ctl', 'screenshot', 'boot-smoke', 'module-smoke', 'companion-smoke', 'build', 'gui-build', 'gui', 'companion-build', 'facetory-build', 'canopus', 'supervisor')]
    [string]$Action = 'doctor',
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$RemainingArgs
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$taskWorkspace = Split-Path $taskRoot
$taskTools = Join-Path $taskWorkspace '.tools\mirage'
# CLI tests and GUI builds must load the same Swift runtime.
$taskSwiftDirectory = if (Test-Path (Join-Path $taskTools 'swift-gui')) { 'swift-gui' } else { 'swift' }
$taskSwiftRoot = if ($env:SWIFT_ROOT) { $env:SWIFT_ROOT } else {
    Join-Path $taskTools "$taskSwiftDirectory\LocalApp\Programs\Swift"
}
if (Test-Path $taskSwiftRoot) {
    $taskToolchain = Get-ChildItem (Join-Path $taskSwiftRoot 'Toolchains') -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $taskPlatform = Get-ChildItem (Join-Path $taskSwiftRoot 'Platforms') -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($taskToolchain) { $env:PATH = "$($taskToolchain.FullName)\usr\bin;" + $env:PATH }
    if ($taskPlatform) {
        $env:SDKROOT = Join-Path $taskPlatform.FullName 'Windows.platform\Developer\SDKs\Windows.sdk'
    }
    # The portable runtime MSI is unpacked here; installed Swift uses its own PATH.
    if (-not $env:SWIFT_ROOT) {
        $env:PATH = (Join-Path $taskTools $taskSwiftDirectory) + ';' + $env:PATH
    }
}
$env:PYTHONUTF8 = '1'
$taskEmuHome = if ($env:CANOPUS_EMU_HOME) { $env:CANOPUS_EMU_HOME } else { Join-Path $taskRoot '.local\mirage' }
if ($Action -eq 'gui') {
    $taskMirage = if ($env:MIRAGE_ROOT) { $env:MIRAGE_ROOT } else { Join-Path $taskWorkspace 'Mirage' }
    $taskGui = @(
        (Join-Path $taskMirage '.build\debug\Mirage.exe'),
        (Join-Path $taskMirage '.build\x86_64-unknown-windows-msvc\debug\Mirage.exe'),
        (Join-Path $taskMirage '.build\release\Mirage.exe'),
        (Join-Path $taskMirage '.build\x86_64-unknown-windows-msvc\release\Mirage.exe')
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $taskGui) { throw 'Mirage GUI is not built; run mirage.ps1 gui-build first' }
    if (-not $env:MIRAGE_LIBRARY) { $env:MIRAGE_LIBRARY = Join-Path $taskEmuHome 'Library' }
    if (-not $env:MIRAGE_QEMU) {
        foreach ($taskQemuDirectory in @('qemu-fixed', 'qemu')) {
            $taskQemu = Join-Path $taskTools "$taskQemuDirectory\qemu-system-arm.exe"
            if (Test-Path -LiteralPath $taskQemu) { $env:MIRAGE_QEMU = $taskQemu; break }
        }
    }
    $taskLogDir = $taskEmuHome
    New-Item -ItemType Directory -Path $taskLogDir -Force | Out-Null
    $taskProcess = Start-Process -FilePath $taskGui -WorkingDirectory (Split-Path $taskGui) -WindowStyle Normal -PassThru `
        -RedirectStandardOutput (Join-Path $taskLogDir 'gui.stdout.log') `
        -RedirectStandardError (Join-Path $taskLogDir 'gui.stderr.log')
    Write-Host "Mirage GUI started (PID $($taskProcess.Id)); library: $env:MIRAGE_LIBRARY"
    exit 0
}
if ($Action -in @('canopus', 'supervisor')) {
    $taskDistro = if ($env:CANOPUS_WSL_DISTRO) { $env:CANOPUS_WSL_DISTRO } else { 'Ubuntu-24.04' }
    $taskWindowsScript = (Join-Path $PSScriptRoot 'wsl-dev.sh').Replace('\', '/')
    $taskWslOutput = & wsl -d $taskDistro --exec wslpath -u $taskWindowsScript
    if ($LASTEXITCODE -ne 0 -or -not $taskWslOutput) { throw 'Unable to resolve the Canopus path inside WSL' }
    $taskWslScript = ($taskWslOutput | Select-Object -Last 1).Trim()
    & wsl -d $taskDistro --exec bash $taskWslScript $Action @RemainingArgs
    exit $LASTEXITCODE
}
if ($Action -in @('build', 'gui-build', 'companion-build', 'facetory-build')) {
    if ($Action -in @('build', 'gui-build') -and -not (Get-Command swift -ErrorAction SilentlyContinue)) { throw 'Swift 6.1+ is required; set SWIFT_ROOT or install Swift for Windows' }
    $taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $taskVsRoot = if (Test-Path $taskVsWhere) { & $taskVsWhere -latest -products '*' -property installationPath }
    $taskVsDev = if ($taskVsRoot) { Join-Path $taskVsRoot 'Common7\Tools\VsDevCmd.bat' }
    if ($taskVsDev -and (Test-Path $taskVsDev)) {
        $taskEnv = & cmd /d /s /c "`"$taskVsDev`" -no_logo -arch=x64 && set"
        foreach ($taskLine in $taskEnv) {
            if ($taskLine -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
            }
        }
    }
    $taskMirage = if ($env:MIRAGE_ROOT) { $env:MIRAGE_ROOT } else { Join-Path $taskWorkspace 'Mirage' }
    if ($Action -eq 'facetory-build') {
        & cargo build --manifest-path (Join-Path $taskWorkspace 'Facetory\rust\Cargo.toml') --locked --example package_lua_watchface --jobs 4
        exit $LASTEXITCODE
    }
    if ($Action -eq 'companion-build') {
        & (Join-Path $PSScriptRoot 'build-companion-windows.ps1') -MirageRoot $taskMirage
        exit $LASTEXITCODE
    }
    if ($Action -eq 'gui-build') {
        $taskGuiPatch = Join-Path $PSScriptRoot 'mirage-windows-gui.patch'
        & git -c core.autocrlf=true -C $taskMirage apply --reverse --check $taskGuiPatch 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -c core.autocrlf=true -C $taskMirage apply --check $taskGuiPatch
            if ($LASTEXITCODE -ne 0) { throw 'Mirage source does not match the local GUI patch' }
            & git -c core.autocrlf=true -C $taskMirage apply $taskGuiPatch
            if ($LASTEXITCODE -ne 0) { throw 'Cannot apply Mirage Windows GUI patch' }
        }
        if ($RemainingArgs -contains '--preview') {
            $env:MIRAGE_NO_COMPANION = '1'
            $RemainingArgs = @($RemainingArgs | Where-Object { $_ -ne '--preview' })
        } elseif ($env:MIRAGE_NO_COMPANION -ne '1' -and -not (Test-Path (Join-Path $taskMirage 'AstroBoxBridge\target\release\mirage_astrobox.lib'))) {
            & (Join-Path $PSScriptRoot 'build-companion-windows.ps1') -MirageRoot $taskMirage
        }
        $taskGitConfigCount = if ($env:GIT_CONFIG_COUNT) { [int]$env:GIT_CONFIG_COUNT } else { 0 }
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$taskGitConfigCount", 'http.sslBackend', 'Process')
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$taskGitConfigCount", 'openssl', 'Process')
        $env:GIT_CONFIG_COUNT = [string]($taskGitConfigCount + 1)
        & (Join-Path $PSScriptRoot 'prepare-mirage-gui.ps1') -MirageRoot $taskMirage
        $taskWinUI = Join-Path $taskMirage '.build\checkouts\swift-winui'
        & swift package --package-path $taskMirage resolve
        if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve Mirage GUI dependencies' }
        $taskRuntimePatch = Join-Path $PSScriptRoot 'swift-winui-runtime.patch'
        & git -c core.autocrlf=false -C $taskWinUI apply --reverse --check $taskRuntimePatch 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -c core.autocrlf=false -C $taskWinUI apply --check $taskRuntimePatch
            if ($LASTEXITCODE -ne 0) { throw 'Swift WinUI source does not match the runtime compatibility patch' }
            foreach ($taskFile in @('Package.swift', 'Sources\WinAppSDK\Initialize.swift', 'Sources\WinUI\Application\SwiftApplication.swift')) {
                $taskFilePath = Join-Path $taskWinUI $taskFile
                (Get-Item -LiteralPath $taskFilePath).IsReadOnly = $false
            }
            & git -c core.autocrlf=false -C $taskWinUI apply $taskRuntimePatch
            if ($LASTEXITCODE -ne 0) { throw 'Cannot apply Swift WinUI runtime compatibility patch' }
        }
        $taskCrossUI = Join-Path $taskMirage '.build\checkouts\swift-cross-ui'
        $taskCrossUIPatch = Join-Path $PSScriptRoot 'swift-cross-ui-windows.patch'
        & git -c core.autocrlf=true -C $taskCrossUI apply --reverse --check $taskCrossUIPatch 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -c core.autocrlf=true -C $taskCrossUI apply --check $taskCrossUIPatch
            if ($LASTEXITCODE -ne 0) { throw 'SwiftCrossUI source does not match the Windows patch' }
            foreach ($taskFile in @('WinUIBackend.swift', 'WinUIBackend+Alerts.swift', 'WinUIBackend+Sheets.swift')) {
                (Get-Item -LiteralPath (Join-Path $taskCrossUI "Sources\WinUIBackend\$taskFile")).IsReadOnly = $false
            }
            & git -c core.autocrlf=true -C $taskCrossUI apply $taskCrossUIPatch
            if ($LASTEXITCODE -ne 0) { throw 'Cannot apply SwiftCrossUI Windows patch' }
        }
        & swift build --package-path $taskMirage --product Mirage --jobs 4 @RemainingArgs
        $taskBuildResult = $LASTEXITCODE
        if ($taskBuildResult -eq 0) {
            # SwiftPM on Windows can persist machine-local mirror paths in its lock.
            $taskCanonical = ((& git -C $taskMirage show HEAD:Package.resolved) -join "`n") | ConvertFrom-Json
            $taskLockPath = Join-Path $taskMirage 'Package.resolved'
            $taskLockText = [IO.File]::ReadAllText($taskLockPath)
            $taskLock = $taskLockText | ConvertFrom-Json
            foreach ($taskPin in $taskLock.pins) {
                $taskOriginal = $taskCanonical.pins | Where-Object { $_.identity -eq $taskPin.identity }
                if ($taskOriginal -and $taskPin.kind -eq 'localSourceControl') {
                    $taskLockText = $taskLockText.Replace('"localSourceControl"', '"remoteSourceControl"')
                    $taskLockText = $taskLockText.Replace($taskPin.location, $taskOriginal.location)
                }
            }
            [IO.File]::WriteAllText($taskLockPath, ($taskLockText.TrimEnd() + "`n"), [Text.UTF8Encoding]::new($false))
        }
        exit $taskBuildResult
    } else {
        & swift build --package-path (Join-Path $taskMirage 'Packages\MirageKit') --product miragectl @RemainingArgs
    }
    exit $LASTEXITCODE
}
if ($Action -eq 'companion-smoke') {
    & python -u (Join-Path $PSScriptRoot 'companion_smoke.py') @RemainingArgs
} else {
    & python -u (Join-Path $PSScriptRoot 'mirage.py') $Action @RemainingArgs
}
exit $LASTEXITCODE

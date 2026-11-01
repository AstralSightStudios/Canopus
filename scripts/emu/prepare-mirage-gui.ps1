# Seed workspace-local SwiftPM mirrors from Mirage's pinned dependency revisions.
# Shallow fetches avoid downloading every package's full Git history.
param([Parameter(Mandatory=$true)][string]$MirageRoot)
$ErrorActionPreference = 'Stop'
$taskWorkspace = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$taskDependencies = Join-Path $taskWorkspace '.tools\mirage\gui-deps\mirrors'
New-Item -ItemType Directory -Path $taskDependencies -Force | Out-Null
# The patched lock is the single source for pinned revisions and source URLs.
$taskPins = (Get-Content (Join-Path $MirageRoot 'Package.resolved') -Raw | ConvertFrom-Json).pins
$taskMirrorConfig = Join-Path $MirageRoot '.swiftpm\configuration\mirrors.json'
$taskExistingMirrors = if (Test-Path $taskMirrorConfig) { (Get-Content $taskMirrorConfig -Raw | ConvertFrom-Json).object } else { @() }
foreach ($taskPin in $taskPins) {
    $taskRepository = Join-Path $taskDependencies ($taskPin.identity + '.git')
    if (-not (Test-Path -LiteralPath $taskRepository)) {
        & git init --bare $taskRepository | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cannot create mirror for $($taskPin.identity)" }
    }
    & git -C $taskRepository cat-file -e ($taskPin.state.revision + '^{commit}') 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Fetching pinned $($taskPin.identity)..."
        & git -c http.sslBackend=openssl -C $taskRepository fetch --depth 1 $taskPin.location $taskPin.state.revision
        if ($LASTEXITCODE -ne 0) { throw "Cannot fetch $($taskPin.identity)" }
    }
    & git -C $taskRepository update-ref refs/heads/mirage-pin $taskPin.state.revision
    & git -C $taskRepository symbolic-ref HEAD refs/heads/mirage-pin
    if ($taskPin.state.version) {
        & git -C $taskRepository update-ref ('refs/tags/' + $taskPin.state.version) $taskPin.state.revision
    }
    $taskMirrorUrl = ([Uri]$taskRepository).AbsoluteUri
    if (-not ($taskExistingMirrors | Where-Object { $_.original -eq $taskPin.location -and $_.mirror -eq $taskMirrorUrl })) {
        & swift package --package-path $MirageRoot config set-mirror --original $taskPin.location --mirror $taskMirrorUrl
        if ($LASTEXITCODE -ne 0) { throw "Cannot configure mirror for $($taskPin.identity)" }
    }
    # SwiftPM won't import a newly added shallow tag into an existing cache
    # without --update-shallow (for example after changing a toolchain pin).
    if ($taskPin.state.version) {
        $taskCaches = Get-ChildItem (Join-Path $MirageRoot '.build\repositories') -Directory -Filter ($taskPin.identity + '-*') -ErrorAction SilentlyContinue
        foreach ($taskCache in $taskCaches) {
            & git -C $taskCache.FullName show-ref --verify --quiet ('refs/tags/' + $taskPin.state.version)
            if ($LASTEXITCODE -ne 0) {
                & git -C $taskCache.FullName fetch --update-shallow $taskMirrorUrl '+refs/*:refs/*'
                if ($LASTEXITCODE -ne 0) { throw "Cannot refresh cached $($taskPin.identity)" }
            }
        }
    }
}

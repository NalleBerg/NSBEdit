# pack.ps1 — Build a distributable ZIP of NSBEdit
# Usage: .\pack.ps1
# Output: NSBEdit_v<version>.zip  (in the workspace root)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Read version from curver.txt ─────────────────────────────────────────────
$curver = Get-Content -Path "$PSScriptRoot\curver.txt" -Encoding UTF8
$version = ($curver | Where-Object { $_ -match '^Version:\s*(.+)' } |
            Select-Object -First 1) -replace '^Version:\s*', ''
if (-not $version) { throw "Could not read version from curver.txt" }

$zipName = "NSBEdit_v$version.zip"
$stagingDir = "$PSScriptRoot\_pack_staging\NSBEdit"

Write-Host "Packaging NSBEdit v$version..."

# ── Stage files ──────────────────────────────────────────────────────────────
if (Test-Path "$PSScriptRoot\_pack_staging") {
    Remove-Item "$PSScriptRoot\_pack_staging" -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir | Out-Null

$files = @(
    'NSBEdit.exe',    # main executable
    'nsbedit.db',     # SQLite3 stub — presence signals portable mode; installer copies to AppData
    'Changelog.html', # version history (human-readable)
    'GPLv2.md',       # licence
    'Install.bat',    # double-click launcher (opens _doinstall.ps1 in a PowerShell window)
    '_doinstall.ps1', # installer  (run as admin; copies files, shortcuts, registry)
    'Uninstall.ps1'   # uninstaller (also copied to Program Files by installer)
)

foreach ($f in $files) {
    $src = "$PSScriptRoot\$f"
    if (-not (Test-Path $src)) { Write-Warning "  Skipping (not found): $f"; continue }
    Copy-Item $src $stagingDir
    Write-Host "  + $f"
}

# Write version.txt so the installer can read the version without curver.txt
$version | Set-Content (Join-Path $stagingDir 'version.txt') -Encoding UTF8
Write-Host "  + version.txt  ($version)"

# ── Zip ───────────────────────────────────────────────────────────────────────
$zipPath = "$PSScriptRoot\$zipName"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path "$PSScriptRoot\_pack_staging\*" -DestinationPath $zipPath
Remove-Item "$PSScriptRoot\_pack_staging" -Recurse -Force

$sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host ""
Write-Host "  Done: $zipName  ($sizeMB MB)"
Write-Host ""

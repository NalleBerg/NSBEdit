# pack.ps1 — Build a distributable ZIP of NSBEdit
# Usage: .\pack.ps1
# Output: NSBEdit_v<version>.zip  (in .\zip\)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Read version from curver.txt ─────────────────────────────────────────────
$curver = Get-Content -Path "$PSScriptRoot\curver.txt" -Encoding UTF8
$version = ($curver | Where-Object { $_ -match '^Version:\s*(.+)' } |
            Select-Object -First 1) -replace '^Version:\s*', ''
if (-not $version) { throw "Could not read version from curver.txt" }

$timestamp = Get-Date -Format 'HH-mm-ss'
$zipStamp = Get-Date -Format 'yyyyMMdd-HH-mm-ss'
$zipName = "NSBEdit_v${version}_$zipStamp.zip"
$folderName = "NSBEdit-$timestamp"
$stagingRoot = "$PSScriptRoot\_pack_staging"
$stagingDir = Join-Path $stagingRoot $folderName

Write-Host "Packaging NSBEdit v$version..."

# ── Stage files ──────────────────────────────────────────────────────────────
if (Test-Path $stagingRoot) {
    Remove-Item $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir | Out-Null

$files = @(
    'NSBEdit.exe',    # main executable
    'ollama.png',     # AI toolbar/button icon
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

$curlLibSrc = Join-Path $PSScriptRoot 'curl\lib'
if (Test-Path $curlLibSrc) {
    $curlLibDst = Join-Path $stagingDir 'curl\lib'
    New-Item -ItemType Directory -Path $curlLibDst -Force | Out-Null
    Get-ChildItem -Path $curlLibSrc -Filter '*.a' | Copy-Item -Destination $curlLibDst
    Write-Host "  + curl\lib\*.a"
} else {
    Write-Warning '  Skipping curl\lib (not found)'
}

# Write version.txt so the installer can read the version without curver.txt
$version | Set-Content (Join-Path $stagingDir 'version.txt') -Encoding UTF8
Write-Host "  + version.txt  ($version)"

# ── Zip ───────────────────────────────────────────────────────────────────────
$zipDir = "$PSScriptRoot\zip"
if (-not (Test-Path $zipDir)) { New-Item -ItemType Directory -Path $zipDir | Out-Null }
$zipPath = "$zipDir\$zipName"

# Remove only the previous unzipped install folder(s) (named NSBEdit-<timestamp>)
# so the loose install folder is refreshed each build. Any other folder you keep
# in .\zip (e.g. Milestones) is left untouched.
Get-ChildItem -Path $zipDir -Directory -Filter 'NSBEdit-*' | Remove-Item -Recurse -Force
Copy-Item $stagingDir $zipDir -Recurse -Force
Compress-Archive -Path (Join-Path $zipDir $folderName) -DestinationPath $zipPath -Force
Remove-Item $stagingRoot -Recurse -Force

$sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host ""
Write-Host "  Done: $zipName  ($sizeMB MB)"
Write-Host ""

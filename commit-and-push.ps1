
# commit-and-push.ps1
# Automates the version bump, changelog update, build, and git commit/push workflow.

param(
    [string]$PublishedDate = "",
    [string]$Version = "",
    [switch]$SkipBuild,
    [switch]$SkipPush
)

# --- Helper: get last commit message ---
function Get-LastCommitMessage {
    try {
        $msg = git log -1 --pretty=%B 2>$null
        if ($msg) { return $msg.Trim() }
    } catch {}
    return ""
}

# --- Step 1: Run NewVersion.ps1 ---
Write-Host "=== Step 1: Run NewVersion.ps1 ===" -ForegroundColor Cyan
$newVerScript = Join-Path $PSScriptRoot "NewVersion.ps1"
if (Test-Path $newVerScript) {
    & $newVerScript -PublishedDate $PublishedDate -Version $Version
} else {
    Write-Host "NewVersion.ps1 not found. Skipping." -ForegroundColor Yellow
}

# --- Step 2: Read curver.txt ---
$curverPath = Join-Path $PSScriptRoot "curver.txt"
if (Test-Path $curverPath) {
    $curverLines = Get-Content $curverPath
    $publishedLine = $curverLines[0] -replace "^Published:\s*", ""
    $versionLine = $curverLines[1] -replace "^Version:\s*", ""
    Write-Host "Published: $publishedLine" -ForegroundColor Green
    Write-Host "Version: $versionLine" -ForegroundColor Green
} else {
    Write-Host "curver.txt not found. Aborting." -ForegroundColor Red
    exit 1
}

# --- Step 3: Check if version changed since last commit ---
$lastCommitMsg = Get-LastCommitMessage

$versionInLastCommit = $lastCommitMsg -match "v(\d{4}\.\d{2}\.\d{2}\.\d{2})"
$versionChanged = $true
if ($versionInLastCommit -and $Matches) {
    $lastVersion = $Matches[1]
    if ($lastVersion -eq $versionLine) {
        $versionChanged = $false
        Write-Host "Version $versionLine unchanged since last commit. Adding changes under same version." -ForegroundColor Yellow
    }
}

# --- Step 4: Update Changelog.html and CHANGELOG.md ---
Write-Host "`n=== Step 4: Update changelogs ===" -ForegroundColor Cyan
Write-Host "Please update Changelog.html and CHANGELOG.md with the new version and changes."
Write-Host "When done, press Enter to continue..." -ForegroundColor Yellow
Read-Host

# --- Step 5: Update README.md if needed ---
Write-Host "`n=== Step 5: Update README.md ===" -ForegroundColor Cyan
Write-Host "Do you need to update README.md? (y/n)" -ForegroundColor Yellow
$updateReadme = Read-Host
if ($updateReadme -eq "y") {
    Write-Host "Please edit README.md now. Press Enter when done." -ForegroundColor Yellow
    Read-Host
}

# --- Step 6: Check MyStyle/API_list.txt ---
Write-Host "`n=== Step 6: Check MyStyle/API_list.txt ===" -ForegroundColor Cyan
$apiListPath = Join-Path $PSScriptRoot "MyStyle\API_list.txt"
if (Test-Path $apiListPath) {
    Write-Host "API_list.txt found. Check if any files listed need updating."
    Write-Host "Edit API_INTERNALS\API\* and API_INTERNALS\INTERNALS\* as needed."
    Write-Host "Press Enter when done (or type 'skip' to skip):" -ForegroundColor Yellow
    $apiResponse = Read-Host
    if ($apiResponse -eq "skip") {
        Write-Host "Skipping API/INTERNALS update." -ForegroundColor Yellow
    }
} else {
    Write-Host "MyStyle/API_list.txt not found. Skipping." -ForegroundColor Yellow
}

# --- Step 7: Build ---
if (-not $SkipBuild) {
    Write-Host "`n=== Step 7: Build ===" -ForegroundColor Cyan
    $makeitPath = Join-Path $PSScriptRoot "makeit.bat"
    if (Test-Path $makeitPath) {
        & $makeitPath
        if ($LASTEXITCODE -ne 0) {
            Write-Host "Build failed (exit code $LASTEXITCODE). Aborting commit." -ForegroundColor Red
            exit 1
        }
        Write-Host "Build succeeded." -ForegroundColor Green
    } else {
        Write-Host "makeit.bat not found. Skipping build." -ForegroundColor Yellow
    }
} else {
    Write-Host "`n=== Step 7: Build skipped (-SkipBuild) ===" -ForegroundColor Yellow
}

# --- Step 8: Commit and push ---
if (-not $SkipPush) {
    Write-Host "`n=== Step 8: Commit and push ===" -ForegroundColor Cyan
    
    # Build commit message
    $commitMsg = "v$versionLine"
    if (-not $versionChanged) {
        $commitMsg = "$commitMsg (additional changes)"
    }
    
    Write-Host "Suggested commit message: $commitMsg" -ForegroundColor Green
    Write-Host "Enter commit message (or press Enter to use suggested):" -ForegroundColor Yellow
    $userMsg = Read-Host
    if ($userMsg -ne "") {
        $commitMsg = $userMsg
    }
    
    # Stage all changes
    git add -A
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git add failed." -ForegroundColor Red
        exit 1
    }
    
    # Commit
    git commit -m "$commitMsg"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git commit failed." -ForegroundColor Red
        exit 1
    }
    
    # Push
    git push
    if ($LASTEXITCODE -ne 0) {
        Write-Host "git push failed." -ForegroundColor Red
        exit 1
    }
    
    Write-Host "`nCommit and push completed successfully!" -ForegroundColor Green
} else {
    Write-Host "`n=== Step 8: Push skipped (-SkipPush) ===" -ForegroundColor Yellow
}

Write-Host "`nDone." -ForegroundColor Cyan
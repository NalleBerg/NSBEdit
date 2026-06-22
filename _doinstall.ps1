#Requires -Version 5.1
<#
.SYNOPSIS
    NSBEdit Installer
#>

$ErrorActionPreference = 'Stop'

# -- UAC elevation -------------------------------------------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    $psExe  = (Get-Process -Id $PID).MainModule.FileName
    $script = $MyInvocation.MyCommand.Path
    Start-Process -FilePath $psExe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$script`"" -Verb RunAs
    exit
}

try {

# -- Read version -------------------------------------------------------------
$here    = Split-Path $MyInvocation.MyCommand.Path
$version = (Get-Content "$here\version.txt" -Encoding UTF8 -ErrorAction SilentlyContinue | Select-Object -First 1).Trim()
if (-not $version) { $version = 'unknown' }

$installDir = Join-Path $env:ProgramFiles 'NSBEdit'
$appDataDir = Join-Path $env:APPDATA       'NSBEdit'
$aiConfigPath = Join-Path $appDataDir 'ai-settings.json'
$ollamaDownloadUrl = 'https://ollama.com/download/windows'
$defaultAiModel = 'qwen3-coder'
$fallbackAiModel = 'qwen2.5-coder:3b'

function Write-Section($text) {
    Write-Host ""
    Write-Host "  $text"
}

function Test-OllamaAvailable {
    $cmd = Get-Command ollama -ErrorAction SilentlyContinue
    if (-not $cmd) { return $false }
    try {
        & $cmd.Source --version *> $null
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    }
}

function Write-AiConfig([bool]$enabled, [string]$provider, [string]$model, [string]$fallback, [string]$note) {
    $payload = [ordered]@{
        enabled      = $enabled
        provider     = $provider
        model        = $model
        fallback     = $fallback
        note         = $note
        installedAt  = (Get-Date).ToString('s')
        version      = $version
    }
    $payload | ConvertTo-Json -Depth 4 | Set-Content -Path $aiConfigPath -Encoding UTF8
    Write-Host "  + ai-settings.json"
}

function Ensure-OllamaReady {
    if (Test-OllamaAvailable) {
        Write-Host "  + Ollama detected"
        return $true
    }

    Write-Host "  ~ Ollama not found on this machine"
    $wantInstall = Read-Host "  Install or enable Ollama now? [Y/n]"
    if ($wantInstall -match '^[nN]') {
        return $false
    }

    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        Write-Host ""
        Write-Host "  Attempting Ollama installation via winget..."
        try {
            & $winget.Source install --id Ollama.Ollama -e --accept-package-agreements --accept-source-agreements
        } catch {
            Write-Host "  winget reported an error: $_" -ForegroundColor Yellow
        }

        if (Test-OllamaAvailable) {
            Write-Host "  + Ollama is now available"
            return $true
        }

        Write-Host "  winget install did not finish with a working Ollama install."
    } else {
        Write-Host "  winget is not available on this machine."
    }

    Write-Host ""
    Write-Host "  Manual fallback: open the official Ollama download page below"
    Write-Host "  and install it by hand if you prefer or if winget fails."
    Write-Host "  $ollamaDownloadUrl"
    $openPage = Read-Host "  Open the download page now? [Y/n]"
    if ($openPage -notmatch '^[nN]') {
        try {
            Start-Process $ollamaDownloadUrl
        } catch {
            Write-Host "  Could not open the browser automatically. Copy the URL above."
        }
    }

    Read-Host "  Install Ollama, then press Enter here to retry detection"
    if (Test-OllamaAvailable) {
        Write-Host "  + Ollama is now available"
        return $true
    }

    Write-Host "  ~ Ollama still not detected; continuing without local AI"
    return $false
}

function Install-OllamaModel([string]$modelName) {
    if (-not $modelName) { return $false }

    $cmd = Get-Command ollama -ErrorAction SilentlyContinue
    if (-not $cmd) { return $false }

    Write-Host "  ~ pulling Ollama model: $modelName"
    try {
        & $cmd.Source pull $modelName
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  + model ready: $modelName"
            return $true
        }
        Write-Host "  ! Ollama pull returned exit code $LASTEXITCODE for $modelName" -ForegroundColor Yellow
    } catch {
        Write-Host "  ! Ollama pull failed for $modelName : $_" -ForegroundColor Yellow
    }

    return $false
}

Write-Host ""
Write-Host "  NSBEdit v$version -- Installer"
Write-Host "  -------------------------------------------------"
Write-Host "  Program Files : $installDir"
Write-Host "  AppData       : $appDataDir"
Write-Host ""

# -- Create directories --------------------------------------------------------
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null

# -- Database migration --------------------------------------------------------
$dbSrc = Join-Path $here 'nsbedit.db'
$dbDst = Join-Path $appDataDir 'nsbedit.db'
if (Test-Path $dbSrc) {
    if (-not (Test-Path $dbDst)) {
        Copy-Item $dbSrc $dbDst -Force
        Write-Host "  + nsbedit.db -> AppData  (imported from installer folder)"
    } else {
        Write-Host "  ~ nsbedit.db already in AppData -- user data preserved"
    }
} else {
    Write-Host "  ~ no nsbedit.db in installer folder -- AppData DB will be created on first run"
}

Write-Section "AI setup"
$ollamaReady = Ensure-OllamaReady
if ($ollamaReady) {
    $defaultModelReady  = Install-OllamaModel $defaultAiModel
    $fallbackModelReady  = Install-OllamaModel $fallbackAiModel
    Write-AiConfig $true 'ollama' $defaultAiModel $fallbackAiModel 'Local AI configured during install'
    if (-not $defaultModelReady -or -not $fallbackModelReady) {
        Write-Host "  ~ one or more AI models could not be pulled during install"
    }
} else {
    Write-AiConfig $false 'none' $defaultAiModel $fallbackAiModel 'Local AI not available during install'
}

# -- Copy program files --------------------------------------------------------
Write-Section "Copy program files"
foreach ($f in @('NSBEdit.exe','ollama.png','Changelog.html','GPLv2.md')) {
    $src = Join-Path $here $f
    if (Test-Path $src) {
        Copy-Item $src $installDir -Force
        Write-Host "  + $f"
    } else {
        Write-Warning "  Skipping (not found): $f"
    }
}

$curlLibSrc = Join-Path $here 'curl\lib'
if (Test-Path $curlLibSrc) {
    $curlLibDst = Join-Path $installDir 'curl\lib'
    New-Item -ItemType Directory -Path $curlLibDst -Force | Out-Null
    Get-ChildItem -Path $curlLibSrc -Filter '*.a' | Copy-Item -Destination $curlLibDst -Force
    Write-Host "  + curl\lib\*.a"
} else {
    Write-Warning '  Skipping curl\lib (not found)'
}

# -- Copy Uninstall.ps1 into Program Files -------------------------------------
Copy-Item (Join-Path $here 'Uninstall.ps1') $installDir -Force
Write-Host "  + Uninstall.ps1"

# -- Shortcuts (current user) --------------------------------------------------
Write-Section "Create shortcuts"
$exePath   = Join-Path $installDir 'NSBEdit.exe'
$desk      = [Environment]::GetFolderPath('Desktop')
$startMenu = [Environment]::GetFolderPath('StartMenu')
$programs  = [Environment]::GetFolderPath('Programs')
$shell     = New-Object -ComObject WScript.Shell

function New-Lnk ($lnkPath, $target, $desc) {
    $dir = Split-Path $lnkPath
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $sc = $shell.CreateShortcut($lnkPath)
    $sc.TargetPath       = $target
    $sc.WorkingDirectory = Split-Path $target
    $sc.Description      = $desc
    $sc.IconLocation     = "$target,0"
    $sc.Save()
    Write-Host "  + Shortcut: $lnkPath"
}

Write-Host ""
New-Lnk (Join-Path $desk      'NSBEdit.lnk') $exePath 'NSBEdit RTF Notepad'
New-Lnk (Join-Path $startMenu 'NSBEdit.lnk') $exePath 'NSBEdit RTF Notepad'
New-Lnk (Join-Path $programs  'NSBEdit.lnk') $exePath 'NSBEdit RTF Notepad'

# -- Registry uninstall entry --------------------------------------------------
Write-Section "Write uninstall registry entry"
$regKey    = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NSBEdit'
$uninstPs  = Join-Path $installDir 'Uninstall.ps1'
$uninstCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstPs`""

New-Item -Path $regKey -Force | Out-Null
$strProps = [ordered]@{
    DisplayName     = 'NSBEdit'
    DisplayVersion  = $version
    Publisher       = 'NalleBerg'
    InstallLocation = $installDir
    DisplayIcon     = "$exePath,0"
    UninstallString = $uninstCmd
    URLInfoAbout    = 'https://github.com/NalleBerg/NSBdit'
}
foreach ($kv in $strProps.GetEnumerator()) {
    New-ItemProperty -Path $regKey -Name $kv.Key -Value $kv.Value -PropertyType String -Force | Out-Null
}
$dwordProps = [ordered]@{
    EstimatedSize = [int]([math]::Ceiling((Get-Item $exePath).Length / 1KB))
    NoModify      = 1
    NoRepair      = 1
}
foreach ($kv in $dwordProps.GetEnumerator()) {
    New-ItemProperty -Path $regKey -Name $kv.Key -Value $kv.Value -PropertyType DWord -Force | Out-Null
}

Write-Host ""
Write-Host "  + Registry uninstall entry written"
Write-Host ""
Write-Host "  =================================================="
Write-Host "  NSBEdit v$version installed successfully."
Write-Host "  Launch from the Desktop or Start Menu shortcut."
Write-Host "  =================================================="
Write-Host ""

} catch {
    Write-Host ""
    Write-Host "  ERROR: $_" -ForegroundColor Red
    Write-Host ""
}

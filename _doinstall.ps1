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

Write-Host ""
Write-Host "  NSBEdit v$version -- Installer"
Write-Host "  -------------------------------------------------"
Write-Host "  Program Files : $installDir"
Write-Host "  AppData       : $appDataDir"
Write-Host ""

# -- Create directories --------------------------------------------------------
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null

# -- Copy program files --------------------------------------------------------
foreach ($f in @('NSBEdit.exe','Changelog.html','GPLv2.md')) {
    $src = Join-Path $here $f
    if (Test-Path $src) {
        Copy-Item $src $installDir -Force
        Write-Host "  + $f"
    } else {
        Write-Warning "  Skipping (not found): $f"
    }
}

# -- Copy Uninstall.ps1 into Program Files -------------------------------------
Copy-Item (Join-Path $here 'Uninstall.ps1') $installDir -Force
Write-Host "  + Uninstall.ps1"

# -- Copy database to AppData -- only on fresh install (preserve user data) ----
$dbSrc = Join-Path $here 'nsbedit.db'
$dbDst = Join-Path $appDataDir 'nsbedit.db'
if (Test-Path $dbSrc) {
    if (-not (Test-Path $dbDst)) {
        Copy-Item $dbSrc $dbDst -Force
        Write-Host "  + nsbedit.db -> AppData  (fresh install)"
    } else {
        Write-Host "  ~ nsbedit.db already in AppData -- user data preserved"
    }
}

# -- Shortcuts (current user) --------------------------------------------------
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
    Set-ItemProperty -Path $regKey -Name $kv.Key -Value $kv.Value -Type String
}
$dwordProps = [ordered]@{
    EstimatedSize = [int]([math]::Ceiling((Get-Item $exePath).Length / 1KB))
    NoModify      = 1
    NoRepair      = 1
}
foreach ($kv in $dwordProps.GetEnumerator()) {
    Set-ItemProperty -Path $regKey -Name $kv.Key -Value $kv.Value -Type DWord
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

Read-Host "  Press Enter to close"

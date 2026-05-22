#Requires -Version 5.1
<#
.SYNOPSIS
    NSBEdit Uninstaller
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

$installDir = Join-Path $env:ProgramFiles 'NSBEdit'

Write-Host ""
Write-Host "  NSBEdit -- Uninstaller"
Write-Host "  -------------------------------------------------"
Write-Host ""

# -- Confirm -------------------------------------------------------------------
$confirm = Read-Host "  Remove NSBEdit from this computer? [y/N]"
if ($confirm -notmatch '^[yY]') {
    Write-Host "  Cancelled."
    Read-Host "  Press Enter to close"
    exit
}

try {

# -- Remove shortcuts ----------------------------------------------------------
$desk      = [Environment]::GetFolderPath('Desktop')
$startMenu = [Environment]::GetFolderPath('StartMenu')
$programs  = [Environment]::GetFolderPath('Programs')

foreach ($lnk in @(
    (Join-Path $desk      'NSBEdit.lnk'),
    (Join-Path $startMenu 'NSBEdit.lnk'),
    (Join-Path $programs  'NSBEdit.lnk')
)) {
    if (Test-Path $lnk) {
        Remove-Item $lnk -Force
        Write-Host "  - Removed shortcut: $lnk"
    }
}

# -- Remove registry uninstall entry -------------------------------------------
$regKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NSBEdit'
if (Test-Path $regKey) {
    Remove-Item $regKey -Force
    Write-Host "  - Removed registry entry"
}

# -- Remove Program Files folder -----------------------------------------------
# Schedule via cmd.exe because this script lives inside $installDir.
if (Test-Path $installDir) {
    $cmd = "ping -n 2 127.0.0.1 >nul & rd /s /q `"$installDir`""
    Start-Process -FilePath 'cmd.exe' -ArgumentList '/c',$cmd -WindowStyle Hidden
    Write-Host "  - Scheduled removal: $installDir"
}

Write-Host ""
Write-Host "  NSBEdit has been uninstalled."
Write-Host "  (Your data in AppData\NSBEdit was not removed.)"
Write-Host ""

} catch {
    Write-Host ""
    Write-Host "  ERROR: $_" -ForegroundColor Red
    Write-Host ""
}

Read-Host "  Press Enter to close"

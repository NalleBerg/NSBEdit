$scriptDir      = Split-Path -Parent $MyInvocation.MyCommand.Path
$log            = Join-Path $scriptDir 'makeit.log'
$countFile      = Join-Path $scriptDir 'makeit_count.txt'
$countTodayFile = Join-Path $scriptDir 'makeit_today.txt'
$pos            = 0

# ── Load persistent counters ───────────────────────────────────────────────
# makeit_count.txt  — all-time build number, never reset
# makeit_today.txt  — append log; one line per build: "yyyy-MM-dd HH:mm:ss  #N"
#                     today's count = lines starting with today's date
$run         = if (Test-Path $countFile) { [int](Get-Content $countFile -Raw).Trim() } else { 0 }
$today       = (Get-Date).ToString('yyyy-MM-dd')
$runToday    = 0
$sessionRuns = 0

if (Test-Path $countTodayFile) {
    $runToday = @(Get-Content $countTodayFile | Where-Object { $_ -match "^$today" }).Count
}

# ── Helpers ───────────────────────────────────────────────────────────────────
function Write-Sep($char, $color) {
    Write-Host (([string]$char) * 62) -ForegroundColor $color
}

function Write-ProgressBar {
    param([int]$step, [int]$total, [string]$label)
    $pct    = [int]($step * 100.0 / $total)
    $width  = 30
    $filled = [int]($width * $step / $total)
    $empty  = $width - $filled
    $bar    = ([string][char]0x2588) * $filled + ([string][char]0x2591) * $empty
    Write-Host ''
    Write-Sep ([char]0x2500) DarkGray
    Write-Host '  ' -NoNewline
    Write-Host "Step $step/$total" -NoNewline -ForegroundColor DarkGray
    Write-Host '  ' -NoNewline
    Write-Host $bar -NoNewline -ForegroundColor Cyan
    Write-Host "  $pct%" -NoNewline -ForegroundColor White
    Write-Host "  $label" -ForegroundColor Yellow
    Write-Sep ([char]0x2500) DarkGray
}

function Write-StepDone {
    Write-Host '  + Done' -ForegroundColor Green
    Write-Host ''
}

function Write-TimerBanner {
    param([string]$secs)
    Write-Host ''
    Write-Sep ([char]0x2550) Green
    Write-Host "  BUILD SUCCEEDED" -NoNewline -ForegroundColor Green
    Write-Host "   --  $secs seconds total" -ForegroundColor Cyan
    Write-Sep ([char]0x2550) Green
    Write-Host ''
}

# ── Line renderer ─────────────────────────────────────────────────────────────
function Write-ColoredLine($line) {
    # Structured markers
    if ($line -match '^\[STEP (\d+)/(\d+)\] (.+)') {
        Write-ProgressBar ([int]$Matches[1]) ([int]$Matches[2]) $Matches[3]; return
    }
    if ($line -match '^\[STEP_DONE') { Write-StepDone; return }
    if ($line -match '^\[BUILD_TIME: ([\d.]+) s\]') { Write-TimerBanner $Matches[1]; return }

    # Compiler diagnostics
    if ($line -match '\berror:')   { Write-Host "  ! $line" -ForegroundColor Red;    return }
    if ($line -match '\bwarning:') { Write-Host "  ? $line" -ForegroundColor Yellow; return }
    if ($line -match '\bnote:')    { Write-Host $line -ForegroundColor DarkCyan;     return }

    # Build-script messages
    if ($line -match '\[ERROR\]|FAILED')  { Write-Host "  ! $line" -ForegroundColor Red;      return }
    if ($line -match 'BUILD SUCCEEDED')   { return }   # swallowed -- replaced by timer banner
    if ($line -match '^={3,}')            { Write-Host $line -ForegroundColor DarkGray;        return }
    if ($line -match 'NSBEdit Build')     { Write-Host $line -ForegroundColor Cyan;            return }
    if ($line -match '^\s+kept:')         { Write-Host $line -ForegroundColor Green;           return }
    if ($line -match '\s+\+\s')           { Write-Host $line -ForegroundColor DarkGreen;       return }
    if ($line -match 'Packaging NSBEdit') { Write-Host $line -ForegroundColor Cyan;            return }
    if ($line -match 'Done:.*\.zip')      { Write-Host $line -ForegroundColor Green;           return }
    if ($line -match '(OK|killed|not running)$') { Write-Host $line -ForegroundColor Green;   return }

    # Noisy compiler verbose paths -- suppress
    if ($line -match '^[A-Z]:\\.*>')                 { return }
    if ($line -match '^(COLLECT|COMPILER|Target|Configured with|Thread model|gcc version)') { return }

    Write-Host $line -ForegroundColor DarkGray
}

# ── Main loop ─────────────────────────────────────────────────────────────────
Write-Host "  Watching $log  --  Ctrl+C to stop" -ForegroundColor DarkGray
Write-Host ''

while ($true) {
    if (Test-Path $log) {
        $info = Get-Item $log
        $size = $info.Length

        if ($size -lt $pos -or ($sessionRuns -eq 0 -and $size -gt 0)) {
            $now   = Get-Date
            $today = $now.ToString('yyyy-MM-dd')
            $runToday = 0
            if (Test-Path $countTodayFile) {
                $runToday = @(Get-Content $countTodayFile | Where-Object { $_ -match "^$today" }).Count
            }
            $run++
            $runToday++
            $sessionRuns++
            Set-Content $countFile $run
            $entry = "{0}  #{1}" -f $now.ToString('yyyy-MM-dd HH:mm:ss'), $run
            Add-Content $countTodayFile $entry
            $pos = 0
            Clear-Host
            Write-Sep ([char]0x2550) Cyan
            Write-Host ("  BUILD RUN {0}   ({1} today)   {2}" -f $run, $runToday,
                $now.ToString('yyyy-MM-dd  HH:mm:ss')) -ForegroundColor Cyan
            Write-Host '  NSBEdit  —  MinGW / CMake' -ForegroundColor Cyan
            Write-Sep ([char]0x2550) Cyan
            Write-Host ''
        }

        if ($size -gt $pos) {
            try {
                $fs = [IO.File]::Open($log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $fs.Position = $pos
                $sr = [IO.StreamReader]::new($fs)
                $text = $sr.ReadToEnd()
                $sr.Close()
                $fs.Close()
                if ($text) {
                    $pos = $size   # advance before rendering so an error never re-reads the same block
                    foreach ($line in ($text -split "`r?`n")) {
                        if ($line -ne '') { Write-ColoredLine $line }
                    }
                    $pos = $size
                }
            } catch { }
        }
    }
    Start-Sleep -Milliseconds 150
}

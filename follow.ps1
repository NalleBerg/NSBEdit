$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$log        = Join-Path $scriptDir 'makeit.log'
$countFile  = Join-Path $scriptDir 'makeit_count.txt'
$pos  = 0

# Load persistent run counter; start at 0 if the file doesn't exist yet.
$run = if (Test-Path $countFile) { [int](Get-Content $countFile -Raw).Trim() } else { 0 }

function Write-ColoredLine($line) {
    if     ($line -match 'error:')           { Write-Host $line -ForegroundColor Red }
    elseif ($line -match 'warning:')         { Write-Host $line -ForegroundColor Yellow }
    elseif ($line -match 'note:')            { Write-Host $line -ForegroundColor DarkCyan }
    elseif ($line -match 'BUILD SUCCEEDED')  { Write-Host $line -ForegroundColor Green }
    elseif ($line -match '\[ERROR\]|FAILED') { Write-Host $line -ForegroundColor Red }
    elseif ($line -match 'Done\.')           { Write-Host $line -ForegroundColor Green }
    elseif ($line -match '^\[\d+/\d+\]')    { Write-Host $line -ForegroundColor White }
    elseif ($line -match '^[A-Z]:\\.*>')    { Write-Host $line -ForegroundColor DarkGray }
    else                                     { Write-Host $line }
}

Write-Host "Watching $log  (Ctrl+C to stop)" -ForegroundColor DarkGray

while ($true) {
    if (Test-Path $log) {
        $info = Get-Item $log
        $size = $info.Length

        # New run: file was truncated (tee overwrote it for a new build)
        if ($size -lt $pos) {
            $run++
            Set-Content $countFile $run   # persist across restarts
            $pos = 0
            Clear-Host
            Write-Host ("=" * 60) -ForegroundColor Cyan
            Write-Host ("  RUN #$run   " + $info.LastWriteTime.ToString("« yyyy-MM-dd HH:mm:ss»")) -ForegroundColor Cyan
            Write-Host ("=" * 60) -ForegroundColor Cyan
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

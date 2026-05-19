Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipPath = "NSBEdit_v2026.05.18.12.zip"
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Update')
$wrong = $zip.GetEntry("NSBEdit.exe")
if ($wrong) { $wrong.Delete(); Write-Host "Removed root NSBEdit.exe" }
[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
    $zip, "NSBEdit.exe", "NSBEdit/NSBEdit.exe",
    [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
Write-Host "Added NSBEdit/NSBEdit.exe"
$zip.Entries | Select-Object FullName
$zip.Dispose()

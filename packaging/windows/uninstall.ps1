# Remove Tally (your budget in %APPDATA%\Tally is kept).
$dest = Join-Path $env:LOCALAPPDATA 'Programs\Tally'
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
foreach ($folder in @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))) {
    Remove-Item (Join-Path $folder 'Tally.lnk') -ErrorAction SilentlyContinue
}
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($userPath) {
    [Environment]::SetEnvironmentVariable('Path', (($userPath -split ';') | Where-Object { $_ -and $_ -ne $dest }) -join ';', 'User')
}
Write-Host 'Tally is uninstalled. Your budget is still in' (Join-Path $env:APPDATA 'Tally')

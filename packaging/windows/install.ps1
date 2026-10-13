# Install Tally for the current user. No admin rights needed.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $env:LOCALAPPDATA 'Programs\Tally'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $here 'tally.exe') $dest -Force

# Let you type "tally" in a new terminal window
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not $userPath) { $userPath = '' }
if (($userPath -split ';') -notcontains $dest) {
    [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $dest).TrimStart(';'), 'User')
}

# Start Menu and Desktop shortcuts. Windows Terminal is used if you have it (better colors).
$shell = New-Object -ComObject WScript.Shell
$wt = Get-Command wt.exe -ErrorAction SilentlyContinue
foreach ($folder in @([Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))) {
    $lnk = $shell.CreateShortcut((Join-Path $folder 'Tally.lnk'))
    if ($wt) { $lnk.TargetPath = $wt.Source; $lnk.Arguments = '--title Tally "' + (Join-Path $dest 'tally.exe') + '"' }
    else { $lnk.TargetPath = Join-Path $dest 'tally.exe' }
    $lnk.WorkingDirectory = $env:USERPROFILE
    $lnk.Description = 'Private budget planner'
    $lnk.Save()
}
Write-Host ''
Write-Host 'Tally is installed.' -ForegroundColor Green
Write-Host 'Open it from the Start Menu or the Desktop shortcut, or type  tally  in a NEW terminal window.'

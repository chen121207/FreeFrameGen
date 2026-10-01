[CmdletBinding()]
param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$meta=[IO.File]::ReadAllText((Join-Path $repo 'installer/product.json')) | ConvertFrom-Json
$Installer=(Resolve-Path -LiteralPath $Installer).Path
$regPath='Software\Microsoft\Windows\CurrentVersion\Uninstall\'+$meta.id
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$existing=$registry.OpenSubKey($regPath)
if($existing){$existing.Dispose();$registry.Dispose();throw 'Product is already installed; will not touch the user installation'}
$group=Join-Path ([Environment]::GetFolderPath('Programs')) $meta.folder
if(Test-Path -LiteralPath $group){$registry.Dispose();throw 'Existing Start menu folder is preserved; resolve it before running the installer test'}
$work=Join-Path $repo ('build\installer-tests\'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $work -Force)
$root=Join-Path (Join-Path $work 'Unicode 路径 with spaces') $meta.folder
function RunSetup([string[]]$Arguments,[bool]$ExpectSuccess=$true,[string]$Exe=$Installer){
    $p=Start-Process -FilePath $Exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru -Wait
    if(($p.ExitCode -eq 0) -ne $ExpectSuccess){throw "Unexpected exit code $($p.ExitCode): $Arguments"}
}
function Quoted([string]$s){return '"'+$s+'"'}
function AssertNoRegistration{
    $key=$registry.OpenSubKey($regPath)
    if($key){$key.Dispose();throw 'Unexpected uninstall registration'}
}
# Render actual WinForms controls offscreen; no desktop automation.
RunSetup @('--preview',(Quoted (Join-Path $work 'setup-preview.png')))
$forbidden=Join-Path $work ('steamapps\common\'+$meta.folder)
RunSetup @('--silent','--install','--dir',(Quoted $forbidden),'--log',(Quoted (Join-Path $work 'reject-game.log'))) $false
if(Test-Path -LiteralPath $forbidden){throw 'Game-path safeguard failed'}
AssertNoRegistration
[void](New-Item -ItemType Directory -Path $root -Force)
$sentinel=Join-Path $root 'existing-user-file.txt'
[IO.File]::WriteAllText($sentinel,'unchanged')
RunSetup @('--silent','--install','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-existing.log'))) $false
if([IO.File]::ReadAllText($sentinel) -ne 'unchanged'){throw 'Existing user file changed'}
Remove-Item -LiteralPath $sentinel
Remove-Item -LiteralPath $root
RunSetup @('--silent','--install','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'install.log')))
$key=$registry.OpenSubKey($regPath)
if(-not $key){throw 'Missing installed-apps registration'}
try{if($key.GetValue('InstallLocation') -ne $root){throw 'Wrong install location'}}finally{$key.Dispose()}
$installedExe=Join-Path $root ('bin\'+$meta.exe)
if(-not(Test-Path -LiteralPath $installedExe)){throw 'Missing installed program'}
if(@(Get-ChildItem -LiteralPath $group -Filter '*.lnk').Count -ne 2){throw 'Start menu links missing'}
RunSetup @('--help') $true $installedExe
RunSetup @($meta.testArgs) $true (Join-Path $root ('bin\'+$meta.testExe))
foreach($link in (Get-ChildItem -LiteralPath $group -Filter '*.lnk')){
    $shell=New-Object -ComObject WScript.Shell
    $shortcut=$shell.CreateShortcut($link.FullName)
    if($shortcut.TargetPath -eq $installedExe -and $shortcut.Arguments -ne $meta.launchArgs){throw 'Wrong capture launch arguments'}
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut)
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
}
RunSetup @('--silent','--install','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-reinstall.log'))) $false
$readme=Join-Path $root 'README.md'
$bytes=[IO.File]::ReadAllBytes($readme)
[IO.File]::AppendAllText($readme,'modified by safety test')
RunSetup @('--silent','--uninstall','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-modified.log'))) $false
if(-not(Test-Path -LiteralPath $installedExe)){throw 'Modified-file preflight removed files'}
[IO.File]::WriteAllBytes($readme,$bytes)
$state=Join-Path $root 'install-state.xml'
$originalState=[IO.File]::ReadAllBytes($state)
[IO.File]::AppendAllText($state,'tamper')
RunSetup @('--silent','--uninstall','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-state.log'))) $false
[IO.File]::WriteAllBytes($state,$originalState)
$locked=[IO.File]::Open($readme,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{RunSetup @('--silent','--uninstall','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-in-use.log'))) $false}
finally{$locked.Dispose()}
$attributes=[IO.File]::GetAttributes($readme)
[IO.File]::SetAttributes($readme,$attributes -bor [IO.FileAttributes]::ReadOnly)
try{RunSetup @('--silent','--uninstall','--dir',(Quoted $root),'--log',(Quoted (Join-Path $work 'reject-readonly.log'))) $false}
finally{[IO.File]::SetAttributes($readme,$attributes)}
$extra=Join-Path $root 'user-notes.txt'
[IO.File]::WriteAllText($extra,'keep user data')
$links=@(Get-ChildItem -LiteralPath $group -Filter '*.lnk')
$shell=New-Object -ComObject WScript.Shell
$changedLink=$null
foreach($item in $links){
    $shortcut=$shell.CreateShortcut($item.FullName)
    if($shortcut.TargetPath -eq $installedExe){
        $shortcut.Arguments='--help'
        $shortcut.Save()
        $changedLink=$item.FullName
    }else{
        # Metadata-only byte change: trailing padding after the Shell Link terminal block.
        # The COM properties must remain readable; uninstaller should not orphan this link.
        $stream=[IO.File]::Open($item.FullName,[IO.FileMode]::Append,[IO.FileAccess]::Write)
        try{$stream.WriteByte(0)}finally{$stream.Dispose()}
    }
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut)
}
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
if(-not $changedLink){throw 'Did not find main shortcut for preservation test'}
$uninstallLog=Join-Path $work 'uninstall.log'
# Exercise the installed EXE, including its temporary-worker/self-removal route.
RunSetup @('--silent','--log',(Quoted $uninstallLog)) $true (Join-Path $root 'Uninstall.exe')
$timer=[Diagnostics.Stopwatch]::StartNew()
do{
    if(Test-Path -LiteralPath $uninstallLog){
        $text=[IO.File]::ReadAllText($uninstallLog)
        if($text -match 'RESULT=FAILED'){throw $text}
        if($text -match 'RESULT=SUCCESS'){break}
    }
    Start-Sleep -Milliseconds 100
}while($timer.Elapsed.TotalSeconds -lt 30)
if($text -notmatch 'RESULT=SUCCESS'){throw 'Uninstall worker timed out'}
AssertNoRegistration
if(Test-Path -LiteralPath $installedExe){throw 'Program not removed'}
if(Test-Path -LiteralPath (Join-Path $root 'Uninstall.exe')){throw 'Uninstaller not removed'}
if(-not(Test-Path -LiteralPath $changedLink)){throw 'Customized shortcut was lost'}
if(@(Get-ChildItem -LiteralPath $group -Force).Count -ne 1){throw 'Metadata-only uninstall shortcut was left behind'}
Remove-Item -LiteralPath $changedLink
Remove-Item -LiteralPath $group
if([IO.File]::ReadAllText($extra) -ne 'keep user data'){throw 'Unlisted file lost'}
Remove-Item -LiteralPath $extra
Remove-Item -LiteralPath $root
$registry.Dispose()
Write-Output "PASS: setup, actual installed uninstaller, registration, metadata-only shortcut cleanup/customized shortcut preservation, game/existing/reinstall/modified/state/in-use/readonly safeguards, user-data retention."
Write-Output "Logs and GUI preview: $work"

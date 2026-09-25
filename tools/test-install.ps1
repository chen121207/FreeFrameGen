param([Parameter(Mandatory)][string]$Package)
$ErrorActionPreference='Stop'
$script=Join-Path $PSScriptRoot 'manage-portable.ps1'
$product='FreeFrameGen'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('ffg-install-test-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temp)
$dest=Join-Path $temp $product
& $script -Action Install -Destination $dest -Package $Package
& $script -Action Uninstall -Destination $dest
if(Test-Path -LiteralPath $dest){throw 'Clean uninstall failed'}
& $script -Action Install -Destination $dest -Package $Package
$file=Join-Path $dest 'product.txt'
[IO.File]::AppendAllText($file,'changed')
$blocked=$false
try{& $script -Action Uninstall -Destination $dest}catch{$blocked=$true}
if(-not $blocked -or -not (Test-Path -LiteralPath $file)){throw 'Modified-file safeguard failed'}
Copy-Item -LiteralPath (Join-Path $Package 'product.txt') -Destination $file
# A malicious manifest must not escape the destination.
$manifestPath=Join-Path $dest 'install-manifest.json'
$original=[IO.File]::ReadAllText($manifestPath)
$m=$original | ConvertFrom-Json
$m.files[0].path='..\outside.txt'
$m | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$blocked=$false
try{& $script -Action Uninstall -Destination $dest}catch{$blocked=$true}
if(-not $blocked){throw 'Traversal safeguard failed'}
[IO.File]::WriteAllText($manifestPath,$original)
$userFile=Join-Path $dest 'user-notes.txt'
[IO.File]::WriteAllText($userFile,'unlisted user data')
& $script -Action Uninstall -Destination $dest
if(-not (Test-Path -LiteralPath $userFile)){throw 'Unlisted user file was removed'}
if((Get-Content -LiteralPath $userFile -Raw) -ne 'unlisted user data'){throw 'Unlisted user file changed'}
# Remove only the exact fixture file and its now-empty directory.
Remove-Item -LiteralPath $userFile
Remove-Item -LiteralPath $dest
# This exact test directory is empty; never recurse.
Remove-Item -LiteralPath $temp
Write-Output 'PASS: install/uninstall, modified/unlisted-file preservation, traversal rejection'

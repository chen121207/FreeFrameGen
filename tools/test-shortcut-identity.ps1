$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ('build\shortcut-contract-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $work)
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$exe=Join-Path $work 'ShortcutIdentityTests.exe'
& $compiler /nologo /target:exe /platform:x64 /warn:4 /warnaserror+ /codepage:65001 /main:ShortcutIdentityTests `
    /reference:System.Windows.Forms.dll /reference:System.Drawing.dll `
    /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll `
    ("/out:"+$exe) (Join-Path $repo 'installer/Setup.cs') (Join-Path $repo 'installer/tests/ShortcutIdentity.cs')
if($LASTEXITCODE){throw 'Shortcut test compilation failed'}
& $exe (Join-Path $work 'fixtures')
if($LASTEXITCODE){throw 'Shortcut identity test failed'}

[CmdletBinding()]
param([string]$CMake,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$meta=[IO.File]::ReadAllText((Join-Path $repo 'installer/product.json')) | ConvertFrom-Json
$icon=Join-Path $repo 'assets\ffg.ico'
if(-not (Test-Path -LiteralPath $icon)){throw 'Application icon not found: assets\ffg.ico'}
if(-not $CMake){
    $command=Get-Command cmake.exe -ErrorAction SilentlyContinue
    if($command){$CMake=$command.Source}
    else {
        $vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
        if(-not (Test-Path -LiteralPath $vswhere)){throw 'Install CMake or pass -CMake'}
        $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $CMake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
if(-not (Test-Path -LiteralPath $CMake)){throw 'CMake executable not found'}
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path -LiteralPath $compiler)){throw '.NET Framework 4.x compiler is required'}
$build=Join-Path $repo 'build'
& $CMake -S $repo -B $build -A x64
if($LASTEXITCODE){throw 'Configure failed'}
& $CMake --build $build --config Release --parallel
if($LASTEXITCODE){throw 'Build failed'}
$work=Join-Path $build ('installer-work-'+[Guid]::NewGuid().ToString('N'))
$payload=Join-Path $work 'payload'
& $CMake --install $build --config Release --prefix $payload
if($LASTEXITCODE){throw 'Package staging failed'}
$files=@(Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object {
    $rel=$_.FullName.Substring($payload.Length+1)
    $rel -match '^(bin|include|lib|docs|assets)[\\/]' -or $rel -in @('README.md','product.txt')
} | Sort-Object FullName)
if(-not $files.Count){throw 'Empty payload'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath=Join-Path $work 'payload.zip'
$zip=[IO.Compression.ZipFile]::Open($archivePath,[IO.Compression.ZipArchiveMode]::Create)
$manifestLines=@()
function CsString([string]$s){return '"'+$s.Replace('\','\\').Replace('"','\"')+'"'}
try{
    foreach($file in $files){
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point in payload'}
        $rel=$file.FullName.Substring($payload.Length+1)
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$rel.Replace('\','/'),[IO.Compression.CompressionLevel]::Optimal)
        $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $manifestLines+='        files.Add('+(CsString $rel)+', '+(CsString $hash)+');'
    }
}finally{$zip.Dispose()}
$generated=@"
using System;
using System.Collections.Generic;
internal static class Product {
    internal const string Id = $(CsString $meta.id);
    internal const string Folder = $(CsString $meta.folder);
    internal const string DisplayName = $(CsString $meta.displayName);
    internal const string Description = $(CsString $meta.description);
    internal const string Version = $(CsString $meta.version);
    internal const string Exe = $(CsString $meta.exe);
    internal const string LaunchArgs = $(CsString $meta.launchArgs);
    internal const string Url = $(CsString $meta.url);
    internal static Dictionary<string,string> Manifest() {
        var files = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
$($manifestLines -join [Environment]::NewLine)
        return files;
    }
}
"@
[IO.File]::WriteAllText((Join-Path $work 'Product.g.cs'),$generated,(New-Object Text.UTF8Encoding($false)))
if(-not $OutputDirectory){$OutputDirectory=Join-Path $build 'setup'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$output=Join-Path $OutputDirectory ($meta.folder+'-Setup-'+$meta.version+'-x64.exe')
if(Test-Path -LiteralPath $output){throw 'Installer already exists; select a fresh -OutputDirectory'}
$compileArgs=@('/nologo','/target:winexe','/platform:x64','/optimize+','/warn:4','/warnaserror+','/codepage:65001',
    '/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll',
    '/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll',
    ("/win32manifest:"+(Join-Path $repo 'installer/app.manifest')),
    ("/win32icon:"+$icon),
    ("/resource:"+$archivePath+",payload.zip"),("/out:"+$output),
    (Join-Path $repo 'installer/Setup.cs'),(Join-Path $work 'Product.g.cs'))
& $compiler @compileArgs
if($LASTEXITCODE){throw 'Installer compilation failed'}
$hash=(Get-FileHash -LiteralPath $output).Hash
[IO.File]::WriteAllText($output+'.sha256',$hash+'  '+[IO.Path]::GetFileName($output)+[Environment]::NewLine)
Write-Output "Installer: $output"
Write-Output "Embedded files: $($files.Count)"
Write-Output "SHA256: $hash"
Write-Output 'Unsigned prototype. Does not install into or modify a game.'

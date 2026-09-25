[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Install','Uninstall')][string]$Action,
    [Parameter(Mandatory)][string]$Destination,
    [string]$Package = (Split-Path $PSScriptRoot -Parent)
)
$ErrorActionPreference='Stop'
$Product='FreeFrameGen'
function SafeRoot([string]$value) {
    if(-not [IO.Path]::IsPathRooted($value)){throw 'Use an absolute destination'}
    $full=[IO.Path]::GetFullPath($value).TrimEnd('\')
    if([IO.Path]::GetFileName($full) -ne $Product){throw "Destination must end with $Product"}
    $cursor=$full
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            $item=Get-Item -LiteralPath $cursor -Force
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse points are not allowed'}
            if(Test-Path -LiteralPath (Join-Path $cursor 'left4dead2.exe')){throw 'Installing into a game tree is forbidden'}
        }
        $parent=Split-Path $cursor -Parent
        if($parent -eq $cursor){break}
        $cursor=$parent
    }
    return $full
}
function ChildPath([string]$root,[string]$relative){
    if([IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)' -or $relative.Contains(':')){throw 'Unsafe manifest path'}
    $full=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes destination'}
    $cursor=$full
    while($cursor -ne $root){
        if(Test-Path -LiteralPath $cursor){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point in package path'}
        }
        $cursor=Split-Path $cursor -Parent
    }
    return $full
}
$dest=SafeRoot $Destination
$manifestPath=Join-Path $dest 'install-manifest.json'
if($Action -eq 'Install'){
    $packageRoot=(Resolve-Path -LiteralPath $Package).Path.TrimEnd('\')
    $marker=Join-Path $packageRoot 'product.txt'
    if(-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $Product){throw 'Wrong package identity'}
    if(Test-Path -LiteralPath $dest){throw 'Destination already exists; no files will be overwritten'}
    # Explicit install surface: binaries, SDK, docs and these management tools.
    $items=@()
    foreach($sub in @('bin','include','lib','docs','tools')){
        $dir=Join-Path $packageRoot $sub
        if(Test-Path -LiteralPath $dir){
            foreach($file in Get-ChildItem -LiteralPath $dir -Recurse -File -Force){
                $rel=$file.FullName.Substring($packageRoot.Length+1)
                [void](ChildPath $packageRoot $rel)
                $items+=@{path=$rel;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
            }
        }
    }
    if(-not $items.Count){throw 'Empty package'}
    foreach($rel in @('product.txt','README.md')){
        $file=Join-Path $packageRoot $rel
        if(Test-Path -LiteralPath $file){$items+=@{path=$rel;sha256=(Get-FileHash -LiteralPath $file).Hash}}
    }
    [void](New-Item -ItemType Directory -Path $dest)
    # Write the full plan before copying, so an interrupted install is auditable.
    @{product=$Product;version=1;root=$dest;files=$items} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    foreach($entry in $items){
        $target=ChildPath $dest $entry.path
        [void](New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force)
        Copy-Item -LiteralPath (Join-Path $packageRoot $entry.path) -Destination $target
        if((Get-FileHash -LiteralPath $target).Hash -ne $entry.sha256){throw 'Copy hash mismatch'}
    }
    Write-Output "Installed $Product to $dest (no game/registry/service modifications)"
}else{
    if(-not (Test-Path -LiteralPath $manifestPath)){throw 'No install manifest'}
    $m=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if($m.product -ne $Product -or $m.version -ne 1 -or $m.root -ne $dest){throw 'Manifest identity mismatch'}
    $targets=@()
    foreach($entry in $m.files){
        $target=ChildPath $dest $entry.path
        if($target -eq $manifestPath -or $entry.sha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Invalid manifest record'}
        if(Test-Path -LiteralPath $target){
            if((Get-Item -LiteralPath $target).PSIsContainer){throw 'File became a directory'}
            if((Get-FileHash -LiteralPath $target).Hash -ne $entry.sha256){throw "Modified file preserved: $target. Back it up and resolve before uninstall."}
            $targets+=$target
        }
    }
    # Preflight all entries first. Delete only unchanged, listed leaf files.
    foreach($target in $targets){Remove-Item -LiteralPath $target}
    Remove-Item -LiteralPath $manifestPath
    # No recursive directory deletion. Retain any user-created files/output.
    $dirs=@(Get-ChildItem -LiteralPath $dest -Recurse -Directory -Force | Sort-Object {$_.FullName.Length} -Descending)
    foreach($dir in $dirs){
        if(-not ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) -and -not (Get-ChildItem -LiteralPath $dir.FullName -Force | Select-Object -First 1)){Remove-Item -LiteralPath $dir.FullName}
    }
    if(-not (Get-ChildItem -LiteralPath $dest -Force | Select-Object -First 1)){Remove-Item -LiteralPath $dest}
    Write-Output "Uninstalled unchanged $Product package files; any unlisted user data is retained."
}

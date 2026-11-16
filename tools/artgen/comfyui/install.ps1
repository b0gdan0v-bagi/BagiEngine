<#
.SYNOPSIS
Installs the portable ComfyUI and the models of manifest.json on this Windows PC (tools/artgen/CLAUDE.md).

.DESCRIPTION
Idempotent: what is already there and matches the manifest is left alone, so a second run only checks. It never overwrites
or deletes what it did not download: a ComfyUI of another version in -Root, or a model file whose hash differs, stops the run
and says what to do. Every download is checked against the manifest's size and SHA256 before it is used; an interrupted
download resumes. Needs curl.exe (Windows 10+) and 7-Zip or a tar.exe that reads .7z (Windows 11).

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\artgen\comfyui\install.ps1 -Root D:\ComfyUI
#>
param(
    [string]$Root = "D:\ComfyUI",
    [string]$Downloads = "",
    [switch]$SkipModels,
    [switch]$KeepDownloads
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Get-Content -Raw -Path (Join-Path $here "manifest.json") | ConvertFrom-Json
if (-not $Downloads) { $Downloads = "$Root.downloads" }

function Fail([string]$message) {
    Write-Host "error: $message" -ForegroundColor Red
    exit 1
}

function Get-Sha256([string]$path) {
    (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Downloads $url into $dest through $dest.part (resumed when a part is there) and checks size and hash before the rename.
function Get-Verified([string]$url, [string]$dest, [int64]$size, [string]$sha256) {
    $part = "$dest.part"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    Write-Host "downloading $url"
    & curl.exe -L --fail --retry 3 -C - -o $part $url
    if ($LASTEXITCODE -ne 0) { Fail "curl failed ($LASTEXITCODE) for $url; run again to resume" }
    $actualSize = (Get-Item $part).Length
    if ($actualSize -ne $size) { Fail "$part has $actualSize bytes, the manifest says $size; delete it and run again" }
    Write-Host "checking SHA256 of $part"
    $actual = Get-Sha256 $part
    if ($actual -ne $sha256) { Fail "$part has SHA256 $actual, the manifest says $sha256; delete it and run again" }
    Move-Item -Force -Path $part -Destination $dest
}

function Get-InstalledVersion([string]$root) {
    $file = Join-Path $root "ComfyUI\comfyui_version.py"
    if (-not (Test-Path $file)) { return $null }
    $match = Select-String -Path $file -Pattern '__version__\s*=\s*"([^"]+)"'
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return "unknown"
}

function Expand-SevenZip([string]$archive, [string]$target) {
    $sevenZip = (Get-Command 7z.exe -ErrorAction SilentlyContinue).Source
    if (-not $sevenZip -and (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) { $sevenZip = "$env:ProgramFiles\7-Zip\7z.exe" }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    if ($sevenZip) {
        & $sevenZip x -y "-o$target" $archive | Out-Null
    } else {
        & tar.exe -xf $archive -C $target
    }
    if ($LASTEXITCODE -ne 0) { Fail "could not unpack $archive (install 7-Zip and run again)" }
}

# ComfyUI itself
$comfy = $manifest.comfyui
$installed = Get-InstalledVersion $Root
if ($installed -eq $comfy.version) {
    Write-Host "ComfyUI $installed is in $Root"
} elseif ($installed) {
    Fail "$Root holds ComfyUI $installed, the manifest pins $($comfy.version); move that folder away or pass another -Root"
} elseif ((Test-Path $Root) -and (Get-ChildItem -Force $Root | Select-Object -First 1)) {
    Fail "$Root is not empty and holds no portable ComfyUI; pass another -Root"
} else {
    $archive = Join-Path $Downloads $comfy.asset
    if (-not (Test-Path $archive)) { Get-Verified $comfy.url $archive $comfy.size $comfy.sha256 }
    $staging = "$Root.unpack"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Write-Host "unpacking $archive"
    Expand-SevenZip $archive $staging
    $inner = Get-ChildItem -Directory $staging | Where-Object { Test-Path (Join-Path $_.FullName "ComfyUI\main.py") } | Select-Object -First 1
    if (-not $inner) { Fail "$archive holds no ComfyUI_windows_portable folder" }
    if (Test-Path $Root) { Remove-Item -Force $Root }  # checked empty above
    Move-Item -Path $inner.FullName -Destination $Root
    Remove-Item -Recurse -Force $staging
    Write-Host "ComfyUI $(Get-InstalledVersion $Root) installed in $Root"
}

Copy-Item -Force -Path (Join-Path $here "start-local.bat") -Destination (Join-Path $Root "start-local.bat")

# The models
if (-not $SkipModels) {
    foreach ($model in $manifest.models) {
        $dest = Join-Path $Root "ComfyUI\models\$($model.folder)\$($model.name)"
        if (Test-Path $dest) {
            Write-Host "checking $($model.name)"
            $actual = Get-Sha256 $dest
            if ($actual -ne $model.sha256) { Fail "$dest has SHA256 $actual, the manifest says $($model.sha256); delete it and run again" }
            Write-Host "ok $($model.folder)/$($model.name)"
        } else {
            Get-Verified $model.url $dest $model.size $model.sha256
            Write-Host "installed $($model.folder)/$($model.name) ($($model.licence))"
        }
    }
}

# Only the archive goes, and the folder only when nothing else is in it: -Downloads may name a folder of the owner's.
$archive = Join-Path $Downloads $comfy.asset
if (-not $KeepDownloads -and (Test-Path $archive)) { Remove-Item -Force $archive }
if ((Test-Path $Downloads) -and -not (Get-ChildItem -Force $Downloads | Select-Object -First 1)) { Remove-Item -Force $Downloads }
Write-Host "done. Start: $Root\start-local.bat; check: python tools\artgen\comfyui_check.py"

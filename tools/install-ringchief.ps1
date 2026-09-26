<#
.SYNOPSIS
  Installs (or removes) AlphaRing's WTSAPI32.dll into Halo: The Master Chief Collection.

.DESCRIPTION
  Finds MCC through Steam's library folders, backs up any existing WTSAPI32.dll, and copies
  the DLL that sits next to this script. Run with -Uninstall to remove it (and restore the
  most recent backup, if any). Use -McCPath to point at a non-Steam or unusual install.

.EXAMPLE
  .\install-ringchief.ps1
  .\install-ringchief.ps1 -Uninstall
  .\install-ringchief.ps1 -McCPath "D:\Games\Halo The Master Chief Collection"
#>
param(
    [switch]$Uninstall,
    [string]$McCPath
)

$ErrorActionPreference = 'Stop'
# SHA-256 via .NET: works even when Windows PowerShell is started from PowerShell 7 and
# can't load its own hashing cmdlets.
function Get-Sha256([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '').ToLower() }
    finally { $sha.Dispose() }
}
$SupportedVersion = '1.3528.0.0'

function Find-SteamLibraries {
    $roots = @()
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        try {
            $p = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($name in 'SteamPath', 'InstallPath') {
                if ($p.$name) { $roots += ($p.$name -replace '/', '\') }
            }
        } catch { }
    }
    $roots += 'C:\Program Files (x86)\Steam'
    $libs = @()
    foreach ($root in ($roots | Select-Object -Unique)) {
        if (-not (Test-Path $root)) { continue }
        $libs += $root
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    return $libs | Select-Object -Unique
}

function Find-MCC {
    foreach ($lib in Find-SteamLibraries) {
        $candidate = Join-Path $lib 'steamapps\common\Halo The Master Chief Collection'
        if (Test-Path (Join-Path $candidate 'MCC\Binaries\Win64\MCC-Win64-Shipping.exe')) { return $candidate }
    }
    return $null
}

if (-not $McCPath) { $McCPath = Find-MCC }
if (-not $McCPath) {
    Write-Host "Couldn't find Halo MCC through Steam." -ForegroundColor Red
    Write-Host 'Run again with -McCPath "<folder that contains the MCC folder>".'
    exit 1
}

$bin = Join-Path $McCPath 'MCC\Binaries\Win64'
$exe = Join-Path $bin 'MCC-Win64-Shipping.exe'
$target = Join-Path $bin 'WTSAPI32.dll'
if (-not (Test-Path $exe)) { Write-Host "Not an MCC install: $McCPath" -ForegroundColor Red; exit 1 }
Write-Host "Halo MCC: $McCPath"

if ($Uninstall) {
    if (Test-Path $target) { Remove-Item $target -Force; Write-Host 'Removed WTSAPI32.dll' }
    $backup = Get-ChildItem $bin -Filter 'WTSAPI32.dll.bak-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($backup) { Copy-Item $backup.FullName $target; Write-Host "Restored $($backup.Name)" }
    Write-Host 'Done. MCC will start without AlphaRing.' -ForegroundColor Green
    exit 0
}

$source = Join-Path $PSScriptRoot 'WTSAPI32.dll'
if (-not (Test-Path $source)) { Write-Host "WTSAPI32.dll must be next to this script ($PSScriptRoot)." -ForegroundColor Red; exit 1 }

$version = (Get-Item $exe).VersionInfo.FileVersion
if ($version -and $version -ne $SupportedVersion) {
    Write-Host "Warning: MCC is $version but this AlphaRing was built for $SupportedVersion." -ForegroundColor Yellow
    Write-Host 'It may crash. Check for a newer AlphaRing release first.' -ForegroundColor Yellow
    if ((Read-Host 'Install anyway? (y/N)') -notmatch '^[yY]') { exit 1 }
}

if (Get-Process -Name 'MCC-Win64-Shipping' -ErrorAction SilentlyContinue) {
    Write-Host 'Close Halo MCC first, then run this again.' -ForegroundColor Red
    exit 1
}

if (Test-Path $target) {
    if ((Get-Sha256 $target) -eq (Get-Sha256 $source)) {
        Write-Host 'This version is already installed.' -ForegroundColor Green
        exit 0
    }
    $backup = "$target.bak-$(Get-Date -Format yyyyMMdd-HHmmss)"
    Copy-Item $target $backup
    Write-Host "Backed up the old DLL to $(Split-Path $backup -Leaf)"
}

try {
    Copy-Item $source $target -Force
} catch {
    Write-Host 'Copy failed. If MCC is in Program Files, right-click the script and run PowerShell as administrator.' -ForegroundColor Red
    throw
}
Write-Host 'AlphaRing installed.' -ForegroundColor Green
Write-Host 'Launch MCC from Steam with "Anti-Cheat Disabled", then sign in to your group in the Ring Chief window.'

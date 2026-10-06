# install.ps1 — Nopa installer for Windows (PowerShell)
#
# Usage (from the extracted bundle directory):
#   .\install.ps1                     # per-user: %LOCALAPPDATA%\Programs\nopac
#   .\install.ps1 -Prefix C:\nopa     # custom install prefix
#   .\install.ps1 -System             # machine-wide: %ProgramFiles%\nopac (admin)
#   .\install.ps1 -NoPath             # do not modify PATH
#
# Layout installed under <Prefix> (mirrors the Unix install.sh):
#   <Prefix>\bin\nopac.exe
#   <Prefix>\lib\libnopa.a
#   <Prefix>\include\{nopa,Foundation}\...
#   <Prefix>\share\nopac\completions\nopac.ps1
#
# `nopac.exe` locates its bundled headers via `resolve_bundle_root()`:
# it checks <exe-dir>\.. for `include\nopa\runtime.h`, so `bin\nopac.exe` +
# `include\` at the prefix root works out of the box. We also set NOPA_HOME.

[CmdletBinding()]
param(
    [string]$Prefix = "",
    [switch]$System,
    [switch]$NoPath
)

$ErrorActionPreference = "Stop"

# ── Bundle directory (where this script lives) ──
$Bundle = Split-Path -Parent $MyInvocation.MyCommand.Path

# ── i18n (Chinese UI → zh, otherwise en) ──
$ui = ""
try { $ui = [System.Globalization.CultureInfo]::CurrentUICulture.Name } catch {}
if (-not $ui) { $ui = $PSUICulture }
$zh = ($ui -like 'zh*') -or ($env:LANG -like 'zh*')
function T([string]$z, [string]$e) { if ($zh) { $z } else { $e } }

# ── Default install prefix ──
if (-not $Prefix) {
    if ($System) {
        $Prefix = Join-Path $env:ProgramFiles "nopac"
    } else {
        $Prefix = Join-Path $env:LOCALAPPDATA "Programs\nopac"
    }
}

$BinDir  = Join-Path $Prefix "bin"
$LibDir  = Join-Path $Prefix "lib"
$IncDir  = Join-Path $Prefix "include"
$CompDir = Join-Path $Prefix "share\nopac\completions"

Write-Host "==> $(T 'Nopa 安装包' 'Nopa Installer') (nopac)"
Write-Host "    $(T '安装目录' 'Install directory'): $Prefix"
Write-Host ""

New-Item -ItemType Directory -Force -Path $BinDir, $LibDir, $IncDir, $CompDir | Out-Null

# ── Binary ──
$srcExe = Join-Path $Bundle "nopac.exe"
if (-not (Test-Path $srcExe)) {
    Write-Error "$(T '包内找不到 nopac.exe' 'nopac.exe not found in bundle'): $srcExe"
}
Copy-Item -Force $srcExe (Join-Path $BinDir "nopac.exe")
Write-Host "    nopac.exe    -> $(Join-Path $BinDir 'nopac.exe')"

# ── Static runtime library ──
$srcLib = Join-Path $Bundle "libnopa.a"
if (Test-Path $srcLib) {
    Copy-Item -Force $srcLib (Join-Path $LibDir "libnopa.a")
    Write-Host "    libnopa.a    -> $(Join-Path $LibDir 'libnopa.a')"
}

# ── Headers ──
$srcInc = Join-Path $Bundle "include"
if (Test-Path $srcInc) {
    Copy-Item -Recurse -Force (Join-Path $srcInc "*") $IncDir
    Write-Host "    include\     -> $IncDir\"
}

# ── Completions (generate the PowerShell completion) ──
$srcComp = Join-Path $Bundle "completions"
if (Test-Path $srcComp) {
    Copy-Item -Force (Join-Path $srcComp "*") $CompDir
}
$nopacExe = Join-Path $BinDir "nopac.exe"
$compPs1 = Join-Path $CompDir "nopac.ps1"
try {
    & $nopacExe --gen-completions powershell | Out-File -Encoding utf8 $compPs1
    Write-Host "    completions  -> $compPs1"
} catch {
    Write-Host "    $(T '跳过补全生成' 'completion generation skipped')"
}

# ── PATH (User by default; Machine with -System) ──
if (-not $NoPath) {
    $scope = if ($System) { "Machine" } else { "User" }
    $cur = [Environment]::GetEnvironmentVariable("Path", $scope)
    if (-not $cur) { $cur = "" }
    $parts = @($cur -split ';' | Where-Object { $_ -ne "" })
    if ($parts -notcontains $BinDir) {
        [Environment]::SetEnvironmentVariable("Path", (($parts + $BinDir) -join ';'), $scope)
        Write-Host "    PATH[$scope] += $BinDir"
    } else {
        Write-Host "    PATH[$scope] $(T '已包含' 'already contains') $BinDir"
    }
    # Make it usable in the CURRENT session too.
    if (($env:Path -split ';') -notcontains $BinDir) { $env:Path = "$env:Path;$BinDir" }
}

# ── NOPA_HOME (robust bundle-root resolution) ──
[Environment]::SetEnvironmentVariable("NOPA_HOME", $Prefix, "User")
$env:NOPA_HOME = $Prefix
Write-Host "    NOPA_HOME    = $Prefix"

# ── Register the PowerShell completion in the user profile (idempotent) ──
if (Test-Path $compPs1) {
    $marker = "# nopac completion (install.ps1 auto-added)"
    $profilePath = $PROFILE.CurrentUserAllHosts
    $profDir = Split-Path -Parent $profilePath
    if ($profDir -and -not (Test-Path $profDir)) { New-Item -ItemType Directory -Force -Path $profDir | Out-Null }
    if (-not (Test-Path $profilePath)) { New-Item -ItemType File -Force -Path $profilePath | Out-Null }
    $content = ""
    try { $content = Get-Content -Raw -ErrorAction SilentlyContinue $profilePath } catch {}
    if (-not ($content -and $content.Contains($marker))) {
        Add-Content -Path $profilePath -Value "`r`n$marker`r`n. `"$compPs1`""
        Write-Host "    PowerShell profile updated: $profilePath"
    } else {
        Write-Host "    PowerShell profile $(T '已配置' 'already configured')"
    }
}

Write-Host ""
Write-Host "========== $(T '安装完成' 'Installation complete') =========="
Write-Host "  binary:  $BinDir\nopac.exe"
Write-Host "  headers: $IncDir\"
Write-Host "  lib:     $LibDir\libnopa.a"
Write-Host ""
Write-Host "  $(T '新开一个终端即可使用' 'Open a new terminal, then run'): nopac --version"
Write-Host "  $(T '卸载' 'Uninstall'): Remove-Item -Recurse -Force `"$Prefix`""

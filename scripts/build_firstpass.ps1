# Rebuild Assembly-CSharp-firstpass.dll (EZGUI) for the WA2DC PC port.
#
# Applies the firstpass patches, compiles with the stock Unity 3.5 references,
# and (unless -SkipInstall) copies the result into the target build.
#
# Defaults preserve the development workflow:
#   powershell -ExecutionPolicy Bypass -File scripts/build_firstpass.ps1
#
# scripts/build_all.ps1 passes the fresh export/stock-player paths explicitly.

param(
    [string]$Source = "",
    [string]$StockManaged = "",
    [string]$SystemCore = "",
    [string]$Output = "",
    [string]$Build = "",
    [switch]$SkipApply,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root "work\pc-firstpass" }
if (-not $StockManaged) { $StockManaged = "C:\Users\CHIAKI\AppData\Local\Temp\opencode\wa2dc-stock-player\empty_Data\Managed" }
if (-not $SystemCore) { $SystemCore = Join-Path $root "work\data\Managed\System.Core.dll" }
if (-not $Output) { $Output = Join-Path $root "work\pc-firstpass.dll" }
if (-not $Build) { $Build = Join-Path $root "build\WA2DC" }
$work = Join-Path $root "work"

if (-not (Test-Path $Source)) { throw "firstpass source tree not found: $Source" }
if (-not (Test-Path $StockManaged)) { throw "stock Unity references not found: $StockManaged" }

Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. E:\agent-projects\tools\re\env.ps1 | Out-Null

if (-not $SkipApply) {
    python-re -I (Join-Path $root "scripts\apply_ui_mouse.py") --source $Source
}

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$response = @(
    "/nologo",
    "/target:library",
    "/nostdlib+",
    "/out:`"$Output`""
)
foreach ($reference in @(
    (Join-Path $StockManaged "mscorlib.dll"),
    (Join-Path $StockManaged "UnityEngine.dll"),
    $SystemCore
)) {
    if (-not (Test-Path $reference)) { throw "missing reference: $reference" }
    $response += "/r:`"$reference`""
}
$response += Get-ChildItem $Source -Recurse -Filter *.cs | ForEach-Object { "`"$($_.FullName)`"" }
$rsp = Join-Path $work "pc-firstpass.rsp"
Set-Content -Path $rsp -Value $response -Encoding ASCII

$errors = Join-Path $work "pc-firstpass-errors.txt"
& $csc /noconfig "@$rsp" > $errors 2>&1
$errorCount = (Get-Content $errors | Select-String "error CS").Count
Write-Host "firstpass compile exit=$LASTEXITCODE errors=$errorCount"
if ($errorCount -ne 0) {
    Get-Content $errors | Select-String "error CS" | Select-Object -First 20
    throw "Assembly-CSharp-firstpass build failed"
}

if (-not $SkipInstall) {
    $target = Join-Path $Build "WA2DC_Data\Managed\Assembly-CSharp-firstpass.dll"
    Copy-Item $Output $target -Force
    Write-Host "installed $target"
}

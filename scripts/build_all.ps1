# Build the finished WA2DC Windows port from a pristine XAPK with one command.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 `
#       -Xapk "C:\path\to\game.xapk" -Background "C:\path\to\bg.png"
#
# Inputs:
#   -Xapk       pristine XAPK (default: the only *.xapk in the repo root)
#   -Unity      Unity 3.5.6f4 editor (default: C:\Program Files (x86)\Unity\Editor\Unity.exe)
#   -Pyenv      toolbox Python venv with UnityPy (default: E:\agent-projects\tools\re\pyenv)
#   -AssetRipper AssetRipper GUI executable (default: the toolbox install)
#   -Ilspycmd   optional ilspycmd for post-build DLL verification
#   -Background optional strip art (bg.png); the build works without it (black strips)
#   -Output     finished build folder (default: build\WA2DC)
#   -Port       local port for the AssetRipper headless API (default: 5577)
#
# The build is staged in work\build_all and only replaces -Output after every
# step and the verification pass succeeds. On failure the previous output is
# left untouched and the error is printed.

param(
    [string]$Xapk = "",
    [string]$Unity = "C:\Program Files (x86)\Unity\Editor\Unity.exe",
    [string]$Pyenv = "E:\agent-projects\tools\re\pyenv",
    [string]$AssetRipper = "E:\agent-projects\tools\re\assetripper\AssetRipper.GUI.Free.exe",
    [string]$Ilspycmd = "E:\agent-projects\tools\re\bin\ilspycmd.cmd",
    [string]$Background = "",
    [string]$Output = "",
    [int]$Port = 5577,
    [string]$ExpectedXapkSha256 = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output = Join-Path $root "build\WA2DC" }
$Output = [System.IO.Path]::GetFullPath($Output)
if (-not $Xapk) {
    $found = Get-ChildItem $root -Filter *.xapk -File | Select-Object -First 1
    if (-not $found) { throw "no .xapk found in $root; pass -Xapk" }
    $Xapk = $found.FullName
}
$Xapk = [System.IO.Path]::GetFullPath($Xapk)

$work = Join-Path $root "work\build_all"
$staging = Join-Path $work "staging\WA2DC"
$dataDir = Join-Path $work "data"
$obbDir = Join-Path $work "obb"
$obbWin = Join-Path $work "obb-win"
$apkDir = Join-Path $work "apk"
$xapkDir = Join-Path $work "xapk"
$generated = Join-Path $work "generated"
$exportRoot = Join-Path $work "assetripper\UnityProject"
$stockProject = Join-Path $env:TEMP "wa2dc-build-stock"
$stockPlayer = Join-Path $env:TEMP "wa2dc-build-player"
$stockLog = Join-Path $env:TEMP "wa2dc-build-unity.log"
$python = Join-Path $Pyenv "Scripts\python.exe"
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Write-Step($message) {
    Write-Host ""
    Write-Host "===== $message ====="
}

function Assert-File($path, $label) {
    if (-not (Test-Path $path -PathType Leaf)) { throw "missing $label`: $path" }
}

function Invoke-Python($script, [string[]]$arguments, $label) {
    Write-Host "== $label"
    & $python -I $script @arguments
    if ($LASTEXITCODE -ne 0) { throw "$label failed (exit $LASTEXITCODE)" }
}

function Expand-Zip($zip, $destination) {
    if (Test-Path $destination) { Remove-Item $destination -Recurse -Force }
    New-Item -ItemType Directory -Force $destination | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $destination)
}

function Invoke-Unity([string[]]$arguments, $label) {
    Write-Host "== $label"
    $process = Start-Process -FilePath $Unity -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "$label failed (exit $($process.ExitCode)); see $stockLog" }
}

function Assert-NoAndroidTargets($directory, $src, $dst) {
    $output = & $python -I (Join-Path $root "scripts\retag_serialized.py") $directory --from $src --to $dst --dry-run --skip StreamingAssets --skip Mono --skip Resources 2>&1
    if ($LASTEXITCODE -ne 0) { throw "retag verification failed" }
    if (($output | Out-String) -match "would-retag") {
        throw "base Data still contains Android-target serialized files"
    }
}

New-Item -ItemType Directory -Force $work | Out-Null
$summary = New-Object System.Collections.Generic.List[string]

try {
    Write-Step "Inputs"
    Assert-File $Xapk "XAPK"
    Assert-File $Unity "Unity editor"
    Assert-File $AssetRipper "AssetRipper"
    Assert-File $csc "csc"
    if (-not (Test-Path $python -PathType Leaf)) { throw "Python venv not found: $python (expected UnityPy installed)" }
    $unityVersion = (Get-Item $Unity).VersionInfo.FileVersion
    if ($unityVersion -notlike "3.5.6*") { throw "expected Unity 3.5.6, found $unityVersion" }
    $xapkHash = (Get-FileHash $Xapk -Algorithm SHA256).Hash
    if ($ExpectedXapkSha256 -and $xapkHash -ne $ExpectedXapkSha256.ToUpperInvariant()) {
        throw "XAPK SHA-256 mismatch: $xapkHash"
    }
    $unityPy = (& $python -c "import UnityPy; print(UnityPy.__version__)" 2>&1 | Out-String).Trim()
    $cscVersion = (Get-Item $csc).VersionInfo.FileVersion
    Write-Host "XAPK      : $Xapk"
    Write-Host "SHA-256   : $xapkHash"
    Write-Host "Unity     : $unityVersion"
    Write-Host "UnityPy   : $unityPy"
    Write-Host "csc       : $cscVersion"
    Write-Host "Output    : $Output"
    if (Test-Path $Ilspycmd) { Write-Host "ilspycmd  : present ($Ilspycmd)" } else { Write-Host "ilspycmd  : not found (optional)" }
    if ($Background) { Assert-File $Background "background image"; Write-Host "Background: $Background" } else { Write-Host "Background: none (black strip backing)" }

    Write-Step "Clean workspace"
    if (Test-Path $work) { Remove-Item $work -Recurse -Force }
    New-Item -ItemType Directory -Force $work, $staging, $dataDir, $generated, (Join-Path $work "src") | Out-Null

    Write-Step "1/9 Unpack XAPK, APK and OBB"
    Expand-Zip $Xapk $xapkDir
    $apk = Get-ChildItem $xapkDir -Recurse -Filter *.apk -File | Select-Object -First 1
    $obb = Get-ChildItem $xapkDir -Recurse -Filter *.obb -File | Select-Object -First 1
    if (-not $apk) { throw "no APK inside $Xapk" }
    if (-not $obb) { throw "no OBB inside $Xapk" }
    Expand-Zip $apk.FullName $apkDir
    Expand-Zip $obb.FullName $obbDir
    $apkData = Join-Path $apkDir "assets\bin\Data"
    if (-not (Test-Path $apkData)) { throw "APK has no assets/bin/Data" }
    Get-ChildItem $apkData -File | ForEach-Object { Copy-Item $_.FullName $dataDir -Force }
    New-Item -ItemType Directory -Force (Join-Path $dataDir "Managed") | Out-Null
    Copy-Item (Join-Path $apkData "Managed\*") (Join-Path $dataDir "Managed") -Recurse -Force
    Assert-File (Join-Path $dataDir "mainData") "base Data mainData"
    $obbCount = (Get-ChildItem $obbDir -Filter *.unity3d -File).Count
    if ($obbCount -ne 255) { throw "expected 255 bundles in the OBB, found $obbCount" }
    Write-Host "bundles: $obbCount"

    Write-Step "2/9 Build the stock Unity player"
    if (Test-Path $stockProject) { Remove-Item $stockProject -Recurse -Force }
    if (Test-Path $stockPlayer) { Remove-Item $stockPlayer -Recurse -Force }
    New-Item -ItemType Directory -Force $stockPlayer | Out-Null
    Invoke-Unity @("-batchmode", "-quit", "-createProject", $stockProject, "-logFile", $stockLog) "create stock Unity project"
    New-Item -ItemType Directory -Force (Join-Path $stockProject "Assets\Editor") | Out-Null
    Copy-Item (Join-Path $root "scripts\unity\BuildEmpty.cs") (Join-Path $stockProject "Assets\Editor\BuildEmpty.cs") -Force
    $apkIcon = Get-ChildItem (Join-Path $apkDir "res") -Recurse -Filter app_icon.png -File |
        Sort-Object Length -Descending | Select-Object -First 1
    if (-not $apkIcon) { throw "the APK has no res/**/app_icon.png for the player icon" }
    Copy-Item $apkIcon.FullName (Join-Path $stockProject "Assets\app_icon.png") -Force
    Write-Host "player icon: $($apkIcon.FullName) ($($apkIcon.Length) bytes)"
    $env:WA2DC_STOCK_PLAYER_OUT = Join-Path $stockPlayer "empty.exe"
    try {
        Invoke-Unity @("-batchmode", "-quit", "-projectPath", $stockProject, "-executeMethod", "BuildEmpty.Build", "-logFile", $stockLog) "build stock Windows player"
    } finally {
        Remove-Item Env:WA2DC_STOCK_PLAYER_OUT -ErrorAction SilentlyContinue
    }
    Assert-File (Join-Path $stockPlayer "empty.exe") "stock player"
    Assert-File (Join-Path $stockPlayer "empty_Data\Managed\mscorlib.dll") "stock mscorlib"
    Write-Host "stock player: $((Get-Item (Join-Path $stockPlayer 'empty.exe')).Length) bytes"

    Write-Step "3/9 Assemble the player, Data and StreamingAssets"
    Copy-Item (Join-Path $stockPlayer "empty.exe") (Join-Path $staging "WA2DC.exe") -Force
    Copy-Item (Join-Path $stockPlayer "empty_Data") (Join-Path $staging "WA2DC_Data") -Recurse -Force
    $stagingData = Join-Path $staging "WA2DC_Data"
    Get-ChildItem $dataDir -File |
        Where-Object { $_.Name -notin @("settings.xml", "unity default resources", "PlayerConnectionConfigFile") } |
        ForEach-Object { Copy-Item $_.FullName $stagingData -Force }
    Copy-Item (Join-Path $dataDir "Managed\System.Core.dll") (Join-Path $stagingData "Managed\System.Core.dll") -Force
    New-Item -ItemType Directory -Force (Join-Path $stagingData "StreamingAssets") | Out-Null

    # Saves-tab hook: version.dll proxy next to WA2DC.exe
    $hookSrc = Join-Path $work "dialoghook-src"
    Copy-Item (Join-Path $root "dialoghook") $hookSrc -Recurse -Force
    $hookBuild = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", "call `"$hookSrc\build.bat`"") -Wait -PassThru -WindowStyle Hidden
    if ($hookBuild.ExitCode -ne 0) { throw "dialog hook build failed (exit $($hookBuild.ExitCode))" }
    Assert-File (Join-Path $hookSrc "out\version.dll") "dialog hook version.dll"
    Copy-Item (Join-Path $hookSrc "out\version.dll") (Join-Path $staging "version.dll") -Force
    Write-Host "dialog hook: $((Get-Item (Join-Path $staging 'version.dll')).Length) bytes"

    Write-Step "4/9 Retag the base Data to Windows"
    Invoke-Python (Join-Path $root "scripts\retag_serialized.py") @($stagingData, "--from", "13", "--to", "5", "--skip", "StreamingAssets", "--skip", "Mono", "--skip", "Resources") "retag base Data"
    Assert-NoAndroidTargets $stagingData 13 5

    Write-Step "5/9 Apply the Data fixes"
    $companyOut = Join-Path $work "mainData.company"
    Invoke-Python (Join-Path $root "scripts\fix_company_name.py") @((Join-Path $stagingData "mainData"), $companyOut) "patch companyName"
    Move-Item $companyOut (Join-Path $stagingData "mainData") -Force
    Invoke-Python (Join-Path $root "scripts\fix_argb4444_order.py") @(
        (Join-Path $stagingData "sharedassets0.assets"),
        (Join-Path $stagingData "sharedassets2.assets"),
        (Join-Path $stagingData "sharedassets5.assets")
    ) "fix ARGB4444 nibble order"
    $etc1Out = Join-Path $work "sharedassets0.rgb24"
    Invoke-Python (Join-Path $root "scripts\convert_etc1.py") @((Join-Path $stagingData "sharedassets0.assets"), $etc1Out, "--format", "3") "convert ETC1 black/white"
    Move-Item $etc1Out (Join-Path $stagingData "sharedassets0.assets") -Force

    Write-Step "6/9 Retag and install the 255 bundles"
    Invoke-Python (Join-Path $root "scripts\retag_bundles.py") @($obbDir, $obbWin) "retag 255 bundles"
    $bundleCount = (Get-ChildItem $obbWin -Filter *.unity3d -File).Count
    if ($bundleCount -ne 255) { throw "expected 255 retagged bundles, found $bundleCount" }
    Copy-Item (Join-Path $obbWin "*.unity3d") (Join-Path $stagingData "StreamingAssets") -Force
    $face = Join-Path $stagingData "StreamingAssets\CH101_01A_Face.unity3d"
    $faceOut = Join-Path $work "CH101_01A_Face.rgb24"
    Invoke-Python (Join-Path $root "scripts\convert_etc1.py") @($face, $faceOut, "--format", "3") "convert ETC1 face"
    Move-Item $faceOut $face -Force
    if ($Background) {
        Copy-Item $Background (Join-Path $stagingData "StreamingAssets\bg.png") -Force
    }
    Copy-Item (Join-Path $root "patches\03-launcher\WA2DC.bat") (Join-Path $staging "WA2DC.bat") -Force
    Copy-Item (Join-Path $root "patches\06-launcher\WA2DC-1080p.bat") (Join-Path $staging "WA2DC-1080p.bat") -Force

    Write-Step "7/9 Export the project with AssetRipper"
    if (Test-Path $exportRoot) { Remove-Item $exportRoot -Recurse -Force }
    New-Item -ItemType Directory -Force $exportRoot | Out-Null
    $server = Start-Process -FilePath $AssetRipper -ArgumentList @("--headless", "--port", "$Port") -PassThru -WindowStyle Hidden
    try {
        $ready = $false
        for ($attempt = 0; $attempt -lt 30; $attempt++) {
            Start-Sleep -Seconds 1
            try {
                $response = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/" -UseBasicParsing -TimeoutSec 2
                if ($response.StatusCode -eq 200) { $ready = $true; break }
            } catch {
            }
        }
        if (-not $ready) { throw "AssetRipper did not start on port $Port" }
        $load = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/LoadFolder" -Method Post -Body @{Path = $dataDir} -UseBasicParsing -TimeoutSec 300
        if ($load.StatusCode -ne 200) { throw "AssetRipper LoadFolder returned $($load.StatusCode)" }
        $export = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/Export/UnityProject" -Method Post -Body @{Path = $exportRoot; CreateSubfolder = "false"} -UseBasicParsing -TimeoutSec 900
        if ($export.StatusCode -ne 200) { throw "AssetRipper export returned $($export.StatusCode)" }
    } finally {
        if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    }
    $project = Join-Path $exportRoot "ExportedProject"
    $srcMain = Join-Path $project "Assets\Scripts\Assembly-CSharp"
    $srcFirstExport = Join-Path $project "Assets\Plugins\Assembly-CSharp-firstpass"
    if (-not (Test-Path $srcMain)) { throw "AssetRipper export has no Assembly-CSharp source" }
    if (-not (Test-Path $srcFirstExport)) { throw "AssetRipper export has no firstpass source" }
    $srcFirst = Join-Path $work "src\pc-firstpass"
    Copy-Item $srcFirstExport $srcFirst -Recurse -Force
    Copy-Item (Join-Path $root "patches\02-player-swap\pc-firstpass\*.cs") $srcFirst -Force

    Write-Step "8/9 Patch and compile both assemblies"
    Invoke-Python (Join-Path $root "scripts\fix_ezgui_virtual.py") @("--source", $srcFirst) "remove illegal EZGUI virtual modifiers"
    Invoke-Python (Join-Path $root "scripts\apply_ui_mouse.py") @("--source", $srcFirst) "EZGUI mouse patch"
    Invoke-Python (Join-Path $root "scripts\apply_boot_fixes.py") @("--source", $srcMain) "save-directory boot fixes"
    Invoke-Python (Join-Path $root "scripts\parse_dlc_items.py") @($srcMain, $generated) "parse owned DLC tables"
    $dlcRows = Import-Csv (Join-Path $generated "dlc-items.csv")
    $sale1 = ($dlcRows | Where-Object { [int]$_.SaleType -eq 1 }).Count
    if ($sale1 -ne 57) { throw "expected 57 SaleType==1 DLC items, found $sale1" }
    Invoke-Python (Join-Path $root "scripts\apply_dlc_unlock.py") @(
        "--source", $srcMain,
        "--items", (Join-Path $generated "dlc-items.csv"),
        "--recipe", (Join-Path $generated "02-dlc-unlock")
    ) "DLC unlock patch"
    Invoke-Python (Join-Path $root "scripts\apply_background.py") @(
        "--source", $srcMain,
        "--patch", (Join-Path $root "patches\03-background"),
        "--input", (Join-Path $root "patches\03-input")
    ) "curtain/input patches"

    $firstOut = Join-Path $work "pc-firstpass.dll"
    $mainOut = Join-Path $work "pc-main-curtain.dll"
    & (Join-Path $root "scripts\build_firstpass.ps1") `
        -Source $srcFirst `
        -StockManaged (Join-Path $stockPlayer "empty_Data\Managed") `
        -SystemCore (Join-Path $dataDir "Managed\System.Core.dll") `
        -Output $firstOut -Build $staging -SkipApply
    if (-not (Test-Path $firstOut)) { throw "firstpass compile produced no DLL" }
    & (Join-Path $root "scripts\build_main.ps1") `
        -Source $srcMain `
        -StockManaged (Join-Path $stockPlayer "empty_Data\Managed") `
        -SystemCore (Join-Path $dataDir "Managed\System.Core.dll") `
        -Firstpass $firstOut `
        -Output $mainOut -Build $staging -SkipApply
    if (-not (Test-Path $mainOut)) { throw "main compile produced no DLL" }

    Write-Step "9/9 Verify and install the finished build"
    if ($Output -eq $work -or $Output -eq $staging -or $Output.StartsWith($staging + [System.IO.Path]::DirectorySeparatorChar)) {
        throw "refusing to use the staging/workspace as -Output: $Output"
    }
    $verifyArgs = @("--build", $staging)
    if ($Background) { $verifyArgs += "--require-bg" }
    Invoke-Python (Join-Path $root "scripts\verify_build.py") $verifyArgs "verify build invariants"

    if (Test-Path $Output) { Remove-Item $Output -Recurse -Force }
    New-Item -ItemType Directory -Force $Output | Out-Null
    Copy-Item (Join-Path $staging "*") $Output -Recurse -Force

    $outputFiles = (Get-ChildItem $Output -Recurse -File).Count
    $outputSize = (Get-ChildItem $Output -Recurse -File | Measure-Object -Property Length -Sum).Sum
    $mainDll = (Get-Item (Join-Path $Output "WA2DC_Data\Managed\Assembly-CSharp.dll")).Length
    $firstDll = (Get-Item (Join-Path $Output "WA2DC_Data\Managed\Assembly-CSharp-firstpass.dll")).Length
    $bundleOut = (Get-ChildItem (Join-Path $Output "WA2DC_Data\StreamingAssets") -Filter *.unity3d -File).Count
    Write-Host ""
    Write-Host "BUILD OK"
    Write-Host "  output       : $Output"
    Write-Host "  files / bytes: $outputFiles / $outputSize"
    Write-Host "  bundles      : $bundleOut"
    Write-Host "  main DLL     : $mainDll bytes"
    Write-Host "  firstpass DLL: $firstDll bytes"
    Write-Host "  xapk SHA-256 : $xapkHash"

    $summary.Add("output=$Output")
    $summary.Add("files=$outputFiles")
    $summary.Add("bytes=$outputSize")
    $summary.Add("bundles=$bundleOut")
    $summary.Add("main_dll=$mainDll")
    $summary.Add("firstpass_dll=$firstDll")
    $summary.Add("xapk_sha256=$xapkHash")
    $summary.Add("unity=$unityVersion")
    $summary.Add("unitypy=$unityPy")
    $summary.Add("csc=$cscVersion")
    Set-Content -Path (Join-Path $work "build-info.txt") -Value $summary -Encoding UTF8
}
catch {
    Write-Host ""
    Write-Host "BUILD FAILED: $($_.Exception.Message)" -ForegroundColor Red
    Set-Content -Path (Join-Path $work "FAILED.txt") -Value $_.Exception.ToString() -Encoding UTF8
    exit 1
}

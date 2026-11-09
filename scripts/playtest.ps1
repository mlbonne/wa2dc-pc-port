param(
    [ValidateSet("gl", "d3d")] [string]$Mode = "gl",
    [string]$Build = "",
    [ValidateSet("title", "loadingblack", "loadingburst", "illust", "illustbuy", "rank", "copyright", "communicate", "setup", "nowloading", "ripple", "charaselect", "lesson", "shop", "shopitem", "shopscroll", "shopwheel", "shopwheel2", "shopbg", "shopbgitem", "transitions10", "native_lesson", "native_pv", "gallery", "gallery_pages", "restore", "pvhub", "lesson1", "lesson2", "lesson3", "lessonplay", "pvplay", "pvstage", "pvstages", "pvstage15", "sweep15", "uiprobe", "shop15", "lesson2probe", "pvmouse", "pvleft", "pvauto", "savechange", "savereload", "fullscreen", "kazusa", "saves")] [string]$Scenario = "title",
    [int]$SaveSlotRow = 1,
    [int]$DialogRow = -1,
    [int]$PvRow = 325,
    [int]$PvStage = 1,
    [string]$WpReference = "",
    [switch]$InGameShot = $false,
    [switch]$WrongButton = $false
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Build) { $Build = Join-Path $root "build\WA2DC" }
$build = [System.IO.Path]::GetFullPath($Build)
$log = Join-Path $build "WA2DC_Data\output_log.txt"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Wa2Test {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr hWnd);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string name);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hWnd);
  public delegate bool EnumChildProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumChildProc callback, IntPtr lParam);
  public static bool VisibleWithId(IntPtr parent, int id) {
    bool found = false;
    EnumChildWindows(parent, delegate(IntPtr h, IntPtr l) {
      if (GetDlgCtrlID(h) == id && IsWindowVisible(h)) { found = true; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  public static void Click(int x, int y) { SetCursorPos(x, y); mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(90); mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero); }
}
'@

function Get-WindowRect($process) {
    $process.Refresh()
    $rect = New-Object Wa2Test+RECT
    [Wa2Test]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    return @([int]$rect.Left, [int]$rect.Top, [int]($rect.Right - $rect.Left), [int]($rect.Bottom - $rect.Top))
}

function Get-Rect($process) {
    $process.Refresh()
    $client = New-Object Wa2Test+RECT
    [Wa2Test]::GetClientRect($process.MainWindowHandle, [ref]$client) | Out-Null
    $point = New-Object Wa2Test+POINT
    [Wa2Test]::ClientToScreen($process.MainWindowHandle, [ref]$point) | Out-Null
    return @([int]$point.X, [int]$point.Y, [int]($client.Right - $client.Left), [int]($client.Bottom - $client.Top))
}

function Screen-Point($process, $x, $y) {
    $rect = Get-Rect $process
    $scaleX = $rect[2] / 1024.0
    $scaleY = $rect[3] / 768.0
    return @([int][math]::Round($rect[0] + $x * $scaleX), [int][math]::Round($rect[1] + $y * $scaleY))
}

function Set-Cursor-Rel($process, $x, $y) {
    $point = Screen-Point $process $x $y
    [Wa2Test]::SetCursorPos($point[0], $point[1]) | Out-Null
}

function Assert-Game($process) {
    $process.Refresh()
    if ($process.HasExited) { throw "game exited (ExitCode=$($process.ExitCode))" }
    if ($process.MainWindowHandle -eq 0) { throw "no MainWindowHandle" }
    if ($process.MainWindowTitle -ne "WA2DC") { throw "unexpected window title: $($process.MainWindowTitle)" }
}

function Focus-Game($process) {
    Assert-Game $process
    [Wa2Test]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 150
}

function Dismiss-Config($process) {
    $button = [Wa2Test]::FindWindowExW($process.MainWindowHandle, [IntPtr]::Zero, "Button", "Play!")
    if ($button -eq [IntPtr]::Zero) { return $false }
    [Wa2Test]::SendMessageW($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 2
    return $true
}

function Save-DialogShot($dialog, $name) {
    $r = New-Object Wa2Test+RECT
    [Wa2Test]::GetWindowRect($dialog, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save((Join-Path $build "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "saved $name (dialog ${w}x${h})"
}

function Select-Saves-Dialog($process, $row) {
    $dialog = $process.MainWindowHandle
    if ($dialog -eq [IntPtr]::Zero) { return $false }
    $list = [IntPtr]::Zero
    $tab = [IntPtr]::Zero
    for ($i = 0; $i -lt 30; $i++) {
        $tab = [Wa2Test]::FindWindowExW($dialog, [IntPtr]::Zero, "SysTabControl32", $null)
        $list = [Wa2Test]::GetDlgItem($dialog, 2001)
        if ($tab -ne [IntPtr]::Zero -and $list -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($tab -eq [IntPtr]::Zero -or $list -eq [IntPtr]::Zero) {
        Write-Host "saves: tab=$tab list=$list (hook not ready)"
        return $false
    }
    [Wa2Test]::SetForegroundWindow($dialog) | Out-Null
    Start-Sleep -Milliseconds 400
    $tr = New-Object Wa2Test+RECT
    [Wa2Test]::GetWindowRect($tab, [ref]$tr) | Out-Null
    [Wa2Test]::Click($tr.Left + 75, $tr.Top + 11)
    Start-Sleep -Milliseconds 700

    # Hover every save row, then cycle Graphics -> Saves three times. Unity's
    # Input page used to repaint over the list during these steps.
    $lr = New-Object Wa2Test+RECT
    [Wa2Test]::GetWindowRect($list, [ref]$lr) | Out-Null
    for ($i = 0; $i -lt 4; $i++) {
        [Wa2Test]::SetCursorPos($lr.Left + 60, $lr.Top + 24 + 20 * $i + 10) | Out-Null
        Start-Sleep -Milliseconds 300
    }
    for ($cycle = 0; $cycle -lt 3; $cycle++) {
        [Wa2Test]::Click($tr.Left + 40, $tr.Top + 11)
        Start-Sleep -Milliseconds 500
        [Wa2Test]::Click($tr.Left + 75, $tr.Top + 11)
        Start-Sleep -Milliseconds 500
    }
    Save-DialogShot $dialog "t_saves_dialog"
    $inputVisible = [Wa2Test]::VisibleWithId($dialog, 1017)
    $listVisible = [Wa2Test]::VisibleWithId($dialog, 2001)
    Write-Host "saves: input-page-visible=$inputVisible our-list-visible=$listVisible"
    if ($inputVisible) { throw "saves: Unity's Input page is still visible over the Saves list" }
    if (-not $listVisible) { throw "saves: the save list is not visible" }

    for ($i = 0; $i -lt $row; $i++) {
        [System.Windows.Forms.SendKeys]::SendWait("{DOWN}")
        Start-Sleep -Milliseconds 200
    }
    Start-Sleep -Milliseconds 400
    Write-Host "saves: selected row $row with the keyboard"
    return $true
}

function Save-Crop($source, $destination, $x, $y, $w, $h) {
    $bmp = [System.Drawing.Bitmap]::FromFile($source)
    $rect = New-Object System.Drawing.Rectangle($x, $y, $w, $h)
    $part = $bmp.Clone($rect, $bmp.PixelFormat)
    $part.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
    $part.Dispose(); $bmp.Dispose()
}

function Save-Shot($process, $name) {
    Focus-Game $process
    $script:lastShot = Join-Path $build "$name.png"
    if ($InGameShot) {
        $shot = Join-Path $build "port-shot.png"
        $request = Join-Path $build "port-shot.request"
        for ($attempt = 0; $attempt -lt 2; $attempt++) {
            Remove-Item $shot -Force -ErrorAction SilentlyContinue
            Set-Content -Path $request -Value "1" -Encoding ASCII
            for ($wait = 0; $wait -lt 40; $wait++) {
                Start-Sleep -Milliseconds 250
                if (Test-Path $shot) { break }
            }
            if (Test-Path $shot) { break }
            Focus-Game $process
        }
        if (-not (Test-Path $shot)) { throw "in-game screenshot was not written" }
        Copy-Item $shot (Join-Path $build "$name.png") -Force
        Write-Host "saved $name (in-game $((Get-Item $shot).Length) bytes)"
        return @(0, 0, 0, 0)
    }
    Start-Sleep -Milliseconds 600
    $rect = Get-Rect $process
    if ($rect[2] -le 0 -or $rect[3] -le 0) { throw "invalid client rect" }
    $bmp = New-Object System.Drawing.Bitmap($rect[2], $rect[3])
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rect[0], $rect[1], 0, 0, (New-Object System.Drawing.Size($rect[2], $rect[3])))
    $bmp.Save((Join-Path $build "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "saved $name ($($rect[2])x$($rect[3]))"
    return $rect
}

function Click-Rel($process, $x, $y) {
    Focus-Game $process
    if ($WrongButton) { $x = 40; $y = 40 }
    $point = Screen-Point $process $x $y
    [Wa2Test]::Click($point[0], $point[1])
}

function Get-Signature($path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    $small = New-Object System.Drawing.Bitmap(64, 48)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($bmp, 0, 0, 64, 48)
    $sig = New-Object 'System.Byte[]' 3072
    $k = 0
    for ($sy = 0; $sy -lt 48; $sy++) {
        for ($sx = 0; $sx -lt 64; $sx++) {
            $c = $small.GetPixel($sx, $sy)
            $sig[$k] = [byte](($c.R * 30 + $c.G * 59 + $c.B * 11) / 100)
            $k++
        }
    }
    $g.Dispose(); $small.Dispose(); $bmp.Dispose()
    return ,$sig
}

function Get-SignatureMean($sig) {
    $sum = 0
    foreach ($value in $sig) { $sum += $value }
    return $sum / $sig.Length
}

function Get-SignatureMad($a, $b) {
    $sum = 0
    for ($i = 0; $i -lt $a.Length; $i++) {
        $diff = [int]$a[$i] - [int]$b[$i]
        if ($diff -lt 0) { $diff = -$diff }
        $sum += $diff
    }
    return $sum / $a.Length
}

function Assert-Differs($referencePath, $actualPath, $threshold, $label) {
    if (-not (Test-Path $referencePath)) { throw "missing reference capture: $referencePath" }
    if (-not (Test-Path $actualPath)) { throw "missing actual capture: $actualPath" }
    $mad = Get-SignatureMad (Get-Signature $referencePath) (Get-Signature $actualPath)
    Write-Host ("verify {0}: MAD={1:N1}" -f $label, $mad)
    if ($mad -lt $threshold) {
        throw "scenario did not leave the initial screen: $label (MAD=$mad, need >= $threshold)"
    }
}

function Assert-Bright($path, $minMean, $label) {
    if (-not (Test-Path $path)) { throw "missing capture: $path" }
    $mean = Get-SignatureMean (Get-Signature $path)
    Write-Host ("verify {0}: mean={1:N1}" -f $label, $mean)
    if ($mean -lt $minMean) {
        throw "screen is blank/black: $label (mean=$mean, need >= $minMean)"
    }
}

$script:modeKey = "HKCU:\Software\AQUAPLUS\WA2DC"
$script:modeNames = @(
    "Screenmanager Resolution Width_h182942802",
    "Screenmanager Resolution Height_h2627697771",
    "Screenmanager Is Fullscreen mode_h3981298716"
)

function Save-GameWindowMode {
    $saved = @{}
    foreach ($name in $script:modeNames) {
        $value = $null
        $item = Get-ItemProperty -Path $script:modeKey -Name $name -ErrorAction SilentlyContinue
        if ($null -ne $item) { $value = $item.$name }
        $saved[$name] = $value
    }
    return $saved
}

function Set-GameWindowMode($width, $height, $fullscreen) {
    New-Item -Path $script:modeKey -Force | Out-Null
    Set-ItemProperty -Path $script:modeKey -Name $script:modeNames[0] -Value ([int]$width) -Type DWord
    Set-ItemProperty -Path $script:modeKey -Name $script:modeNames[1] -Value ([int]$height) -Type DWord
    Set-ItemProperty -Path $script:modeKey -Name $script:modeNames[2] -Value ([int]$fullscreen) -Type DWord
}

function Restore-GameWindowMode($saved) {
    foreach ($name in $script:modeNames) {
        if ($null -eq $saved[$name]) {
            Remove-ItemProperty -Path $script:modeKey -Name $name -ErrorAction SilentlyContinue
        } else {
            Set-ItemProperty -Path $script:modeKey -Name $name -Value ([int]$saved[$name]) -Type DWord
        }
    }
}

function Send-Key($keys) {
    $shell = New-Object -ComObject WScript.Shell
    $shell.AppActivate("WA2DC") | Out-Null
    Start-Sleep -Milliseconds 400
    $shell.SendKeys($keys)
}

$arguments = if ($Mode -eq "gl") { @("-force-opengl") } else { @("-force-d3d9") }
$argumentLine = $arguments -join " "

$savedMode = Save-GameWindowMode
Set-GameWindowMode 1024 768 0
try { (New-Object -ComObject Shell.Application).MinimizeAll() } catch { }

Remove-Item $log -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath (Join-Path $build "WA2DC.exe") -ArgumentList $argumentLine -WorkingDirectory $build -PassThru
try {
    $ready = $false
    for ($i = 0; $i -lt 45; $i++) {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { throw "game exited during startup" }
        if ($process.MainWindowTitle -eq "WA2DC" -and $process.MainWindowHandle -ne 0) { $ready = $true; break }
        if ($process.MainWindowTitle -like "*Configuration*" -or $process.MainWindowTitle -eq "Error") {
            $pickRow = $DialogRow
            if ($pickRow -lt 0 -and $Scenario -eq "saves") { $pickRow = $SaveSlotRow }
            if ($pickRow -ge 0 -and $process.MainWindowTitle -like "*Configuration*") {
                if (-not (Select-Saves-Dialog $process $pickRow)) {
                    throw "could not select a save row in the configuration dialog"
                }
            }
            if (-not (Dismiss-Config $process)) {
                $configRect = Get-WindowRect $process
                [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
                Start-Sleep -Milliseconds 500
                [Wa2Test]::Click($configRect[0] + 330, $configRect[1] + 408)
            }
        }
    }
    if (-not $ready) { throw "game window did not appear" }

    [Wa2Test]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    if ([Wa2Test]::IsZoomed($process.MainWindowHandle)) {
        [Wa2Test]::ShowWindow($process.MainWindowHandle, 9) | Out-Null
    }
    [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Seconds 16
    $client = Get-Rect $process
    Write-Host "client rect: $($client[2])x$($client[3]) at $($client[0]),$($client[1])"
    if ($client[2] -le 0 -or $client[3] -le 0) { throw "invalid game client rect" }
    Save-Shot $process "t_initial" | Out-Null
    $script:lastShot = $null

    switch ($Scenario) {
        "loadingblack" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Milliseconds 900
            Save-Shot $process "t_loadingblack"
        }
        "loadingburst" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Focus-Game $process
            for ($frame = 0; $frame -lt 16; $frame++) {
                Remove-Item (Join-Path $build "port-shot.png") -Force -ErrorAction SilentlyContinue
                Set-Content -Path (Join-Path $build "port-shot.request") -Value "1" -Encoding ASCII
                Start-Sleep -Milliseconds 180
                if (Test-Path (Join-Path $build "port-shot.png")) {
                    Copy-Item (Join-Path $build "port-shot.png") (Join-Path $build ("t_burst_" + $frame + ".png")) -Force
                    Remove-Item (Join-Path $build "port-shot.png") -Force -ErrorAction SilentlyContinue
                }
            }
            $initialSig = Get-Signature (Join-Path $build "t_initial.png")
            $sawDark = $false
            $sawChanged = $false
            Get-ChildItem $build -Filter "t_burst_*.png" -File | ForEach-Object {
                $sig = Get-Signature $_.FullName
                if ((Get-SignatureMean $sig) -lt 45) { $sawDark = $true }
                if ((Get-SignatureMad $initialSig $sig) -ge 8) { $sawChanged = $true }
            }
            Write-Host "verify loadingburst: dark=$sawDark changed=$sawChanged"
            if (-not $sawDark) { throw "loadingburst: no black loading frame was captured" }
            if (-not $sawChanged) { throw "loadingburst: the loading frames never left the title" }
            Assert-Game $process
        }
        "title" {
            Start-Sleep -Seconds 16
            Save-Shot $process "t_title"
            Assert-Bright (Join-Path $build "t_title.png") 60 "title"
        }
        "illust" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 545
            Start-Sleep -Seconds 10
            Save-Shot $process "t_illust"
        }
        "illustbuy" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 545
            Start-Sleep -Seconds 10
            Click-Rel $process 640 620
            Start-Sleep -Seconds 5
            Save-Shot $process "t_illustbuy"
        }
        "rank" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 597
            Start-Sleep -Seconds 10
            Save-Shot $process "t_rank"
        }
        "copyright" {
            Start-Sleep -Seconds 16
            Click-Rel $process 695 750
            Start-Sleep -Seconds 10
            Save-Shot $process "t_copyright"
        }
        "communicate" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Save-Shot $process "t_communicate"
        }
        "setup" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 118
            Start-Sleep -Seconds 5
            Save-Shot $process "t_setup"
        }
        "nowloading" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 5
            Save-Shot $process "t_nowloading_before"
        }
        "ripple" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Milliseconds 300
            Save-Shot $process "t_ripple_before"
        }
        "charaselect" {            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Save-Shot $process "t_back_dialog" | Out-Null
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Save-Shot $process "t_charaselect"
        }
        "lesson" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Save-Shot $process "t_lesson"
        }
        "lesson1" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 500 193
            Start-Sleep -Seconds 20
            Save-Shot $process "t_lesson1"
        }
        "lesson2" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 500 275
            Start-Sleep -Seconds 20
            Save-Shot $process "t_lesson2"
        }
        "lesson3" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 500 355
            Start-Sleep -Seconds 20
            Save-Shot $process "t_lesson3"
        }
        "lessonplay" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 500 193
            Start-Sleep -Seconds 20
            for ($step = 0; $step -lt 10; $step++) {
                Click-Rel $process 515 600
                Start-Sleep -Seconds 3
            }
            Save-Shot $process "t_lessonplay"
        }
        "pvplay" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 500 462
            Start-Sleep -Seconds 25
            Save-Shot $process "t_pvplay"
        }
        "pvstage" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 8
            Click-Rel $process 515 620
            Start-Sleep -Seconds 15
            Save-Shot $process "t_pvlist" | Out-Null
            Click-Rel $process 515 $PvRow
            Start-Sleep -Seconds 5
            Click-Rel $process 515 725
            Start-Sleep -Milliseconds 300
            for ($frame = 0; $frame -lt 36; $frame++) {
                Save-Shot $process ("t_pvstage_" + $frame) | Out-Null
                Start-Sleep -Milliseconds 500
            }
        }
        "pvstages" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 8
            Click-Rel $process 515 620
            Start-Sleep -Seconds 15
            $rows = @(325, 380, 435, 490, 545)
            for ($song = 0; $song -lt $rows.Count; $song++) {
                Click-Rel $process 515 $rows[$song]
                Start-Sleep -Seconds 4
                Click-Rel $process 515 725
                Start-Sleep -Seconds 10
                Save-Shot $process ("t_pvstage_song" + $song) | Out-Null
                Start-Sleep -Seconds 14
                Save-Shot $process ("t_pvstage_song" + $song + "b") | Out-Null
                Click-Rel $process 300 58
                Start-Sleep -Seconds 12
            }
            foreach ($tab in @(@('costume', 300, 88), @('head', 410, 88), @('face', 535, 88), @('background', 635, 88))) {
                Click-Rel $process $tab[1] $tab[2]
                Start-Sleep -Seconds 6
                Save-Shot $process ("t_pv" + $tab[0]) | Out-Null
                Click-Rel $process 300 58
                Start-Sleep -Seconds 6
            }
        }
        "pvstage15" {
            Start-Sleep -Seconds 16
            Start-Sleep -Seconds 38
            Save-Shot $process ("t_stage" + $PvStage + "_a") | Out-Null
            Start-Sleep -Seconds 8
            Save-Shot $process ("t_stage" + $PvStage + "_b") | Out-Null
            Start-Sleep -Seconds 8
            Save-Shot $process ("t_stage" + $PvStage + "_c") | Out-Null
        }
        "shop15" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 3
            Click-Rel $process 515 490
            Start-Sleep -Seconds 12
            Click-Rel $process 455 455
            Start-Sleep -Seconds 20
            Start-Sleep -Seconds 15
            Save-Shot $process "t_shop15"
        }
        "sweep15" {
            Start-Sleep -Seconds 16
            # The debug hook loads PVSelect once and opens the requested tab.
            Set-Content -Path (Join-Path $build "pv-tab.request") -Value "clothes" -Encoding ASCII
            Start-Sleep -Seconds 30
            Save-Shot $process "t_sweep_tab_clothes" | Out-Null
            Click-Rel $process 742 416
            Start-Sleep -Seconds 4
            Save-Shot $process "t_sweep_tab_clothes_next" | Out-Null
            foreach ($tab in @("headaccessories", "faceaccessories", "background", "music")) {
                Set-Content -Path (Join-Path $build "pv-tab.request") -Value $tab -Encoding ASCII
                Start-Sleep -Seconds 8
                Save-Shot $process ("t_sweep_tab_" + $tab) | Out-Null
                Click-Rel $process 742 416
                Start-Sleep -Seconds 4
                Save-Shot $process ("t_sweep_tab_" + $tab + "_next") | Out-Null
            }
            # Direct stage launch through the game's own PV scene path.
            Set-Content -Path (Join-Path $build "pv-stage.request") -Value 1 -Encoding ASCII
            Start-Sleep -Seconds 35
            Save-Shot $process "t_sweep_stage1_a" | Out-Null
            Start-Sleep -Seconds 8
            Save-Shot $process "t_sweep_stage1_b" | Out-Null
            Start-Sleep -Seconds 8
            Save-Shot $process "t_sweep_stage1_c" | Out-Null
            Set-Content -Path (Join-Path $build "pv-back.request") -Value 1 -Encoding ASCII
            Start-Sleep -Seconds 20
            for ($stage = 2; $stage -le 5; $stage++) {
                Set-Content -Path (Join-Path $build "pv-stage.request") -Value $stage -Encoding ASCII
                Start-Sleep -Seconds 30
                Save-Shot $process ("t_sweep_stage" + $stage + "_a") | Out-Null
                Start-Sleep -Seconds 8
                Save-Shot $process ("t_sweep_stage" + $stage + "_b") | Out-Null
                Start-Sleep -Seconds 8
                Save-Shot $process ("t_sweep_stage" + $stage + "_c") | Out-Null
                Set-Content -Path (Join-Path $build "pv-back.request") -Value 1 -Encoding ASCII
                Start-Sleep -Seconds 20
            }
            Save-Shot $process "t_sweep_end"
        }
        "uiprobe" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 3
            Save-Shot $process "t_uiprobe_1" | Out-Null
            Click-Rel $process 515 490
            Start-Sleep -Seconds 12
            Save-Shot $process "t_uiprobe_2" | Out-Null
            Click-Rel $process 455 455
            Start-Sleep -Seconds 15
            Save-Shot $process "t_uiprobe_3" | Out-Null
        }
        "lesson2probe" {            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 580
            Start-Sleep -Seconds 15
            Click-Rel $process 400 260
            Start-Sleep -Seconds 6
            Save-Shot $process "t_l2p1" | Out-Null
            Click-Rel $process 500 275
            Start-Sleep -Seconds 6
            Save-Shot $process "t_l2p2" | Out-Null
            Click-Rel $process 400 290
            Start-Sleep -Seconds 6
            Save-Shot $process "t_l2p3"
        }
        "shopitem" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            Click-Rel $process 310 505
            Start-Sleep -Seconds 6
            Save-Shot $process "t_shopitem"
        }
        "shopscroll" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            $rect = Get-Rect $process
            Set-Cursor-Rel $process 470 700
            [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 200
            for ($step = 1; $step -le 8; $step++) {
                Set-Cursor-Rel $process 470 (700 - $step * 20)
                Start-Sleep -Milliseconds 60
            }
            [Wa2Test]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Seconds 3
            Save-Shot $process "t_shopscroll"
        }
        "shop" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            Save-Shot $process "t_shop"
        }
        "shopwheel" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            $rect = Get-Rect $process
            Set-Cursor-Rel $process 470 650
            [Wa2Test]::mouse_event(0x0800, 0, 0, [uint32]4294967176, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 300
            [Wa2Test]::mouse_event(0x0800, 0, 0, [uint32]4294967176, [UIntPtr]::Zero)
            Start-Sleep -Seconds 3
            Save-Shot $process "t_shopwheel"
        }
        "shopwheel2" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            $rect = Get-Rect $process
            [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
            $point = Screen-Point $process 470 650
            $screenX = $point[0]
            $screenY = $point[1]
            $wheelDown = [int64](((-120) -band 0xFFFF) * 65536)
            $lparam = [int64](($screenY * 65536) + $screenX)
            [Wa2Test]::PostMessage($process.MainWindowHandle, 0x020A, [IntPtr]$wheelDown, [IntPtr]$lparam) | Out-Null
            Start-Sleep -Milliseconds 500
            [Wa2Test]::PostMessage($process.MainWindowHandle, 0x020A, [IntPtr]$wheelDown, [IntPtr]$lparam) | Out-Null
            Start-Sleep -Seconds 3
            Save-Shot $process "t_shopwheel2"
        }
        "shopbg" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            $rect = Get-Rect $process
            for ($drag = 0; $drag -lt 3; $drag++) {
                Set-Cursor-Rel $process 470 700
                [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 200
                for ($step = 1; $step -le 15; $step++) {
                    Set-Cursor-Rel $process 470 (700 - $step * 20)
                    Start-Sleep -Milliseconds 50
                }
                [Wa2Test]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 500
            }
            Start-Sleep -Seconds 3
            Save-Shot $process "t_shopbg"
        }
        "shopbgitem" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            $rect = Get-Rect $process
            for ($drag = 0; $drag -lt 3; $drag++) {
                Set-Cursor-Rel $process 470 700
                [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 200
                for ($step = 1; $step -le 15; $step++) {
                    Set-Cursor-Rel $process 470 (700 - $step * 20)
                    Start-Sleep -Milliseconds 50
                }
                [Wa2Test]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 500
            }
            Click-Rel $process 307 658
            Start-Sleep -Seconds 6
            Save-Shot $process "t_shopbgitem"
        }
        "transitions10" {
            Start-Sleep -Seconds 16
            $menus = @(
                @(515, 545),
                @(515, 597),
                @(695, 750),
                @(515, 545),
                @(515, 597)
            )
            foreach ($menu in $menus) {
                Click-Rel $process $menu[0] $menu[1]
                Start-Sleep -Seconds 9
                Click-Rel $process 300 58
                Start-Sleep -Seconds 9
            }
            Start-Sleep -Seconds 4
            Save-Shot $process "t_transitions10"
        }
        "native_lesson" {
            Start-Sleep -Seconds 16
            Click-Rel $process 964 724
            Start-Sleep -Seconds 10
            Save-Shot $process "t_native_illust" | Out-Null
            Click-Rel $process 662 39
            Start-Sleep -Seconds 10
            Click-Rel $process 964 647
            Start-Sleep -Seconds 10
            Click-Rel $process 964 774
            Start-Sleep -Seconds 12
            Save-Shot $process "t_native_lessonlist" | Out-Null
            Click-Rel $process 943 229
            Start-Sleep -Seconds 15
            Save-Shot $process "t_native_lesson1"
        }
        "native_pv" {
            Start-Sleep -Seconds 16
            Click-Rel $process 964 647
            Start-Sleep -Seconds 10
            Click-Rel $process 964 871
            Start-Sleep -Seconds 12
            Click-Rel $process 697 124
            Start-Sleep -Seconds 8
            Save-Shot $process "t_native_pv_before" | Out-Null
            Click-Rel $process 1281 585
            Start-Sleep -Seconds 4
            Save-Shot $process "t_native_pv"
        }
        "gallery" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 545
            Start-Sleep -Seconds 10
            $cells = @(
                @(370, 270), @(500, 270), @(630, 270),
                @(370, 390), @(500, 390), @(630, 390),
                @(370, 510), @(500, 510)
            )
            $index = 0
            foreach ($cell in $cells) {
                $index++
                Click-Rel $process $cell[0] $cell[1]
                Start-Sleep -Seconds 5
                Save-Shot $process ("t_gallery_" + $index) | Out-Null
                if ($index -lt $cells.Count) {
                    Click-Rel $process 300 58
                    Start-Sleep -Seconds 4
                }
            }
            Save-Shot $process "t_gallery_list"
        }
        "gallery_pages" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 545
            Start-Sleep -Seconds 10
            Click-Rel $process 735 435
            Start-Sleep -Seconds 4
            Save-Shot $process "t_gallery_page2" | Out-Null
            Click-Rel $process 310 435
            Start-Sleep -Seconds 4
            Save-Shot $process "t_gallery_page1" | Out-Null
            $rect = Get-Rect $process
            Set-Cursor-Rel $process 560 400
            [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 150
            for ($step = 1; $step -le 10; $step++) {
                Set-Cursor-Rel $process (560 - $step * 20) 400
                Start-Sleep -Milliseconds 40
            }
            [Wa2Test]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Seconds 3
            Save-Shot $process "t_gallery_drag" | Out-Null
            Click-Rel $process 300 58
            Start-Sleep -Seconds 8
            Save-Shot $process "t_gallery_title"
        }
        "restore" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 710
            Start-Sleep -Seconds 15
            Click-Rel $process 400 115
            Start-Sleep -Seconds 8
            Save-Shot $process "t_restore"
        }
        "pvhub" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Save-Shot $process "t_pvhub"
        }
        "pvmouse" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Click-Rel $process 325 118
            Start-Sleep -Seconds 8
            Click-Rel $process 740 446
            Start-Sleep -Seconds 5
            Save-Shot $process "t_pvmouse"
        }
        "pvleft" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Click-Rel $process 325 118
            Start-Sleep -Seconds 8
            Click-Rel $process 740 446
            Start-Sleep -Seconds 2
            Click-Rel $process 740 446
            Start-Sleep -Seconds 2
            Click-Rel $process 285 446
            Start-Sleep -Seconds 5
            Save-Shot $process "t_pvleft"
        }
        "pvauto" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Click-Rel $process 565 755
            Start-Sleep -Seconds 10
            Save-Shot $process "t_pvauto"
        }
        "savechange" {
            $savePath = Join-Path $build "Documents\save.bin"
            if (-not (Test-Path $savePath)) { throw "savechange requires a seeded Documents\save.bin" }
            $saveBefore = (Get-FileHash $savePath -Algorithm SHA256).Hash
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Click-Rel $process 325 118
            Start-Sleep -Seconds 8
            Click-Rel $process 740 446
            Start-Sleep -Seconds 4
            Save-Shot $process "t_savechange_before_exit" | Out-Null
            Assert-Differs (Join-Path $build "t_initial.png") (Join-Path $build "t_savechange_before_exit.png") 8 "savechange/PV"
            Click-Rel $process 300 58
            Start-Sleep -Seconds 12
            Save-Shot $process "t_savechange_exit"
            $saveAfter = (Get-FileHash $savePath -Algorithm SHA256).Hash
            if ($saveBefore -eq $saveAfter) { throw "savechange did not change Documents\save.bin" }
            Write-Host "verify savechange: save.bin changed"
        }
        "savereload" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 515 505
            Start-Sleep -Seconds 12
            Click-Rel $process 300 58
            Start-Sleep -Seconds 4
            Click-Rel $process 455 455
            Start-Sleep -Seconds 25
            Click-Rel $process 515 650
            Start-Sleep -Seconds 15
            Save-Shot $process "t_savereload"
        }
        "saves" {
            Start-Sleep -Seconds 16
            Save-Shot $process "t_saves"
            Assert-Bright (Join-Path $build "t_saves.png") 60 "saves"
            $reference = $WpReference
            if (-not $reference) { $reference = Join-Path $root "scripts\references\wp-25460.png" }
            if (-not (Test-Path $reference)) { throw "saves: WP reference not found: $reference" }
            $actualCrop = Join-Path $build "t_saves_wp.png"
            Save-Crop (Join-Path $build "t_saves.png") $actualCrop 445 665 190 45
            $mad = Get-SignatureMad (Get-Signature $actualCrop) (Get-Signature $reference)
            Write-Host ("verify saves: WP counter MAD={0:N1}" -f $mad)
            if ($mad -gt 8) { throw "saves: title WP counter does not match the slot reference (MAD=$mad)" }
            Assert-Game $process
        }
        "kazusa" {
            Start-Sleep -Seconds 16
            Click-Rel $process 515 490
            Start-Sleep -Seconds 5
            Click-Rel $process 650 300
            Start-Sleep -Seconds 3
            Click-Rel $process 515 655
            Start-Sleep -Seconds 15
            Save-Shot $process "t_kazusa_pv"
        }
        "fullscreen" {
            Start-Sleep -Seconds 16
            $r = Get-Rect $process
            Write-Host "windowed rect: $($r[0]),$($r[1]),$($r[2]),$($r[3])"
            Send-Key "%{ENTER}"
            Start-Sleep -Seconds 6
            Assert-Game $process
            $r = Get-Rect $process
            Write-Host "fullscreen rect: $($r[0]),$($r[1]),$($r[2]),$($r[3])"
            Send-Key "%{ENTER}"
            Start-Sleep -Seconds 6
            Assert-Game $process
            $r = Get-Rect $process
            Write-Host "restored rect: $($r[0]),$($r[1]),$($r[2]),$($r[3])"
            Save-Shot $process "t_windowed_restored" | Out-Null
        }
    }

    $skipDiff = @("title", "loadingblack", "loadingburst", "savechange", "fullscreen", "saves")
    if ($script:lastShot -and ($skipDiff -notcontains $Scenario)) {
        Assert-Differs (Join-Path $build "t_initial.png") $script:lastShot 8 $Scenario
    }

    Assert-Game $process
    Write-Host "game alive after scenario=$Scenario"
    if (Test-Path $log) {
        $errors = Get-Content $log | Select-String -Pattern "CreateDirectory|TypeLoadException|different serialization|build target|NullReference|Exception|Error" |
            ForEach-Object { $_.Line.Trim() } | Where-Object { $_ -notmatch "^Extensions:" } | Select-Object -First 30
        if ($errors) { Write-Host "--- log matches ---"; $errors } else { Write-Host "no matching log errors" }
    }
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; Write-Host "stopped" }
    if ($savedMode) { Restore-GameWindowMode $savedMode }
    try { (New-Object -ComObject Shell.Application).UndoMinimizeALL() } catch { }
}





























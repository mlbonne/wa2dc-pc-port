param(
    [ValidateSet("gl", "d3d")] [string]$Mode = "gl",
    [string]$Build = "",
    [ValidateSet("title", "loadingblack", "loadingburst", "illust", "illustbuy", "rank", "copyright", "communicate", "setup", "nowloading", "ripple", "charaselect", "lesson", "shop", "shopitem", "shopscroll", "shopwheel", "shopwheel2", "shopbg", "shopbgitem", "transitions10", "native_lesson", "native_pv", "gallery", "gallery_pages", "restore", "pvhub", "lesson1", "lesson2", "lesson3", "lessonplay", "pvplay", "lesson2probe", "pvmouse", "pvleft", "pvauto", "savechange", "savereload", "fullscreen", "kazusa")] [string]$Scenario = "title",
    [switch]$InGameShot = $false
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Build) { $Build = Join-Path $root "build\WA2DC" }
$build = [System.IO.Path]::GetFullPath($Build)
$log = Join-Path $build "WA2DC_Data\output_log.txt"

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Wa2Test {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
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

function Get-Rect($process) {
    $process.Refresh()
    $rect = New-Object Wa2Test+RECT
    [Wa2Test]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    return @([int]$rect.Left, [int]$rect.Top, [int]($rect.Right - $rect.Left), [int]($rect.Bottom - $rect.Top))
}

function Assert-Game($process) {
    $process.Refresh()
    if ($process.HasExited) { throw "game exited (ExitCode=$($process.ExitCode))" }
    if ($process.MainWindowHandle -eq 0) { throw "no MainWindowHandle" }
    if ($process.MainWindowTitle -ne "WA2DC") { throw "unexpected window title: $($process.MainWindowTitle)" }
}

function Save-Shot($process, $name) {
    Assert-Game $process
    if ($InGameShot) {
        $shot = Join-Path $build "port-shot.png"
        Remove-Item $shot -Force -ErrorAction SilentlyContinue
        Set-Content -Path (Join-Path $build "port-shot.request") -Value "1" -Encoding ASCII
        for ($wait = 0; $wait -lt 20; $wait++) {
            Start-Sleep -Milliseconds 250
            if (Test-Path $shot) { break }
        }
        if (-not (Test-Path $shot)) { throw "in-game screenshot was not written" }
        Copy-Item $shot (Join-Path $build "$name.png") -Force
        Write-Host "saved $name (in-game $((Get-Item $shot).Length) bytes)"
        return @(0, 0, 0, 0)
    }
    [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 600
    $rect = Get-Rect $process
    if ($rect[2] -le 0 -or $rect[3] -le 0) { throw "invalid window rect" }
    $bmp = New-Object System.Drawing.Bitmap($rect[2], $rect[3])
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rect[0], $rect[1], 0, 0, (New-Object System.Drawing.Size($rect[2], $rect[3])))
    $bmp.Save((Join-Path $build "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "saved $name ($($rect[2])x$($rect[3]))"
    return $rect
}

function Click-Rel($process, $x, $y) {
    Assert-Game $process
    $rect = Get-Rect $process
    [Wa2Test]::Click($rect[0] + $x, $rect[1] + $y)
}

function Send-Key($keys) {
    $shell = New-Object -ComObject WScript.Shell
    $shell.AppActivate("WA2DC") | Out-Null
    Start-Sleep -Milliseconds 400
    $shell.SendKeys($keys)
}

$arguments = if ($Mode -eq "gl") { @("-force-opengl") } else { @("-force-d3d9") }
$argumentLine = $arguments -join " "

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
            $configRect = Get-Rect $process
            [Wa2Test]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
            Start-Sleep -Milliseconds 500
            [Wa2Test]::Click($configRect[0] + 330, $configRect[1] + 408)
        }
    }
    if (-not $ready) { throw "game window did not appear" }

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
            for ($frame = 0; $frame -lt 16; $frame++) {
                Remove-Item (Join-Path $build "port-shot.png") -Force -ErrorAction SilentlyContinue
                Set-Content -Path (Join-Path $build "port-shot.request") -Value "1" -Encoding ASCII
                Start-Sleep -Milliseconds 180
                if (Test-Path (Join-Path $build "port-shot.png")) {
                    Copy-Item (Join-Path $build "port-shot.png") (Join-Path $build ("t_burst_" + $frame + ".png")) -Force
                    Remove-Item (Join-Path $build "port-shot.png") -Force -ErrorAction SilentlyContinue
                }
            }
            Assert-Game $process
        }
        "title" {
            Start-Sleep -Seconds 16
            Save-Shot $process "t_title"
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
            [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700) | Out-Null
            [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 200
            for ($step = 1; $step -le 8; $step++) {
                [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700 - $step * 20) | Out-Null
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
            [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 650) | Out-Null
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
            $screenX = $rect[0] + 470
            $screenY = $rect[1] + 650
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
                [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700) | Out-Null
                [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 200
                for ($step = 1; $step -le 15; $step++) {
                    [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700 - $step * 20) | Out-Null
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
                [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700) | Out-Null
                [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 200
                for ($step = 1; $step -le 15; $step++) {
                    [Wa2Test]::SetCursorPos($rect[0] + 470, $rect[1] + 700 - $step * 20) | Out-Null
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
                Click-Rel $process 300 58
                Start-Sleep -Seconds 4
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
            [Wa2Test]::SetCursorPos($rect[0] + 560, $rect[1] + 400) | Out-Null
            [Wa2Test]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 150
            for ($step = 1; $step -le 10; $step++) {
                [Wa2Test]::SetCursorPos($rect[0] + 560 - $step * 20, $rect[1] + 400) | Out-Null
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
            Click-Rel $process 300 58
            Start-Sleep -Seconds 12
            Save-Shot $process "t_savechange_exit"
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
}





























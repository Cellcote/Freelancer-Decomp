# Experiment: how far does the unmodified freelancer.exe get on a GitHub Windows runner
# (no GPU, no sound card, no user at the keyboard)? Changes no game code.
#
# Runs the client windowed, logs every window it opens (title, class, child texts), answers
# the EULA dialog with its Accept button, presses Escape now and then to skip movies, and
# samples the screen. Screenshots never leave the runner: only brightness statistics and a
# coarse ASCII thumbnail go to the log. At the end prints the tail of FLSpew.txt.
#
# Usage (Windows PowerShell 5.1, from the repository root):
#     powershell -File .github\scripts\client_probe.ps1 -Bin bin -Seconds 150

param(
    [string]$Bin = 'bin',
    [int]$Seconds = 150,
    [string]$Arguments = '-w',
    # Run the client under the 32-bit cdb and print the stack of an unhandled access violation.
    [switch]$UnderCdb,
    # Timed input, ';'-separated "<seconds>:<action>":
    #   snap              a detailed (128x48) thumbnail of the client area
    #   click:<fx>,<fy>   left click at that fraction of the client area
    #   key:<vk>          press and release a virtual key (decimal; 27 Escape, 13 Enter, 32 Space)
    [string]$Steps = ''
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class Probe
{
    delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lparam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);

    // The client area in screen coordinates: x, y, width, height.
    public static int[] ClientArea(IntPtr hwnd)
    {
        RECT r; POINT p = new POINT();
        if (!GetClientRect(hwnd, out r) || !ClientToScreen(hwnd, ref p)) return null;
        return new int[] { p.X, p.Y, r.Right - r.Left, r.Bottom - r.Top };
    }

    // A click as the client sees one: the OS cursor moves (WM_MOUSEMOVE re-activates the
    // DirectInput mouse, whose software cursor then jumps to the OS pointer), a small relative
    // move for the DirectInput buffer, then the left button.
    public static void Click(int x, int y)
    {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(150);
        mouse_event(0x0001, 1, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        mouse_event(0x0001, -1, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(150);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(100);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }

    public static string Text(IntPtr hwnd)
    {
        StringBuilder b = new StringBuilder(1024);
        GetWindowText(hwnd, b, b.Capacity);
        return b.ToString();
    }

    public static string Class(IntPtr hwnd)
    {
        StringBuilder b = new StringBuilder(256);
        GetClassName(hwnd, b, b.Capacity);
        return b.ToString();
    }

    public static IntPtr[] TopWindows(uint pid)
    {
        List<IntPtr> found = new List<IntPtr>();
        EnumWindows(delegate (IntPtr h, IntPtr l) {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner == pid) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }

    public static IntPtr[] Children(IntPtr parent)
    {
        List<IntPtr> found = new List<IntPtr>();
        EnumChildWindows(parent, delegate (IntPtr h, IntPtr l) { found.Add(h); return true; }, IntPtr.Zero);
        return found.ToArray();
    }

    // A grey-scale sample of the screen: mean, fraction of non-black pixels, distinct coarse
    // colours, and a 64x20 ASCII thumbnail. The bitmap is never written anywhere.
    public static string Sample(int x, int y, int w, int h, int cols, int rows)
    {
        if (w <= 0 || h <= 0) return "empty rectangle";
        using (System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(w, h))
        {
            using (System.Drawing.Graphics g = System.Drawing.Graphics.FromImage(bmp))
                g.CopyFromScreen(x, y, 0, 0, new System.Drawing.Size(w, h));
            const string ramp = " .:-=+*#%@";
            StringBuilder art = new StringBuilder();
            double sum = 0; long lit = 0, n = 0;
            HashSet<int> colours = new HashSet<int>();
            for (int py = 0; py < h; py += 4)
                for (int px = 0; px < w; px += 4)
                {
                    System.Drawing.Color c = bmp.GetPixel(px, py);
                    double v = 0.299 * c.R + 0.587 * c.G + 0.114 * c.B;
                    sum += v; n++;
                    if (v > 16) lit++;
                    colours.Add(((c.R >> 4) << 8) | ((c.G >> 4) << 4) | (c.B >> 4));
                }
            for (int r = 0; r < rows; r++)
            {
                art.Append("    |");
                for (int col = 0; col < cols; col++)
                {
                    System.Drawing.Color c = bmp.GetPixel(col * w / cols, r * h / rows);
                    double v = 0.299 * c.R + 0.587 * c.G + 0.114 * c.B;
                    art.Append(ramp[Math.Min(ramp.Length - 1, (int)(v * ramp.Length / 256))]);
                }
                art.Append("|\n");
            }
            return string.Format("{0}x{1} mean {2:F1}, lit {3:P1}, coarse colours {4}\n{5}",
                w, h, sum / n, (double)lit / n, colours.Count, art.ToString().TrimEnd('\n'));
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

$BM_CLICK = 0x00F5
$VK_ESCAPE = 0x1B
$KEYEVENTF_KEYUP = 0x2

function Log([string]$Text) {
    Write-Host ('[{0,6:F1}s] {1}' -f $script:clock.Elapsed.TotalSeconds, $Text)
}

function Sample-Screen([IntPtr]$Window, [int]$Cols = 64, [int]$Rows = 20) {
    $area = $(if ($Window -ne [IntPtr]::Zero) { [Probe]::ClientArea($Window) } else { $null })
    if ($area -and $area[2] -gt 32) {
        $x = [Math]::Max(0, $area[0]); $y = [Math]::Max(0, $area[1])
        $w = $area[2]; $h = $area[3]
        $what = "client area"
    } else {
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $x = 0; $y = 0; $w = $screen.Width; $h = $screen.Height
        $what = "whole screen"
    }
    try {
        $sample = [Probe]::Sample($x, $y, $w, $h, $Cols, $Rows)
        if ($sample -eq $script:lastSample) { Log "screen ($what): unchanged" }
        else { Log ("screen ($what): " + $sample); $script:lastSample = $sample }
    }
    catch { Log "screen ($what): capture failed: $($_.Exception.Message)" }
}

Add-Type -AssemblyName System.Windows.Forms

# --- The machine -------------------------------------------------------------------------
Write-Host '== Runner =='
Write-Host "session $([System.Diagnostics.Process]::GetCurrentProcess().SessionId), interactive $([Environment]::UserInteractive), user $env:USERNAME"
Get-CimInstance Win32_VideoController |
    ForEach-Object { Write-Host "video: $($_.Name) | driver $($_.DriverVersion) | $($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution) $($_.CurrentBitsPerPixel)bpp | RAM $($_.AdapterRAM)" }
Get-CimInstance Win32_SoundDevice | ForEach-Object { Write-Host "sound: $($_.Name)" }
Write-Host "screen: $([System.Windows.Forms.Screen]::PrimaryScreen.Bounds)"
foreach ($dll in 'd3d8.dll', 'd3d9.dll', 'dsound.dll', 'dinput8.dll', 'dinput.dll', 'quartz.dll', 'wmvdecod.dll') {
    $path = Join-Path $env:WINDIR "SysWOW64\$dll"
    Write-Host ("{0,-12} {1}" -f $dll, $(if (Test-Path $path) { 'present' } else { 'MISSING' }))
}

# --- The run -----------------------------------------------------------------------------
$binPath = (Resolve-Path $Bin).Path
Write-Host "== Starting $binPath\freelancer.exe $Arguments for up to $Seconds s =="
$script:clock = [System.Diagnostics.Stopwatch]::StartNew()
$cdbOut = Join-Path $env:RUNNER_TEMP 'client_cdb.txt'
if ($UnderCdb) {
    $cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
    $env:_NT_SYMBOL_PATH = $binPath
    $proc = Start-Process -FilePath $cdb -WorkingDirectory $binPath -PassThru -NoNewWindow `
        -RedirectStandardOutput $cdbOut -ArgumentList @('-lines', '-G',
            '-c', '"sxd -c2 \".ecxr;r;kn 40;q\" av; g; q"',
            (Join-Path $binPath 'freelancer.exe'), $Arguments)
} else {
    $proc = Start-Process -FilePath (Join-Path $binPath 'freelancer.exe') -ArgumentList $Arguments `
        -WorkingDirectory $binPath -PassThru
}
$null = $proc.Handle
$gamePid = $(if ($UnderCdb) { 0 } else { $proc.Id })

$seen = @{}
$gameWindow = [IntPtr]::Zero
$nextSample = 5
$spewPath = Join-Path $env:LOCALAPPDATA 'Freelancer\FLSpew.txt'
$spewLines = 0
$queue = New-Object System.Collections.Generic.List[object]
foreach ($step in ($Steps -split ';' | Where-Object { $_.Trim() })) {
    $at, $action = $step.Trim() -split ':', 2
    $queue.Add(@{ At = [double]$at; Action = $action })
}
while (-not $proc.HasExited -and $script:clock.Elapsed.TotalSeconds -lt $Seconds) {
    if ($gamePid -eq 0) {
        $game = Get-Process -Name freelancer -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($game) { $gamePid = $game.Id; Log "freelancer.exe is process $gamePid" }
    }
    foreach ($h in [Probe]::TopWindows([uint32]$gamePid)) {
        $title = [Probe]::Text($h)
        $class = [Probe]::Class($h)
        $visible = [Probe]::IsWindowVisible($h)
        $key = "$h|$title|$visible"
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            Log "window 0x$($h.ToString('x')) class '$class' title '$title' visible $visible"
            foreach ($c in [Probe]::Children($h)) {
                $t = [Probe]::Text($c)
                if ($t) { Log "    child '$([Probe]::Class($c))': $($t.Substring(0, [Math]::Min(300, $t.Length)))" }
            }
        }
        if ($class -eq 'Freelancer') { $gameWindow = $h }
        # The EULA dialog (EBUEula.dll): press its Accept button.
        # The client's own "Critical Warning" boxes (e.g. no sound device: "continue without
        # sound?"): press Yes, the answer that keeps the game going.
        $answer = $(if ($title -like '*License Agreement*') { '&Accept' }
                    elseif ($title -like '*Critical Warning*') { '&Yes' } else { $null })
        if ($visible -and $answer) {
            foreach ($c in [Probe]::Children($h)) {
                if ([Probe]::Text($c) -eq $answer) {
                    Log "dialog '$title': pressing $answer"
                    $null = [Probe]::SendMessage($c, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
                }
            }
        }
    }

    # New FLSpew.txt lines, as the game writes them.
    if (Test-Path $spewPath) {
        try {
            $stream = [System.IO.File]::Open($spewPath, 'Open', 'Read', 'ReadWrite')
            $reader = New-Object System.IO.StreamReader($stream)
            $all = $reader.ReadToEnd() -split "`r?`n"
            $reader.Close()
            for ($i = $spewLines; $i -lt $all.Count - 1; $i++) {
                if ($all[$i] -notmatch 'Adding material map|AddLibrary|LoadSystemComponents') { Log "spew: $($all[$i])" }
            }
            $spewLines = [Math]::Max($spewLines, $all.Count - 1)
        } catch { }
    }

    $t = $script:clock.Elapsed.TotalSeconds
    if ($t -ge $nextSample) {
        $game = $(if ($gamePid) { Get-Process -Id $gamePid -ErrorAction SilentlyContinue } else { $null })
        if ($game) {
            Log ("game: cpu {0:F1}s, working set {1:N0} KB, threads {2}, responding {3}, foreground {4}" -f `
                $game.TotalProcessorTime.TotalSeconds, ($game.WorkingSet64 / 1KB), $game.Threads.Count,
                $game.Responding, ([Probe]::GetForegroundWindow() -eq $gameWindow))
        }
        Sample-Screen $gameWindow
        $nextSample = $t + 15
    }

    while ($queue.Count -gt 0 -and $t -ge $queue[0].At -and $gameWindow -ne [IntPtr]::Zero) {
        $action = $queue[0].Action
        $queue.RemoveAt(0)
        $null = [Probe]::SetForegroundWindow($gameWindow)
        if ($action -eq 'snap') {
            Sample-Screen $gameWindow 128 48
        } elseif ($action -like 'click:*') {
            $fx, $fy = ($action.Substring(6) -split ',') | ForEach-Object { [double]$_ }
            $area = [Probe]::ClientArea($gameWindow)
            $x = [int]($area[0] + $fx * $area[2]); $y = [int]($area[1] + $fy * $area[3])
            Log "input: click at ($fx, $fy) = screen ($x, $y)"
            [Probe]::Click($x, $y)
        } elseif ($action -like 'key:*') {
            $vk = [byte]$action.Substring(4)
            Log "input: key $vk"
            [Probe]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 60
            [Probe]::keybd_event($vk, 0, $KEYEVENTF_KEYUP, [UIntPtr]::Zero)
        }
    }
    Start-Sleep -Milliseconds 300
}

if ($UnderCdb -and (Test-Path $cdbOut)) {
    if (-not $proc.HasExited) { Get-Process -Name freelancer -ErrorAction SilentlyContinue | Stop-Process -Force; $null = $proc.WaitForExit(20000) }
    Write-Host "== cdb ($cdbOut) =="
    Get-Content $cdbOut | Select-String -NotMatch -Pattern '^ModLoad:|^\*\*\*|^\s*$|Symbol search path|Executable search path' |
        Select-Object -Last 120 | ForEach-Object { Write-Host $_.Line }
}
if ($proc.HasExited) {
    Log ("freelancer.exe exited by itself with code {0} (0x{0:x8})" -f $proc.ExitCode)
} else {
    Sample-Screen $gameWindow
    Log 'still running at the time limit: stopping it'
    $proc.Kill()
}

# --- The game's own log ------------------------------------------------------------------
$spew = Get-ChildItem -Path $env:USERPROFILE, $env:LOCALAPPDATA -Filter FLSpew.txt -Recurse `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if ($spew) {
    Write-Host "== $($spew.FullName) ($($spew.Length) bytes), last 150 lines =="
    Get-Content $spew.FullName -Tail 150 | Write-Host
} else {
    Write-Host '== no FLSpew.txt found =='
}

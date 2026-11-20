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
    [switch]$UnderCdb
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
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

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
    public static string Sample(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return "empty rectangle";
        using (System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(w, h))
        {
            using (System.Drawing.Graphics g = System.Drawing.Graphics.FromImage(bmp))
                g.CopyFromScreen(x, y, 0, 0, new System.Drawing.Size(w, h));
            const string ramp = " .:-=+*#%@";
            int cols = 64, rows = 20;
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

function Sample-Screen([IntPtr]$Window) {
    $rect = New-Object Probe+RECT
    if ($Window -ne [IntPtr]::Zero -and [Probe]::GetWindowRect($Window, [ref]$rect) -and
        ($rect.Right - $rect.Left) -gt 32) {
        $x = [Math]::Max(0, $rect.Left); $y = [Math]::Max(0, $rect.Top)
        $w = $rect.Right - $x; $h = $rect.Bottom - $y
        $what = "game window"
    } else {
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $x = 0; $y = 0; $w = $screen.Width; $h = $screen.Height
        $what = "whole screen"
    }
    try { Log ("screen ($what): " + [Probe]::Sample($x, $y, $w, $h)) }
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
$nextEscape = 30
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
        if ($visible -and $title -like '*License Agreement*') {
            foreach ($c in [Probe]::Children($h)) {
                if ([Probe]::Text($c) -eq '&Accept') {
                    Log 'EULA: pressing Accept'
                    $null = [Probe]::SendMessage($c, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
                }
            }
        }
    }

    $t = $script:clock.Elapsed.TotalSeconds
    if ($t -ge $nextSample) {
        $proc.Refresh()
        Log ("process: cpu {0:F1}s, working set {1:N0} KB, threads {2}, responding {3}, foreground is game {4}" -f `
            $proc.TotalProcessorTime.TotalSeconds, ($proc.WorkingSet64 / 1KB), $proc.Threads.Count,
            $proc.Responding, ([Probe]::GetForegroundWindow() -eq $gameWindow))
        Sample-Screen $gameWindow
        $nextSample = $t + 15
    }
    # Escape skips the intro movies; keybd_event goes through the real input queue.
    if ($t -ge $nextEscape -and $gameWindow -ne [IntPtr]::Zero) {
        Log 'input: Escape'
        $null = [Probe]::SetForegroundWindow($gameWindow)
        [Probe]::keybd_event($VK_ESCAPE, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 50
        [Probe]::keybd_event($VK_ESCAPE, 0, $KEYEVENTF_KEYUP, [UIntPtr]::Zero)
        $nextEscape = $t + 20
    }
    Start-Sleep -Milliseconds 500
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
